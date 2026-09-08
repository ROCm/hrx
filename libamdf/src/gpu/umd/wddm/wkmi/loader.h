// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef AMDF_SRC_GPU_UMD_WDDM_WKMI_LOADER_H_
#define AMDF_SRC_GPU_UMD_WDDM_WKMI_LOADER_H_

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif  // WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif  // NOMINMAX
#include <stdint.h>
#include <windows.h>

#include "amdf/amdf.h"
#include "libamdf/src/gpu/umd/wddm/wkmi/bridge_api.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Loaded private WKMI bridge and its negotiated immutable API table.
typedef struct amdf_gpu_wddm_wkmi_loader_t {
  // Module owning every procedure in `api`.
  HMODULE module;
  // Borrowed API table valid until `module` is unloaded.
  const amdf_wkmi_bridge_api_t* api;
} amdf_gpu_wddm_wkmi_loader_t;

// Loads the bridge and negotiates its most recent supported API version.
amdf_status_t amdf_gpu_wddm_wkmi_loader_initialize(
    amdf_gpu_wddm_wkmi_loader_t* out_loader);

// Unloads the bridge after all calls through its API table have returned.
amdf_status_t amdf_gpu_wddm_wkmi_loader_deinitialize(
    amdf_gpu_wddm_wkmi_loader_t* loader);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // AMDF_SRC_GPU_UMD_WDDM_WKMI_LOADER_H_
