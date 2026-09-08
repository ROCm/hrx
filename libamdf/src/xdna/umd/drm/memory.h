// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef AMDF_SRC_XDNA_UMD_DRM_MEMORY_H_
#define AMDF_SRC_XDNA_UMD_DRM_MEMORY_H_

#include "libamdf/src/platform/linux/release_list.h"
#include "libamdf/src/xdna/umd/drm/buffer.h"
#include "libamdf/src/xdna/umd/memory.h"

struct amdf_xdna_umd_memory_t {
  // Device-owned record if construction rollback cannot finish.
  amdf_linux_release_t failed_construction;
  // Device borrowed until the native attachment has been released.
  amdf_xdna_umd_device_t* device;
  // SHARE allocation with its persistent SVA mapping.
  amdf_linux_xdna_buffer_t buffer;
};

#endif  // AMDF_SRC_XDNA_UMD_DRM_MEMORY_H_
