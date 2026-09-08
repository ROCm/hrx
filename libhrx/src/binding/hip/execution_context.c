// Copyright 2026 The HRX Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "binding/hip/execution_context.h"

#include "binding/hip/execution_resource.h"
#include "binding/hip/execution_resource_descriptor.h"
#include "common/internal.h"
#include "iree/base/threading/call_once.h"

// A HIP execution context is a binding-private scheduling domain layered over
// the device primary streaming context. Hardware queues are realized lazily by
// streams; this control-plane object does not itself schedule work.
struct ihipExecutionCtx_t {
  // Reference count including the ownership represented by the public handle.
  iree_atomic_ref_count_t ref_count;

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

static void iree_hip_execution_context_release(hipExecutionCtx_t context) {
  if (!context || iree_atomic_ref_count_dec(&context->ref_count) != 1) return;
  IREE_ASSERT(context->device == NULL);
  IREE_ASSERT(context->primary_context == NULL);
  iree_hip_execution_resource_descriptor_release(context->descriptor);
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
  iree_hal_streaming_device_t* device = context->device;
  iree_hal_streaming_context_t* primary_context = context->primary_context;
  context->device = NULL;
  context->primary_context = NULL;
  return iree_hip_execution_context_release_primary(device, primary_context);
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

static hipExecutionCtx_t iree_hip_execution_context_take(
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
  if (*link) {
    owned_context = *link;
    *link = owned_context->next_live_context;
    owned_context->next_live_context = NULL;
  }
  iree_slim_mutex_unlock(&iree_hip_execution_context_registry.mutex);
  return owned_context;
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
    context->host_allocator = host_allocator;
    context->device = device;
    context->device_ordinal = (hipDevice_t)device->ordinal;
    context->primary_context = primary_context;
    context->descriptor = owned_descriptor;
    context->sm_resource = sm_resource;
    context->context_id = context_id;
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
  hipExecutionCtx_t owned_context = iree_hip_execution_context_take(context);
  if (!owned_context) return hipErrorInvalidValue;
  const hipError_t result =
      iree_hip_execution_context_deinitialize(owned_context);
  iree_hip_execution_context_release(owned_context);
  return result;
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
