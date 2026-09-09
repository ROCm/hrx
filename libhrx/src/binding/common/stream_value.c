// Copyright 2026 The HRX Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "common/stream_value.h"

#include "common/internal.h"
#include "common/stream.h"
#include "iree/base/internal/math.h"

bool iree_hal_streaming_queue_family_supports_value_waits(
    const iree_hal_queue_family_spec_t* family_spec) {
  if (!family_spec ||
      iree_math_count_ones_u64(family_spec->physical_device_affinity) != 1 ||
      !iree_all_bits_set(family_spec->role_flags,
                         IREE_HAL_QUEUE_FAMILY_ROLE_FLAG_HOST_CALL |
                             IREE_HAL_QUEUE_FAMILY_ROLE_FLAG_ATOMIC) ||
      !iree_any_bit_set(family_spec->flags,
                        IREE_HAL_QUEUE_FAMILY_SPEC_FLAG_DYNAMIC_ACQUISITION)) {
    return false;
  }

  const iree_hal_atomic_capabilities_t* capabilities =
      &family_spec->zero_compute_atomic_capabilities;
  return iree_all_bits_set(capabilities->operations.device_scope_32,
                           IREE_HAL_ATOMIC_OPERATION_FLAG_WAIT) &&
         iree_all_bits_set(capabilities->operations.device_scope_64,
                           IREE_HAL_ATOMIC_OPERATION_FLAG_WAIT) &&
         iree_all_bits_set(capabilities->operations.system_scope_32,
                           IREE_HAL_ATOMIC_OPERATION_FLAG_WAIT) &&
         iree_all_bits_set(capabilities->operations.system_scope_64,
                           IREE_HAL_ATOMIC_OPERATION_FLAG_WAIT) &&
         iree_all_bits_set(capabilities->wait_conditions.device_scope_32,
                           IREE_HAL_ATOMIC_WAIT_CONDITION_FLAGS_ALL) &&
         iree_all_bits_set(capabilities->wait_conditions.device_scope_64,
                           IREE_HAL_ATOMIC_WAIT_CONDITION_FLAGS_ALL) &&
         iree_all_bits_set(capabilities->wait_conditions.system_scope_32,
                           IREE_HAL_ATOMIC_WAIT_CONDITION_FLAGS_ALL) &&
         iree_all_bits_set(capabilities->wait_conditions.system_scope_64,
                           IREE_HAL_ATOMIC_WAIT_CONDITION_FLAGS_ALL);
}

// Returns a context-owned queue that cannot be blocked behind work submitted
// to |operation_queue|. Queue acquisition is a cold first-use path; subsequent
// waits only take the context mutex long enough to load the stable queue.
static iree_status_t iree_hal_streaming_acquire_value_wait_queue(
    iree_hal_streaming_context_t* context, iree_hal_queue_t* operation_queue,
    iree_hal_queue_t** out_wait_queue) {
  IREE_ASSERT_ARGUMENT(context);
  IREE_ASSERT_ARGUMENT(operation_queue);
  IREE_ASSERT_ARGUMENT(out_wait_queue);
  *out_wait_queue = NULL;

  const iree_hal_queue_family_t* family =
      iree_hal_queue_family(operation_queue);
  const iree_hal_queue_family_spec_t* family_spec =
      iree_hal_queue_family_spec(family);
  if (!iree_hal_streaming_queue_family_supports_value_waits(family_spec)) {
    return iree_make_status(
        IREE_STATUS_UNIMPLEMENTED,
        "stream family exposes no independent queue supporting value waits");
  }

  iree_slim_mutex_lock(&context->mutex);
  iree_status_t status = iree_ok_status();
  iree_hal_queue_t* wait_queue = context->stream_value_wait_queue;
  if (wait_queue && iree_hal_queue_family(wait_queue) != family) {
    status = iree_make_status(
        IREE_STATUS_FAILED_PRECONDITION,
        "stream value-wait queue belongs to a different queue family");
  }

  if (iree_status_is_ok(status) && !wait_queue) {
    iree_hal_queue_params_t params;
    iree_hal_queue_params_initialize(&params);
    params.priority = iree_hal_queue_priority(operation_queue);
    status = iree_hal_device_acquire_queue(context->device, family, &params,
                                           &wait_queue);
    if (iree_status_is_ok(status) && wait_queue == operation_queue) {
      iree_hal_queue_release(wait_queue);
      wait_queue = NULL;
      status = iree_make_status(
          IREE_STATUS_INTERNAL,
          "dynamic queue acquisition returned the operation queue");
    }
  }

  if (iree_status_is_ok(status)) {
    context->stream_value_wait_queue = wait_queue;
    *out_wait_queue = wait_queue;
  }
  iree_slim_mutex_unlock(&context->mutex);
  return status;
}

