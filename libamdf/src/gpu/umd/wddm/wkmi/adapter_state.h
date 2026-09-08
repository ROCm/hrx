// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef AMDF_SRC_GPU_UMD_WDDM_WKMI_ADAPTER_STATE_H_
#define AMDF_SRC_GPU_UMD_WDDM_WKMI_ADAPTER_STATE_H_

#include <cstdlib>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif  // WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif  // NOMINMAX
#include <windows.h>

#include "libamdf/src/gpu/umd/wddm/wkmi/bridge_api.h"
#include "wkmi.h"

// Parsed adapter state and bridge-owned cleanup bookkeeping.
struct amdf_wkmi_bridge_gpu_adapter_t {
  // Private normalized adapter properties owned by WKMI.
  Wkmi::DeviceInfo device_info = {};
  // Protects queue ownership and deferred construction rollback.
  SRWLOCK queue_lock = SRWLOCK_INIT;
  // First partially constructed queue awaiting native cleanup.
  amdf_wkmi_bridge_gpu_kernel_queue_t* deferred_queue_head = nullptr;
  // Number of live queues borrowing this adapter.
  uint32_t live_queue_count = 0;

  ~amdf_wkmi_bridge_gpu_adapter_t() { std::free(device_info.adapter_info); }
};

#endif  // AMDF_SRC_GPU_UMD_WDDM_WKMI_ADAPTER_STATE_H_
