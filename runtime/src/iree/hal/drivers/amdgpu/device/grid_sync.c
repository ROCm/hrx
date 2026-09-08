// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/hal/drivers/amdgpu/device/grid_sync.h"

#if !defined(IREE_AMDGPU_TARGET_DEVICE)

void iree_hal_amdgpu_device_grid_sync_gws_initialize_emplace(
    const iree_hal_amdgpu_device_kernel_args_t* IREE_AMDGPU_RESTRICT
        initialize_kernel_args,
    uint32_t workgroup_count,
    iree_hsa_kernel_dispatch_packet_t* IREE_AMDGPU_RESTRICT dispatch_packet,
    void* IREE_AMDGPU_RESTRICT kernarg_ptr) {
  iree_hal_amdgpu_device_grid_sync_gws_initialize_kernargs_t*
      IREE_AMDGPU_RESTRICT kernargs =
          (iree_hal_amdgpu_device_grid_sync_gws_initialize_kernargs_t*)
              kernarg_ptr;
  kernargs->workgroup_count_minus_one = workgroup_count - 1;

  const uint32_t initialize_workgroup_count[3] = {1, 1, 1};
  iree_hal_amdgpu_device_dispatch_emplace_packet(
      initialize_kernel_args, initialize_workgroup_count,
      /*dynamic_workgroup_local_memory=*/0, dispatch_packet, kernarg_ptr);
}

#endif  // !IREE_AMDGPU_TARGET_DEVICE

#if defined(IREE_AMDGPU_TARGET_DEVICE)

// Hardware GWS is used only on the GFX9 and GFX10 architectures where ROCm's
// cooperative-groups device library uses the same instruction. The target
// feature lets LLVM select the instruction without applying GWS requirements
// to every builtin in the linked code object.
//
// Newer architectures use a memory-backed grid barrier. Their code objects
// retain this inert kernel because builtin kernel descriptors are resolved as
// one eager table, but the host must never submit it.
#if defined(__GFX9__) || defined(__GFX10__)
#define IREE_HAL_AMDGPU_DEVICE_GRID_SYNC_GWS_ATTRIBUTE \
  __attribute__((target("gws")))
#else
#define IREE_HAL_AMDGPU_DEVICE_GRID_SYNC_GWS_ATTRIBUTE
#endif  // __GFX9__ || __GFX10__

IREE_AMDGPU_ATTRIBUTE_KERNEL IREE_HAL_AMDGPU_DEVICE_GRID_SYNC_GWS_ATTRIBUTE void
iree_hal_amdgpu_device_grid_sync_gws_initialize(
    uint32_t workgroup_count_minus_one) {
#if defined(__GFX9__) || defined(__GFX10__)
  __builtin_amdgcn_ds_gws_init(workgroup_count_minus_one, 0);
#else
  (void)workgroup_count_minus_one;
#endif  // __GFX9__ || __GFX10__
}

#undef IREE_HAL_AMDGPU_DEVICE_GRID_SYNC_GWS_ATTRIBUTE

#endif  // IREE_AMDGPU_TARGET_DEVICE
