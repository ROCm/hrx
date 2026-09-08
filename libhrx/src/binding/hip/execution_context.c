// Copyright 2026 The HRX Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "binding/hip/execution_context.h"

#include "binding/hip/execution_resource.h"
#include "binding/hip/execution_resource_descriptor.h"
#include "binding/hip/stream.h"
#include "common/internal.h"
#include "common/stream.h"
#include "iree/base/threading/call_once.h"

typedef enum iree_hip_execution_context_kind_e {
  IREE_HIP_EXECUTION_CONTEXT_KIND_PRIMARY = 0,
  IREE_HIP_EXECUTION_CONTEXT_KIND_RESOURCE_PARTITIONED = 1,
} iree_hip_execution_context_kind_t;

// A HIP execution context is a binding-private scheduling domain layered over
// the device primary streaming context. Hardware queues are realized lazily by
// streams; this control-plane object does not itself schedule work.
struct ihipExecutionCtx_t {
  // Determines whether the context is device-managed or resource-partitioned.
  iree_hip_execution_context_kind_t kind;

  // Reference count including the live registry or public-handle ownership.
  iree_atomic_ref_count_t ref_count;

  // Serializes liveness and execution-context stream membership.
  iree_slim_mutex_t mutex;

  // Host allocator owning this context allocation.
  iree_allocator_t host_allocator;

  // Borrowed device registry entry used while the HIP runtime is initialized.
  iree_hal_streaming_device_t* device;

  // Stable ordinal of |device| used without dereferencing the registry entry.
  hipDevice_t device_ordinal;

  // Owning primary-context reference paired with a device usage count.
  iree_hal_streaming_context_t* primary_context;

  // Resource descriptor consumed when this context was created.
  iree_hip_execution_resource_descriptor_t* descriptor;

  // Canonical union of the descriptor SM resources returned by queries.
  hipDevResource sm_resource;

  // Stable process-unique context identifier.
  unsigned long long context_id;

  // Intrusive list of retained streams created on this execution context.
  hipStream_t stream_head;

  // Next context in the live-handle registry.
  struct ihipExecutionCtx_t* next_live_context;
};

typedef struct iree_hip_execution_context_registry_t {
  // Serializes live-handle lookup, publication, and invalidation.
  iree_slim_mutex_t mutex;

  // Intrusive list of live public execution-context handles.
  hipExecutionCtx_t head;
} iree_hip_execution_context_registry_t;

static iree_once_flag iree_hip_execution_context_registry_once =
    IREE_ONCE_FLAG_INIT;
static iree_hip_execution_context_registry_t
    iree_hip_execution_context_registry;

// Next process-unique context identifier. Zero permanently marks exhaustion.
static iree_atomic_uint64_t iree_hip_next_execution_context_id =
    IREE_ATOMIC_VAR_INIT(1);

static void iree_hip_execution_context_registry_initialize(void) {
  iree_slim_mutex_initialize(&iree_hip_execution_context_registry.mutex);
  iree_hip_execution_context_registry.head = NULL;
}

static hipError_t iree_hip_execution_context_consume_status(
    iree_status_t status) {
  const iree_status_code_t status_code = iree_status_code(status);
  iree_status_free(status);
  switch (status_code) {
    case IREE_STATUS_RESOURCE_EXHAUSTED:
      return hipErrorOutOfMemory;
    case IREE_STATUS_INVALID_ARGUMENT:
    case IREE_STATUS_OUT_OF_RANGE:
      return hipErrorInvalidValue;
    case IREE_STATUS_UNIMPLEMENTED:
      return hipErrorNotSupported;
    case IREE_STATUS_FAILED_PRECONDITION:
      return hipErrorNotInitialized;
    default:
      return hipErrorUnknown;
  }
}

static iree_status_t iree_hip_execution_context_allocate_id(
    unsigned long long* out_context_id) {
  uint64_t current = iree_atomic_load(&iree_hip_next_execution_context_id,
                                      iree_memory_order_relaxed);
  while (current != 0) {
    const uint64_t next = current + 1;
    if (iree_atomic_compare_exchange_weak(
            &iree_hip_next_execution_context_id, &current, next,
            iree_memory_order_relaxed, iree_memory_order_relaxed)) {
      *out_context_id = current;
      return iree_ok_status();
    }
  }
  return iree_make_status(IREE_STATUS_RESOURCE_EXHAUSTED,
                          "HIP execution-context ID space is exhausted");
}

