// Copyright 2026 The HRX Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "binding/hip/stream.h"

#include "binding/hip/handle_registry.h"
#include "common/internal.h"
#include "common/stream.h"
#include "iree/base/threading/call_once.h"

// Binding-private ownership and lifetime state behind a public HIP stream
// handle. The common stream remains the execution engine while this object
// provides the stable API identity needed to detach it independently.
struct hipStream_st {
  // Reference count including the live public-handle ownership.
  iree_atomic_ref_count_t ref_count;

  // Serializes access to the potentially detached common stream.
  iree_slim_mutex_t mutex;

  // Common stream owning the exact HAL queue, or NULL after detachment.
  iree_hal_streaming_stream_t* stream;

  // Host allocator owning this handle allocation.
  iree_allocator_t host_allocator;
};

static iree_once_flag iree_hip_stream_registry_once = IREE_ONCE_FLAG_INIT;
static iree_hip_handle_registry_t iree_hip_stream_registry;

static void iree_hip_stream_registry_initialize(void) {
  iree_hip_handle_registry_initialize(&iree_hip_stream_registry);
}

static void iree_hip_stream_handle_retain(uintptr_t handle) {
  hipStream_t stream = (hipStream_t)handle;
  iree_atomic_ref_count_inc(&stream->ref_count);
}

iree_status_t iree_hip_stream_publish(iree_hal_streaming_stream_t* stream,
                                      iree_allocator_t host_allocator,
                                      hipStream_t* out_handle) {
  IREE_ASSERT_ARGUMENT(stream);
  IREE_ASSERT_ARGUMENT(out_handle);

  hipStream_t handle = NULL;
  IREE_RETURN_IF_ERROR(
      iree_allocator_malloc(host_allocator, sizeof(*handle), (void**)&handle));
  iree_atomic_ref_count_init(&handle->ref_count);
  iree_slim_mutex_initialize(&handle->mutex);
  handle->stream = stream;
  handle->host_allocator = host_allocator;

  iree_call_once(&iree_hip_stream_registry_once,
                 iree_hip_stream_registry_initialize);
  iree_status_t status = iree_hip_handle_registry_insert(
      &iree_hip_stream_registry, (uintptr_t)handle);
  if (iree_status_is_ok(status)) {
    *out_handle = handle;
  } else {
    iree_slim_mutex_deinitialize(&handle->mutex);
    iree_allocator_free(host_allocator, handle);
  }
  return status;
}

bool iree_hip_stream_lookup_retain(hipStream_t handle,
                                   hipStream_t* out_handle) {
  IREE_ASSERT_ARGUMENT(out_handle);
  if (!handle || handle == hipStreamLegacy || handle == hipStreamPerThread) {
    return false;
  }

  iree_call_once(&iree_hip_stream_registry_once,
                 iree_hip_stream_registry_initialize);
  if (!iree_hip_handle_registry_lookup_retain(&iree_hip_stream_registry,
                                              (uintptr_t)handle,
                                              iree_hip_stream_handle_retain)) {
    return false;
  }
  *out_handle = handle;
  return true;
}

bool iree_hip_stream_take(hipStream_t handle, hipStream_t* out_handle) {
  IREE_ASSERT_ARGUMENT(out_handle);
  if (!handle || handle == hipStreamLegacy || handle == hipStreamPerThread) {
    return false;
  }

  iree_call_once(&iree_hip_stream_registry_once,
                 iree_hip_stream_registry_initialize);
  if (!iree_hip_handle_registry_remove(&iree_hip_stream_registry,
                                       (uintptr_t)handle)) {
    return false;
  }
  *out_handle = handle;
  return true;
}

bool iree_hip_stream_retain_attached(
    hipStream_t handle, iree_hal_streaming_stream_t** out_stream,
    iree_hal_streaming_context_t** out_context) {
  IREE_ASSERT_ARGUMENT(handle);
  IREE_ASSERT_ARGUMENT(out_stream);
  IREE_ASSERT_ARGUMENT(out_context);

  iree_hal_streaming_stream_t* stream = NULL;
  iree_hal_streaming_context_t* context = NULL;
  iree_slim_mutex_lock(&handle->mutex);
  stream = handle->stream;
  if (stream) {
    iree_hal_streaming_stream_retain(stream);
    if (!iree_hal_streaming_stream_retain_context(stream, &context)) {
      iree_hal_streaming_stream_release(stream);
      stream = NULL;
    }
  }
  iree_slim_mutex_unlock(&handle->mutex);

  if (!stream) return false;
  *out_stream = stream;
  *out_context = context;
  return true;
}

void iree_hip_stream_release(hipStream_t handle) {
  if (!handle || iree_atomic_ref_count_dec(&handle->ref_count) != 1) return;

  iree_slim_mutex_lock(&handle->mutex);
  iree_hal_streaming_stream_t* stream = handle->stream;
  handle->stream = NULL;
  iree_slim_mutex_unlock(&handle->mutex);

  if (stream) {
    iree_hal_streaming_context_t* context = NULL;
    if (iree_hal_streaming_stream_retain_context(stream, &context)) {
      iree_hal_streaming_context_unregister_stream(context, stream);
    }
    iree_hal_streaming_stream_release(stream);
    iree_hal_streaming_context_release(context);
  }

  iree_slim_mutex_deinitialize(&handle->mutex);
  const iree_allocator_t host_allocator = handle->host_allocator;
  iree_allocator_free(host_allocator, handle);
}
