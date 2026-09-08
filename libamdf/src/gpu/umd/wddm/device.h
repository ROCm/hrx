// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef AMDF_SRC_GPU_UMD_WDDM_DEVICE_H_
#define AMDF_SRC_GPU_UMD_WDDM_DEVICE_H_

#include "libamdf/src/gpu/umd/device.h"
#include "libamdf/src/platform/windows/kmt_api.h"

// Concrete Windows state backing one program-independent GPU device.
struct amdf_gpu_umd_device_t {
  // KMT table borrowed from the endpoint's platform instance.
  const amdf_kmt_api_t* kmt;
  // Logical KMT device owning paging and future execution state.
  D3DKMT_HANDLE device;
  // Paging queue owned by this logical device.
  D3DKMT_HANDLE paging_queue;
  // Synchronization object owned by the paging queue.
  D3DKMT_HANDLE paging_sync_object;
  // CPU mapping of the paging queue's monitored fence.
  const volatile uint64_t* paging_fence;
};

#endif  // AMDF_SRC_GPU_UMD_WDDM_DEVICE_H_