static void iree_hip_execution_context_retain(hipExecutionCtx_t context) {
  if (context) iree_atomic_ref_count_inc(&context->ref_count);
}

static void iree_hip_execution_context_release(hipExecutionCtx_t context) {
  if (!context || iree_atomic_ref_count_dec(&context->ref_count) != 1) return;
  IREE_ASSERT(context->device == NULL);
  IREE_ASSERT(context->primary_context == NULL);
  IREE_ASSERT(context->stream_head == NULL);
  iree_hip_execution_resource_descriptor_release(context->descriptor);
  iree_slim_mutex_deinitialize(&context->mutex);
  iree_allocator_free(context->host_allocator, context);
}

static hipError_t iree_hip_execution_context_release_primary(
    iree_hal_streaming_device_t* device,
    iree_hal_streaming_context_t* primary_context) {
  if (!primary_context) return hipSuccess;
  iree_status_t status =
      iree_hal_streaming_device_release_primary_context(device);
  return iree_status_is_ok(status)
             ? hipSuccess
             : iree_hip_execution_context_consume_status(status);
}

static hipError_t iree_hip_execution_context_deinitialize(
    hipExecutionCtx_t context) {
  iree_slim_mutex_lock(&context->mutex);
  iree_hal_streaming_device_t* device = context->device;
  iree_hal_streaming_context_t* primary_context = context->primary_context;
  context->device = NULL;
  context->primary_context = NULL;

  // Remove all members as one state transition. Each list entry owns a stream
  // reference and each association owns a context reference, keeping both
  // objects live while blocking work is drained outside the lock.
  hipStream_t stream_head = context->stream_head;
  context->stream_head = NULL;
  for (hipStream_t stream = stream_head; stream;
       stream = stream->next_execution_context_stream) {
    iree_slim_mutex_lock(&stream->mutex);
    IREE_ASSERT(stream->execution_context == context);
    stream->execution_context = NULL;
    iree_slim_mutex_unlock(&stream->mutex);
  }
  iree_slim_mutex_unlock(&context->mutex);

  hipError_t result = hipSuccess;
  while (stream_head) {
    hipStream_t stream = stream_head;
    stream_head = stream->next_execution_context_stream;
    stream->next_execution_context_stream = NULL;
    iree_hal_streaming_stream_t* common_stream = NULL;
    iree_hal_streaming_context_t* common_context = NULL;
    if (iree_hip_stream_detach(stream, &common_stream, &common_context)) {
      if (common_context) {
        iree_status_t status =
            iree_hal_streaming_stream_synchronize(common_stream);
        if (!iree_status_is_ok(status)) {
          const hipError_t synchronize_result =
              iree_hip_execution_context_consume_status(status);
          if (result == hipSuccess) result = synchronize_result;
        }
        iree_hal_streaming_context_unregister_stream(common_context,
                                                     common_stream);
      }
      iree_hal_streaming_stream_release(common_stream);
      iree_hal_streaming_context_release(common_context);
    }
    iree_hip_execution_context_release(context);
    iree_hip_stream_release(stream);
  }

  if (context->kind == IREE_HIP_EXECUTION_CONTEXT_KIND_RESOURCE_PARTITIONED) {
    const hipError_t primary_result =
        iree_hip_execution_context_release_primary(device, primary_context);
    if (result == hipSuccess) result = primary_result;
  }
  return result;
}

static hipExecutionCtx_t iree_hip_execution_context_lookup_retain(
    hipExecutionCtx_t handle) {
  if (!handle) return NULL;
  iree_call_once(&iree_hip_execution_context_registry_once,
                 iree_hip_execution_context_registry_initialize);

  hipExecutionCtx_t retained_context = NULL;
  iree_slim_mutex_lock(&iree_hip_execution_context_registry.mutex);
  for (hipExecutionCtx_t current = iree_hip_execution_context_registry.head;
       current; current = current->next_live_context) {
    if (current == handle) {
      iree_atomic_ref_count_inc(&current->ref_count);
      retained_context = current;
      break;
    }
  }
  iree_slim_mutex_unlock(&iree_hip_execution_context_registry.mutex);
  return retained_context;
}

static void iree_hip_execution_context_publish(hipExecutionCtx_t context) {
  iree_call_once(&iree_hip_execution_context_registry_once,
                 iree_hip_execution_context_registry_initialize);
  iree_slim_mutex_lock(&iree_hip_execution_context_registry.mutex);
  context->next_live_context = iree_hip_execution_context_registry.head;
  iree_hip_execution_context_registry.head = context;
  iree_slim_mutex_unlock(&iree_hip_execution_context_registry.mutex);
}

