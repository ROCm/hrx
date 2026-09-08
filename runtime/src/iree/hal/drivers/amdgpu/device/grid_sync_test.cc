// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/hal/drivers/amdgpu/device/grid_sync.h"

#include "iree/testing/gtest.h"

namespace iree::hal::amdgpu {
namespace {

TEST(GridSyncTest, GwsInitializeEncodesParticipatingWorkgroups) {
  iree_hal_amdgpu_device_kernel_args_t kernel_args = {};
  kernel_args.kernel_object = 0x12345678u;
  kernel_args.setup = 1;
  kernel_args.workgroup_size[0] = 1;
  kernel_args.workgroup_size[1] = 1;
  kernel_args.workgroup_size[2] = 1;
  kernel_args.kernarg_size =
      IREE_HAL_AMDGPU_DEVICE_GRID_SYNC_GWS_INITIALIZE_KERNARG_SIZE;
  kernel_args.kernarg_alignment =
      IREE_HAL_AMDGPU_DEVICE_GRID_SYNC_GWS_INITIALIZE_KERNARG_ALIGNMENT;
  iree_hsa_kernel_dispatch_packet_t packet = {};
  iree_hal_amdgpu_device_grid_sync_gws_initialize_kernargs_t kernargs = {};

  iree_hal_amdgpu_device_grid_sync_gws_initialize_emplace(
      &kernel_args, /*workgroup_count=*/257, &packet, &kernargs);

  EXPECT_EQ(kernargs.workgroup_count_minus_one, 256u);
  EXPECT_EQ(packet.workgroup_size[0], 1u);
  EXPECT_EQ(packet.workgroup_size[1], 1u);
  EXPECT_EQ(packet.workgroup_size[2], 1u);
  EXPECT_EQ(packet.grid_size[0], 1u);
  EXPECT_EQ(packet.grid_size[1], 1u);
  EXPECT_EQ(packet.grid_size[2], 1u);
  EXPECT_EQ(packet.kernel_object, kernel_args.kernel_object);
  EXPECT_EQ(packet.kernarg_address, &kernargs);
}

}  // namespace
}  // namespace iree::hal::amdgpu
