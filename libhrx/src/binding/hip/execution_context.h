// Copyright 2026 The HRX Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef LIBHRX_SRC_BINDING_HIP_EXECUTION_CONTEXT_H_
#define LIBHRX_SRC_BINDING_HIP_EXECUTION_CONTEXT_H_

#include "binding/hip/api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct iree_hal_streaming_device_t iree_hal_streaming_device_t;
typedef struct iree_hal_streaming_event_t iree_hal_streaming_event_t;

// Returns the process-managed primary execution context for |device|, creating
// its handle for the current device incarnation if necessary. |out_context| is
// unchanged on failure.
hipError_t iree_hip_execution_context_primary(
    iree_hal_streaming_device_t* device, hipExecutionCtx_t* out_context);

// Creates a resource-partitioned execution context. On success the context
// consumes |descriptor| and retains the device primary streaming context, but
// does not acquire a hardware queue until a stream requests one. |out_context|
// is unchanged on failure.
hipError_t iree_hip_execution_context_create(
    iree_hal_streaming_device_t* device, hipDevResourceDesc_t descriptor,
    hipExecutionCtx_t* out_context);

// Invalidates and releases a resource-partitioned execution context handle.
hipError_t iree_hip_execution_context_destroy(hipExecutionCtx_t context);

// Creates a non-blocking stream that executes on the exact resources owned by
// |context|. A real HAL queue is acquired on demand with the clamped HIP
// |priority|. |out_stream| is unchanged on failure.
hipError_t iree_hip_execution_context_stream_create(hipExecutionCtx_t context,
                                                    unsigned int flags,
                                                    int priority,
                                                    hipStream_t* out_stream);

// Removes |stream| from its execution context, if any, and releases the
// membership reference. The common stream remains attached until its caller
// explicitly detaches it.
void iree_hip_execution_context_unregister_stream(hipStream_t stream);

// Returns the canonical resource of |type| owned by |context|.
// |out_resource| is unchanged on failure.
hipError_t iree_hip_execution_context_get_resource(
    hipExecutionCtx_t context, hipDevResourceType type,
    hipDevResource* out_resource);

// Returns the device ordinal associated with |context|.
// |out_device| is unchanged on failure.
hipError_t iree_hip_execution_context_get_device(hipExecutionCtx_t context,
                                                 hipDevice_t* out_device);

// Returns the process-unique identifier associated with |context|.
// |out_context_id| is unchanged on failure.
hipError_t iree_hip_execution_context_get_id(
    hipExecutionCtx_t context, unsigned long long* out_context_id);

// Records |event| after all work submitted to |context| before the call. A
// primary execution context includes resource-partitioned streams on its
// device. The event must belong to the same primary streaming context.
hipError_t iree_hip_execution_context_record_event(
    hipExecutionCtx_t context, iree_hal_streaming_event_t* event);

// Orders all future work submitted to |context| after the point recorded on
// |event| without blocking the caller. Events from other contexts and devices
// are accepted when their semaphore is compatible with the target queues.
hipError_t iree_hip_execution_context_wait_event(
    hipExecutionCtx_t context, iree_hal_streaming_event_t* event);

// Blocks until all work submitted to |context| before the call completes. A
// primary execution context includes resource-partitioned streams on its
// device.
hipError_t iree_hip_execution_context_synchronize(hipExecutionCtx_t context);

// Invalidates every resource-partitioned execution context on |device| before
// releasing its primary-context ownership.
hipError_t iree_hip_execution_context_reset_device(hipDevice_t device);

// Invalidates every resource-partitioned execution context in the process.
hipError_t iree_hip_execution_context_reset_all(void);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // LIBHRX_SRC_BINDING_HIP_EXECUTION_CONTEXT_H_