static hipExecutionCtx_t iree_hip_execution_context_take_partitioned(
    hipExecutionCtx_t handle) {
  if (!handle) return NULL;
  iree_call_once(&iree_hip_execution_context_registry_once,
                 iree_hip_execution_context_registry_initialize);

  hipExecutionCtx_t owned_context = NULL;
  iree_slim_mutex_lock(&iree_hip_execution_context_registry.mutex);
  hipExecutionCtx_t* link = &iree_hip_execution_context_registry.head;
  while (*link && *link != handle) {
    link = &(*link)->next_live_context;
  }
  if (*link &&
      (*link)->kind == IREE_HIP_EXECUTION_CONTEXT_KIND_RESOURCE_PARTITIONED) {
    owned_context = *link;
    *link = owned_context->next_live_context;
    owned_context->next_live_context = NULL;
  }
  iree_slim_mutex_unlock(&iree_hip_execution_context_registry.mutex);
  return owned_context;
}

hipError_t iree_hip_execution_context_primary(
    iree_hal_streaming_device_t* device, hipExecutionCtx_t* out_context) {
  IREE_ASSERT_ARGUMENT(device);
  IREE_ASSERT_ARGUMENT(out_context);
  iree_call_once(&iree_hip_execution_context_registry_once,
                 iree_hip_execution_context_registry_initialize);

  hipError_t result = hipSuccess;
  hipExecutionCtx_t context = NULL;
  iree_slim_mutex_lock(&iree_hip_execution_context_registry.mutex);
  for (hipExecutionCtx_t current = iree_hip_execution_context_registry.head;
       current; current = current->next_live_context) {
    if (current->kind == IREE_HIP_EXECUTION_CONTEXT_KIND_PRIMARY &&
        current->device_ordinal == (hipDevice_t)device->ordinal) {
      context = current;
      break;
    }
  }

  iree_hal_streaming_context_t* primary_context = NULL;
  if (!context) {
    iree_status_t status =
        iree_hal_streaming_device_get_or_create_primary_context(
            device, &primary_context);
    if (!iree_status_is_ok(status)) {
      result = iree_hip_execution_context_consume_status(status);
    }
  }

  iree_hal_queue_t* primary_queue = NULL;
  if (!context && result == hipSuccess) {
    iree_status_t status =
        iree_hal_streaming_device_select_primary_queue(device, &primary_queue);
    if (!iree_status_is_ok(status)) {
      result = iree_hip_execution_context_consume_status(status);
    }
  }

  hipDevResource sm_resource = {0};
  if (!context && result == hipSuccess) {
    iree_status_t status = iree_hip_execution_resource_create_sm(
        device, iree_hal_queue_family(primary_queue),
        (iree_hal_queue_execution_resource_list_t){0},
        hipDevSmResourceGroupDefault, &sm_resource);
    if (!iree_status_is_ok(status)) {
      result = iree_hip_execution_context_consume_status(status);
    }
  }

  unsigned long long context_id = 0;
  if (!context && result == hipSuccess) {
    iree_status_t status = iree_hip_execution_context_allocate_id(&context_id);
    if (!iree_status_is_ok(status)) {
      result = iree_hip_execution_context_consume_status(status);
    }
  }

  const iree_allocator_t host_allocator = iree_allocator_system();
  hipExecutionCtx_t new_context = NULL;
  if (!context && result == hipSuccess) {
    iree_status_t status = iree_allocator_malloc(
        host_allocator, sizeof(*new_context), (void**)&new_context);
    if (!iree_status_is_ok(status)) {
      result = iree_hip_execution_context_consume_status(status);
    }
  }
  if (new_context) {
    iree_atomic_ref_count_init(&new_context->ref_count);
    iree_slim_mutex_initialize(&new_context->mutex);
    new_context->kind = IREE_HIP_EXECUTION_CONTEXT_KIND_PRIMARY;
    new_context->host_allocator = host_allocator;
    new_context->device = device;
    new_context->device_ordinal = (hipDevice_t)device->ordinal;
    new_context->primary_context = NULL;
    new_context->descriptor = NULL;
    new_context->sm_resource = sm_resource;
    new_context->context_id = context_id;
    new_context->stream_head = NULL;
    new_context->next_live_context = iree_hip_execution_context_registry.head;
    iree_hip_execution_context_registry.head = new_context;
    context = new_context;
  }
  iree_slim_mutex_unlock(&iree_hip_execution_context_registry.mutex);

  if (result == hipSuccess) {
    *out_context = context;
  }
  return result;
}

