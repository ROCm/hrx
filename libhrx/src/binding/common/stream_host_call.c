// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "common/internal.h"
#include "common/stream.h"

// Captured HIP callback adapted to the HAL host-call ABI.
typedef struct iree_hal_streaming_host_callback_t {
  // User callback invoked at the stream timeline point.
  void (*fn)(void* user_data);
  // Opaque argument passed to |fn|.
  void* user_data;
} iree_hal_streaming_host_callback_t;

static iree_status_t iree_hal_streaming_host_callback_thunk(
    void* user_data, const uint64_t args[4],
    iree_hal_host_call_context_t* context) {
  iree_hal_streaming_host_callback_t* callback =
      (iree_hal_streaming_host_callback_t*)user_data;
  callback->fn(callback->user_data);
  iree_allocator_free(iree_allocator_system(), callback);
  return iree_ok_status();
}

iree_status_t iree_hal_streaming_queue_host_call(
    iree_hal_streaming_stream_t* stream, iree_hal_host_call_t call,
    const uint64_t args[4], iree_hal_host_call_flags_t flags) {
  IREE_ASSERT_ARGUMENT(stream);
  IREE_ASSERT_ARGUMENT(call.fn);
  IREE_TRACE_ZONE_BEGIN(z0);

  IREE_RETURN_AND_END_ZONE_IF_ERROR(z0,
                                    iree_hal_streaming_stream_flush(stream));

  iree_slim_mutex_lock(&stream->mutex);
  uint64_t wait_value = 0;
  uint64_t signal_value = 0;
  iree_status_t status = iree_hal_streaming_stream_reserve_next_value_locked(
      stream, &wait_value, &signal_value);
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
    status = iree_hal_queue_host_call(stream->queue, wait_semaphores,
                                      signal_semaphores, call, args, flags);
    if (iree_status_is_ok(status)) stream->pending_value = signal_value;
  }
  iree_slim_mutex_unlock(&stream->mutex);

  IREE_TRACE_ZONE_END(z0);
  return status;
}

iree_status_t iree_hal_streaming_launch_host_function(
    iree_hal_streaming_stream_t* stream, void (*fn)(void*), void* user_data) {
  IREE_ASSERT_ARGUMENT(stream);
  IREE_ASSERT_ARGUMENT(fn);
  IREE_TRACE_ZONE_BEGIN(z0);

  if (stream->capture_status == IREE_HAL_STREAMING_CAPTURE_STATUS_ACTIVE) {
    iree_hal_streaming_graph_node_t* node = NULL;
    IREE_RETURN_AND_END_ZONE_IF_ERROR(
        z0, iree_hal_streaming_graph_add_host_call_node(
                stream->capture_graph, stream->capture_dependencies,
                stream->capture_dependency_count, fn, user_data, &node));
    IREE_RETURN_AND_END_ZONE_IF_ERROR(
        z0, iree_hal_streaming_capture_set_last_node(stream, node));
    IREE_TRACE_ZONE_END(z0);
    return iree_ok_status();
  }

  iree_hal_streaming_host_callback_t* callback = NULL;
  IREE_RETURN_AND_END_ZONE_IF_ERROR(
      z0, iree_allocator_malloc(iree_allocator_system(), sizeof(*callback),
                                (void**)&callback));
  callback->fn = fn;
  callback->user_data = user_data;

  const uint64_t args[4] = {0, 0, 0, 0};
  const iree_hal_host_call_t call =
      iree_hal_make_host_call(iree_hal_streaming_host_callback_thunk, callback);
  iree_status_t status = iree_hal_streaming_queue_host_call(
      stream, call, args, IREE_HAL_HOST_CALL_FLAG_NONE);
  if (!iree_status_is_ok(status)) {
    iree_allocator_free(iree_allocator_system(), callback);
  }

  IREE_TRACE_ZONE_END(z0);
  return status;
}
