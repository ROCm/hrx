// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef AMDF_SRC_XDNA_UMD_DRM_DEVICE_H_
#define AMDF_SRC_XDNA_UMD_DRM_DEVICE_H_

#include "libamdf/src/atomics.h"
#include "libamdf/src/platform/linux/release_list.h"
#include "libamdf/src/xdna/umd/command.h"
#include "libamdf/src/xdna/umd/device.h"
#include "libamdf/src/xdna/umd/drm/buffer.h"

// One independent accel file, context, and firmware heap.
struct amdf_xdna_umd_device_t {
  // Endpoint-owned record if construction rollback cannot finish.
  amdf_linux_release_t failed_construction;
  // Fresh open file description owning all native handle namespaces.
  int descriptor;
  // Hardware context, or AMDXDNA_INVALID_CTX_HANDLE after destruction.
  uint32_t context;
  // Context's user-owned DRM timeline sync object, or zero after destruction.
  uint32_t completion_syncobj;
  // Native host page size established during construction.
  size_t page_size;
  // Qualified CLFLUSH cache-line length in bytes.
  uint32_t cache_line_size;
  // Firmware-addressable device heap with its persistent aligned host mapping.
  amdf_linux_xdna_buffer_t heap;
  // Context-lifetime transaction interpreter PDI, allocated inside the heap.
  amdf_linux_xdna_buffer_t bootstrap;
  // Device-owned interpreter admission command, retained through HWCTX
  // teardown.
  amdf_xdna_umd_command_t* bootstrap_command;
  // Exclusive lease for one public queue and its cold bootstrap admission.
  amdf_atomic_uint32_t queue_leased;
  // First observed terminal firmware or bootstrap-admission failure.
  amdf_atomic_uint64_t terminal_status;
  // Last native sequence assigned under the exclusive queue lease.
  uint64_t last_native_sequence;
  // Children retained when an unpublished native allocation cannot be released.
  amdf_linux_release_list_t failed_children;
};

#endif  // AMDF_SRC_XDNA_UMD_DRM_DEVICE_H_