hipError_t iree_hip_execution_context_create(
    iree_hal_streaming_device_t* device, hipDevResourceDesc_t descriptor_handle,
    hipExecutionCtx_t* out_context) {
  IREE_ASSERT_ARGUMENT(device);
  IREE_ASSERT_ARGUMENT(out_context);

  iree_hip_execution_resource_descriptor_t* retained_descriptor = NULL;
  if (!iree_hip_execution_resource_descriptor_lookup_retain(
          descriptor_handle, &retained_descriptor)) {
    return hipErrorInvalidValue;
  }

  hipError_t result = hipSuccess;
  const uint64_t table_generation =
      iree_hal_streaming_execution_resource_table_generation(
          &device->execution_resource_table);
  if (retained_descriptor->device_ordinal != device->ordinal) {
    result = hipErrorInvalidDevice;
  } else if (retained_descriptor->table_generation != table_generation) {
    result = hipErrorInvalidResourceConfiguration;
  }

  const iree_hal_queue_family_t* queue_family = NULL;
  const iree_hal_streaming_execution_resource_set_t* sm_resource_set = NULL;
  if (result == hipSuccess) {
    queue_family = iree_hal_device_queue_family(
        device->hal_device, retained_descriptor->queue_family_ordinal);
    sm_resource_set = iree_hal_streaming_execution_resource_table_resolve(
        &device->execution_resource_table,
        retained_descriptor->sm_resource_set_id);
    if (!queue_family || !sm_resource_set ||
        sm_resource_set->queue_family_ordinal !=
            retained_descriptor->queue_family_ordinal) {
      result = hipErrorInvalidResourceConfiguration;
    }
  }

  iree_hal_streaming_context_t* primary_context = NULL;
  if (result == hipSuccess) {
    iree_status_t status = iree_hal_streaming_device_retain_primary_context(
        device, &primary_context);
    if (!iree_status_is_ok(status)) {
      result = iree_hip_execution_context_consume_status(status);
    }
  }

  hipDevResource sm_resource = {0};
  if (result == hipSuccess) {
    iree_status_t status = iree_hip_execution_resource_create_sm(
        device, queue_family, sm_resource_set->resources,
        hipDevSmResourceGroupDefault, &sm_resource);
    if (!iree_status_is_ok(status)) {
      result = iree_hip_execution_context_consume_status(status);
    }
  }

  unsigned long long context_id = 0;
  if (result == hipSuccess) {
    iree_status_t status = iree_hip_execution_context_allocate_id(&context_id);
    if (!iree_status_is_ok(status)) {
      result = iree_hip_execution_context_consume_status(status);
    }
  }

  const iree_allocator_t host_allocator = iree_allocator_system();
  hipExecutionCtx_t context = NULL;
  if (result == hipSuccess) {
    iree_status_t status = iree_allocator_malloc(
        host_allocator, sizeof(*context), (void**)&context);
    if (!iree_status_is_ok(status)) {
      result = iree_hip_execution_context_consume_status(status);
    }
  }

  iree_hip_execution_resource_descriptor_t* owned_descriptor = NULL;
  if (result == hipSuccess && !iree_hip_execution_resource_descriptor_take(
                                  descriptor_handle, &owned_descriptor)) {
    result = hipErrorInvalidValue;
  }
  if (result == hipSuccess) {
    iree_atomic_ref_count_init(&context->ref_count);
    iree_slim_mutex_initialize(&context->mutex);
    context->kind = IREE_HIP_EXECUTION_CONTEXT_KIND_RESOURCE_PARTITIONED;
    context->host_allocator = host_allocator;
    context->device = device;
    context->device_ordinal = (hipDevice_t)device->ordinal;
    context->primary_context = primary_context;
    context->descriptor = owned_descriptor;
    context->sm_resource = sm_resource;
    context->context_id = context_id;
    context->stream_head = NULL;
    context->next_live_context = NULL;
    iree_hip_execution_context_publish(context);
    *out_context = context;
    iree_hip_execution_resource_descriptor_release(retained_descriptor);
    return hipSuccess;
  }

  iree_allocator_free(host_allocator, context);
  const hipError_t primary_release_result =
      iree_hip_execution_context_release_primary(device, primary_context);
  if (primary_release_result != hipSuccess) result = primary_release_result;
  iree_hip_execution_resource_descriptor_release(owned_descriptor);
  iree_hip_execution_resource_descriptor_release(retained_descriptor);
  return result;
}