typedef enum iree_hal_streaming_atomic_operation_e {
  IREE_HAL_STREAMING_ATOMIC_OPERATION_WAIT = 0,
  IREE_HAL_STREAMING_ATOMIC_OPERATION_STORE = 1,
  IREE_HAL_STREAMING_ATOMIC_OPERATION_UPDATE = 2,
} iree_hal_streaming_atomic_operation_t;

typedef union iree_hal_streaming_atomic_params_u {
  iree_hal_atomic_wait_params_t wait;
  iree_hal_atomic_store_params_t store;
  iree_hal_atomic_rmw_params_t update;
} iree_hal_streaming_atomic_params_t;

static iree_status_t iree_hal_streaming_value_wait_gate(
    void* user_data, const uint64_t args[4],
    iree_hal_host_call_context_t* call_context) {
  (void)user_data;
  (void)args;
  (void)call_context;
  return iree_ok_status();
}

// Bridges completion of a queue-blocking memory wait through a host signal.
// The dedicated wait queue may safely remain occupied by the unresolved
// predicate. The host-issued gate keeps following work deferred in the HAL
// instead of placing a hardware wait on the operation queue that a future
// producer may need. NON_BLOCKING completes the gate when the HAL issues this
// no-op callback, which cannot happen until the preceding memory wait finishes.
static iree_status_t iree_hal_streaming_queue_value_wait_gate_locked(
    iree_hal_streaming_stream_t* stream, iree_hal_queue_t* wait_queue) {
  uint64_t wait_value = 0;
  uint64_t signal_value = 0;
  IREE_RETURN_IF_ERROR(iree_hal_streaming_stream_reserve_next_value_locked(
      stream, &wait_value, &signal_value));

  const iree_hal_semaphore_list_t wait_semaphores = {
      .count = 1,
      .semaphores = &stream->timeline_semaphore,
      .payload_values = &wait_value,
  };
  const iree_hal_semaphore_list_t signal_semaphores = {
      .count = 1,
      .semaphores = &stream->timeline_semaphore,
      .payload_values = &signal_value,
  };
  const uint64_t args[4] = {0};
  const iree_hal_host_call_t call = iree_hal_make_host_call(
      iree_hal_streaming_value_wait_gate, /*user_data=*/NULL);
  iree_status_t status = iree_hal_queue_host_call(
      wait_queue, wait_semaphores, signal_semaphores, call, args,
      IREE_HAL_HOST_CALL_FLAG_NON_BLOCKING | IREE_HAL_HOST_CALL_FLAG_RELAXED);
  if (iree_status_is_ok(status)) stream->pending_value = signal_value;
  return status;
}

