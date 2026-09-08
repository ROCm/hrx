// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef IREE_HAL_DRIVERS_AMDGPU_DEVICE_GRID_SYNC_H_
#define IREE_HAL_DRIVERS_AMDGPU_DEVICE_GRID_SYNC_H_

#include "iree/hal/drivers/amdgpu/device/dispatch.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

//===----------------------------------------------------------------------===//
// Grid synchronization kernel ABI
//===----------------------------------------------------------------------===//

// Kernel arguments for initializing the hardware GWS barrier resource.
typedef struct iree_hal_amdgpu_device_grid_sync_gws_initialize_kernargs_t {
  // Number of workgroups participating in the following dispatch, minus one.
  uint32_t workgroup_count_minus_one;
} iree_hal_amdgpu_device_grid_sync_gws_initialize_kernargs_t;
IREE_AMDGPU_STATIC_ASSERT(
    sizeof(iree_hal_amdgpu_device_grid_sync_gws_initialize_kernargs_t) == 4,
    "GWS initialize kernargs must match the kernel ABI");

#define IREE_HAL_AMDGPU_DEVICE_GRID_SYNC_GWS_INITIALIZE_KERNARG_SIZE \
  sizeof(iree_hal_amdgpu_device_grid_sync_gws_initialize_kernargs_t)
#define IREE_HAL_AMDGPU_DEVICE_GRID_SYNC_GWS_INITIALIZE_KERNARG_ALIGNMENT \
  IREE_AMDGPU_ALIGNOF(                                                    \
      iree_hal_amdgpu_device_grid_sync_gws_initialize_kernargs_t)

#if !defined(IREE_AMDGPU_TARGET_DEVICE)

// Populates a one-workitem dispatch that initializes GWS resource zero for a
// following cooperative dispatch. The caller owns packet header commit,
// completion-signal assignment, and doorbell signaling.
//
// |workgroup_count| must be in [1, UINT32_MAX]. The target cooperative dispatch
// must not begin until this dispatch has completed.
void iree_hal_amdgpu_device_grid_sync_gws_initialize_emplace(
    const iree_hal_amdgpu_device_kernel_args_t* IREE_AMDGPU_RESTRICT
        initialize_kernel_args,
    uint32_t workgroup_count,
    iree_hsa_kernel_dispatch_packet_t* IREE_AMDGPU_RESTRICT dispatch_packet,
    void* IREE_AMDGPU_RESTRICT kernarg_ptr);

#endif  // !IREE_AMDGPU_TARGET_DEVICE

#if defined(IREE_AMDGPU_TARGET_DEVICE)

// Device builtin that initializes GWS resource zero for a cooperative grid.
// Launched as a single work-item dispatch immediately before the grid.
IREE_AMDGPU_ATTRIBUTE_KERNEL void
iree_hal_amdgpu_device_grid_sync_gws_initialize(
    uint32_t workgroup_count_minus_one);

#endif  // IREE_AMDGPU_TARGET_DEVICE

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // IREE_HAL_DRIVERS_AMDGPU_DEVICE_GRID_SYNC_H_