hipError_t iree_hip_execution_context_destroy(hipExecutionCtx_t context) {
  hipExecutionCtx_t owned_context =
      iree_hip_execution_context_take_partitioned(context);
  if (!owned_context) return hipErrorInvalidValue;
  const hipError_t result =
      iree_hip_execution_context_deinitialize(owned_context);
  iree_hip_execution_context_release(owned_context);
  return result;
}

hipError_t iree_hip_execution_context_stream_create(
    hipExecutionCtx_t context_handle, unsigned int flags, int priority,
    hipStream_t* out_stream) {
  IREE_ASSERT_ARGUMENT(out_stream);
  if (flags & ~hipStreamNonBlocking) return hipErrorInvalidValue;

  hipExecutionCtx_t context =
      iree_hip_execution_context_lookup_retain(context_handle);
  if (!context) return hipErrorInvalidValue;

  // HIP uses lower values for higher priority. The advertised binding range is
  // currently [-1, 0], while HAL priorities increase from low to high.
  const int hip_priority = iree_min(iree_max(priority, -1), 0);
  const iree_hal_queue_priority_t queue_priority =
      (iree_hal_queue_priority_t)-hip_priority;

  hipError_t result = hipSuccess;
  iree_hal_streaming_context_t* common_context = NULL;
  iree_hal_queue_t* queue = NULL;
  hipStream_t stream = NULL;

  iree_slim_mutex_lock(&context->mutex);
  iree_hal_streaming_device_t* device = context->device;
  if (!device) {
    result = hipErrorInvalidValue;
  } else if (context->kind == IREE_HIP_EXECUTION_CONTEXT_KIND_PRIMARY) {
    iree_status_t status =
        iree_hal_streaming_device_get_or_create_primary_context(
            device, &common_context);
    if (!iree_status_is_ok(status)) {
      result = iree_hip_execution_context_consume_status(status);
    }
  } else {
    common_context = context->primary_context;
    if (!common_context) result = hipErrorInvalidValue;
  }

  const iree_hal_streaming_execution_resource_set_t* resource_set = NULL;
  if (result == hipSuccess) {
    result = iree_hip_execution_resource_resolve_sm_for_device(
        &context->sm_resource, device, &resource_set);
  }

  const iree_hal_queue_family_t* queue_family = NULL;
  if (result == hipSuccess) {
    queue_family = iree_hal_device_queue_family(
        device->hal_device, resource_set->queue_family_ordinal);
    if (!queue_family) result = hipErrorInvalidResourceConfiguration;
  }

  if (result == hipSuccess) {
    iree_hal_queue_params_t queue_params;
    iree_hal_queue_params_initialize(&queue_params);
    queue_params.priority = queue_priority;
    queue_params.execution_resources = resource_set->resources;
    iree_status_t status = iree_hal_device_acquire_queue(
        device->hal_device, queue_family, &queue_params, &queue);
    if (!iree_status_is_ok(status)) {
      result = iree_hip_execution_context_consume_status(status);
    }
  }

  if (result == hipSuccess) {
    iree_status_t status = iree_hip_stream_create(
        common_context, queue, hipStreamNonBlocking, hip_priority, &stream);
    if (!iree_status_is_ok(status)) {
      result = iree_hip_execution_context_consume_status(status);
    }
  }
  iree_hal_queue_release(queue);

  if (result == hipSuccess) {
    iree_hip_execution_context_retain(context);
    iree_hip_stream_retain(stream);
    iree_slim_mutex_lock(&stream->mutex);
    IREE_ASSERT(stream->execution_context == NULL);
    IREE_ASSERT(stream->next_execution_context_stream == NULL);
    stream->execution_context = context;
    stream->next_execution_context_stream = context->stream_head;
    context->stream_head = stream;
    iree_slim_mutex_unlock(&stream->mutex);
  }
  iree_slim_mutex_unlock(&context->mutex);

  iree_hip_execution_context_release(context);
  if (result == hipSuccess) *out_stream = stream;
  return result;
}