static iree_status_t iree_hal_streaming_queue_atomic(
    iree_hal_streaming_stream_t* stream,
    iree_hal_streaming_atomic_operation_t operation,
    iree_hal_buffer_t* target_buffer, iree_device_size_t target_offset,
    iree_hal_streaming_atomic_params_t params) {
  IREE_ASSERT_ARGUMENT(stream);
  IREE_ASSERT_ARGUMENT(target_buffer);
  IREE_TRACE_ZONE_BEGIN(z0);

  // Captured atomics need graph nodes that retain their targets and reissue the
  // operation on every launch. Never submit the operation outside the graph
  // while capture is active.
  if (stream->capture_status == IREE_HAL_STREAMING_CAPTURE_STATUS_ACTIVE) {
    IREE_TRACE_ZONE_END(z0);
    return iree_make_status(
        IREE_STATUS_UNIMPLEMENTED,
        "queue-ordered atomics are not supported during stream capture");
  }
  if (IREE_UNLIKELY(!stream->context || !stream->queue)) {
    IREE_TRACE_ZONE_END(z0);
    return iree_make_status(IREE_STATUS_FAILED_PRECONDITION,
                            "stream execution context has been destroyed");
  }

  iree_hal_queue_t* operation_queue = stream->queue;
  if (operation == IREE_HAL_STREAMING_ATOMIC_OPERATION_WAIT) {
    IREE_RETURN_AND_END_ZONE_IF_ERROR(
        z0, iree_hal_streaming_acquire_value_wait_queue(
                stream->context, stream->queue, &operation_queue));
  }
  if (!operation_queue) {
    IREE_TRACE_ZONE_END(z0);
    return iree_make_status(IREE_STATUS_FAILED_PRECONDITION,
                            "stream operation queue is unavailable");
  }
  if (iree_hal_queue_family(operation_queue) !=
      iree_hal_queue_family(stream->queue)) {
    IREE_TRACE_ZONE_END(z0);
    return iree_make_status(IREE_STATUS_FAILED_PRECONDITION,
                            "stream queues belong to different families");
  }
  if (operation == IREE_HAL_STREAMING_ATOMIC_OPERATION_WAIT &&
      operation_queue == stream->queue) {
    IREE_TRACE_ZONE_END(z0);
    return iree_make_status(
        IREE_STATUS_UNIMPLEMENTED,
        "stream family exposes no independent queue supporting value waits");
  }

  IREE_RETURN_AND_END_ZONE_IF_ERROR(z0,
                                    iree_hal_streaming_stream_flush(stream));

  iree_slim_mutex_lock(&stream->mutex);
  uint64_t wait_value = 0;
  uint64_t signal_value = 0;
  iree_status_t status = iree_hal_streaming_stream_reserve_next_value_locked(
      stream, &wait_value, &signal_value);
  if (iree_status_is_ok(status) &&
      operation == IREE_HAL_STREAMING_ATOMIC_OPERATION_WAIT &&
      signal_value == IREE_HAL_SEMAPHORE_MAX_VALUE) {
    status = iree_make_status(
        IREE_STATUS_RESOURCE_EXHAUSTED,
        "stream timeline has no value available for a memory-wait gate");
  }
  if (iree_status_is_ok(status)) {
    const iree_hal_semaphore_list_t wait_semaphores = {
        .count = wait_value > 0 ? 1 : 0,
        .semaphores = &stream->timeline_semaphore,
        .payload_values = &wait_value,
    };
    const iree_hal_semaphore_list_t signal_semaphores = {
        .count = 1,
        .semaphores = &stream->timeline_semaphore,
        .payload_values = &signal_value,
    };
    switch (operation) {
      case IREE_HAL_STREAMING_ATOMIC_OPERATION_WAIT:
        status = iree_hal_queue_atomic_wait(operation_queue, wait_semaphores,
                                            signal_semaphores, target_buffer,
                                            target_offset, params.wait);
        break;
      case IREE_HAL_STREAMING_ATOMIC_OPERATION_STORE:
        status = iree_hal_queue_atomic_store(operation_queue, wait_semaphores,
                                             signal_semaphores, target_buffer,
                                             target_offset, params.store);
        break;
      case IREE_HAL_STREAMING_ATOMIC_OPERATION_UPDATE:
        status = iree_hal_queue_atomic_rmw(operation_queue, wait_semaphores,
                                           signal_semaphores, target_buffer,
                                           target_offset, params.update);
        break;
      default:
        IREE_ASSERT_UNREACHABLE("atomic operation must be valid");
        status = iree_make_status(IREE_STATUS_INTERNAL,
                                  "invalid streaming atomic operation");
        break;
    }
    if (iree_status_is_ok(status)) {
      stream->pending_value = signal_value;
      if (operation == IREE_HAL_STREAMING_ATOMIC_OPERATION_WAIT) {
        status = iree_hal_streaming_queue_value_wait_gate_locked(
            stream, operation_queue);
        if (!iree_status_is_ok(status)) {
          // The device wait has already been accepted. Failing the timeline
          // prevents later work from using the unbridged signal and occupying
          // the operation queue that may be needed by the producer.
          iree_hal_semaphore_fail(stream->timeline_semaphore,
                                  iree_status_clone(status));
        }
      }
    }
  }
  iree_slim_mutex_unlock(&stream->mutex);

  IREE_TRACE_ZONE_END(z0);
  return status;
}

iree_status_t iree_hal_streaming_queue_wait_value(
    iree_hal_streaming_stream_t* stream, iree_hal_buffer_t* target_buffer,
    iree_device_size_t target_offset, iree_hal_atomic_wait_params_t params) {
  const iree_hal_streaming_atomic_params_t operation_params = {
      .wait = params,
  };
  return iree_hal_streaming_queue_atomic(
      stream, IREE_HAL_STREAMING_ATOMIC_OPERATION_WAIT, target_buffer,
      target_offset, operation_params);
}

iree_status_t iree_hal_streaming_queue_store_value(
    iree_hal_streaming_stream_t* stream, iree_hal_buffer_t* target_buffer,
    iree_device_size_t target_offset, iree_hal_atomic_store_params_t params) {
  const iree_hal_streaming_atomic_params_t operation_params = {
      .store = params,
  };
  return iree_hal_streaming_queue_atomic(
      stream, IREE_HAL_STREAMING_ATOMIC_OPERATION_STORE, target_buffer,
      target_offset, operation_params);
}

iree_status_t iree_hal_streaming_queue_update_value(
    iree_hal_streaming_stream_t* stream, iree_hal_buffer_t* target_buffer,
    iree_device_size_t target_offset, iree_hal_atomic_rmw_params_t params) {
  const iree_hal_streaming_atomic_params_t operation_params = {
      .update = params,
  };
  return iree_hal_streaming_queue_atomic(
      stream, IREE_HAL_STREAMING_ATOMIC_OPERATION_UPDATE, target_buffer,
      target_offset, operation_params);
}
