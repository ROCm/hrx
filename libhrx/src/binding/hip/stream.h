// Copyright 2026 The HRX Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef LIBHRX_SRC_BINDING_HIP_STREAM_H_
#define LIBHRX_SRC_BINDING_HIP_STREAM_H_

#include "binding/hip/api.h"
#include "iree/base/api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct iree_hal_streaming_context_t iree_hal_streaming_context_t;
typedef struct iree_hal_streaming_stream_t iree_hal_streaming_stream_t;

// Publishes |stream| as an opaque HIP stream handle. On success the handle
// assumes ownership of the caller's stream reference and |out_handle| receives
// its public identity. On failure ownership remains with the caller and
// |out_handle| is unchanged.
iree_status_t iree_hip_stream_publish(iree_hal_streaming_stream_t* stream,
                                      iree_allocator_t host_allocator,
                                      hipStream_t* out_handle);

// Looks up and retains a live explicit stream handle. |out_handle| is unchanged
// when the handle is not registered.
bool iree_hip_stream_lookup_retain(hipStream_t handle, hipStream_t* out_handle);

// Removes a live explicit stream handle and transfers its public ownership to
// the caller. |out_handle| is unchanged when the handle is not registered.
bool iree_hip_stream_take(hipStream_t handle, hipStream_t* out_handle);

// Retains the common stream and its context while |handle| remains attached.
// Both outputs are unchanged when the stream has been detached by context
// destruction.
bool iree_hip_stream_retain_attached(
    hipStream_t handle, iree_hal_streaming_stream_t** out_stream,
    iree_hal_streaming_context_t** out_context);

// Releases a retained or transferred HIP stream handle.
void iree_hip_stream_release(hipStream_t handle);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // LIBHRX_SRC_BINDING_HIP_STREAM_H_
