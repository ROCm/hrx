// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef AMDF_SRC_XDNA_UMD_DRM_COMMAND_H_
#define AMDF_SRC_XDNA_UMD_DRM_COMMAND_H_

#include "libamdf/src/platform/linux/release_list.h"
#include "libamdf/src/xdna/target/npu5/ert_packet.h"
#include "libamdf/src/xdna/umd/command.h"
#include "libamdf/src/xdna/umd/drm/buffer.h"

// One prepared transaction and its immutable native residency set.
struct amdf_xdna_umd_command_t {
  // Device-owned record if construction rollback cannot finish.
  amdf_linux_release_t failed_construction;
  // Device owning the GEM handle namespace and borrowed heap mapping.
  amdf_xdna_umd_device_t* device;
  // Composed ARRAY/CONTROL transaction in firmware-addressable heap storage.
  amdf_linux_xdna_buffer_t instruction;
  // Mapped command buffer; only its ERT state changes during execution.
  amdf_linux_xdna_buffer_t packet;
  // Number of unique GEM handles in the native residency set.
  uint32_t argument_count;
  // Heap, PDI, instruction, command, and unique bound SHARE allocations.
  uint32_t arguments[4 + AMDF_XDNA_NPU5_MAXIMUM_BINDING_COUNT];
};

#endif  // AMDF_SRC_XDNA_UMD_DRM_COMMAND_H_
