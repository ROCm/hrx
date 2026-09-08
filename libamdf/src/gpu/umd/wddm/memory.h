// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef AMDF_SRC_GPU_UMD_WDDM_MEMORY_H_
#define AMDF_SRC_GPU_UMD_WDDM_MEMORY_H_

#include "amdf/amdf.h"
#include "libamdf/src/gpu/umd/device.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Retries teardown of memory retained after failed construction rollback.
amdf_status_t amdf_gpu_wddm_device_drain_memory_releases(
    amdf_gpu_umd_device_t* device);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // AMDF_SRC_GPU_UMD_WDDM_MEMORY_H_