void iree_hip_execution_context_unregister_stream(hipStream_t stream) {
  if (!stream) return;

  // The association itself owns a context reference. Retain it while holding
  // the stream mutex so context teardown cannot clear and release the
  // association between the load and retain.
  iree_slim_mutex_lock(&stream->mutex);
  hipExecutionCtx_t context = stream->execution_context;
  iree_hip_execution_context_retain(context);
  iree_slim_mutex_unlock(&stream->mutex);
  if (!context) return;

  bool was_removed = false;
  iree_slim_mutex_lock(&context->mutex);
  iree_slim_mutex_lock(&stream->mutex);
  if (stream->execution_context == context) {
    hipStream_t* link = &context->stream_head;
    while (*link && *link != stream) {
      link = &(*link)->next_execution_context_stream;
    }
    IREE_ASSERT(*link == stream);
    if (*link == stream) {
      *link = stream->next_execution_context_stream;
      stream->execution_context = NULL;
      stream->next_execution_context_stream = NULL;
      was_removed = true;
    }
  }
  iree_slim_mutex_unlock(&stream->mutex);
  iree_slim_mutex_unlock(&context->mutex);

  if (was_removed) iree_hip_execution_context_release(context);
  if (was_removed) iree_hip_stream_release(stream);
  iree_hip_execution_context_release(context);
}

hipError_t iree_hip_execution_context_get_resource(
    hipExecutionCtx_t context, hipDevResourceType type,
    hipDevResource* out_resource) {
  IREE_ASSERT_ARGUMENT(out_resource);
  if (type != hipDevResourceTypeSm) return hipErrorInvalidResourceType;
  hipExecutionCtx_t retained_context =
      iree_hip_execution_context_lookup_retain(context);
  if (!retained_context) return hipErrorInvalidValue;
  const hipDevResource resource = retained_context->sm_resource;
  iree_hip_execution_context_release(retained_context);
  *out_resource = resource;
  return hipSuccess;
}

hipError_t iree_hip_execution_context_get_device(hipExecutionCtx_t context,
                                                 hipDevice_t* out_device) {
  IREE_ASSERT_ARGUMENT(out_device);
  hipExecutionCtx_t retained_context =
      iree_hip_execution_context_lookup_retain(context);
  if (!retained_context) return hipErrorInvalidValue;
  const hipDevice_t device = retained_context->device_ordinal;
  iree_hip_execution_context_release(retained_context);
  *out_device = device;
  return hipSuccess;
}

hipError_t iree_hip_execution_context_get_id(
    hipExecutionCtx_t context, unsigned long long* out_context_id) {
  IREE_ASSERT_ARGUMENT(out_context_id);
  hipExecutionCtx_t retained_context =
      iree_hip_execution_context_lookup_retain(context);
  if (!retained_context) return hipErrorInvalidValue;
  const unsigned long long context_id = retained_context->context_id;
  iree_hip_execution_context_release(retained_context);
  *out_context_id = context_id;
  return hipSuccess;
}

static hipError_t iree_hip_execution_context_reset_matching(
    bool reset_all, hipDevice_t device) {
  iree_call_once(&iree_hip_execution_context_registry_once,
                 iree_hip_execution_context_registry_initialize);

  hipExecutionCtx_t reset_head = NULL;
  iree_slim_mutex_lock(&iree_hip_execution_context_registry.mutex);
  hipExecutionCtx_t* link = &iree_hip_execution_context_registry.head;
  while (*link) {
    hipExecutionCtx_t context = *link;
    if (!reset_all && context->device_ordinal != device) {
      link = &context->next_live_context;
      continue;
    }
    *link = context->next_live_context;
    context->next_live_context = reset_head;
    reset_head = context;
  }
  iree_slim_mutex_unlock(&iree_hip_execution_context_registry.mutex);

  hipError_t result = hipSuccess;
  while (reset_head) {
    hipExecutionCtx_t context = reset_head;
    reset_head = context->next_live_context;
    context->next_live_context = NULL;
    const hipError_t deinitialize_result =
        iree_hip_execution_context_deinitialize(context);
    if (result == hipSuccess) result = deinitialize_result;
    iree_hip_execution_context_release(context);
  }
  return result;
}

hipError_t iree_hip_execution_context_reset_device(hipDevice_t device) {
  return iree_hip_execution_context_reset_matching(/*reset_all=*/false, device);
}

hipError_t iree_hip_execution_context_reset_all(void) {
  return iree_hip_execution_context_reset_matching(/*reset_all=*/true, 0);
}
