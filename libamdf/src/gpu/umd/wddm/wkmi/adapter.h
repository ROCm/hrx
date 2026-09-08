// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef AMDF_SRC_GPU_UMD_WDDM_WKMI_ADAPTER_H_
#define AMDF_SRC_GPU_UMD_WDDM_WKMI_ADAPTER_H_

#include <stdbool.h>
#include <stdint.h>

#include "amdf/amdf.h"
#include "libamdf/src/gpu/umd/wddm/wkmi/bridge_api.h"
#include "libamdf/src/gpu/umd/wddm/wkmi/loader.h"
#include "libamdf/src/platform/windows/kmt_api.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Loaded bridge and parsed private state for one physical GPU adapter.
typedef struct amdf_gpu_wddm_wkmi_adapter_t {
  // Loaded bridge module owning the native adapter and API table.
  amdf_gpu_wddm_wkmi_loader_t loader;
  // Opaque parsed adapter state owned by the loaded bridge.
  amdf_wkmi_bridge_gpu_adapter_t* native;
} amdf_gpu_wddm_wkmi_adapter_t;

// Loads WKMI and parses one physical GPU adapter.
amdf_status_t amdf_gpu_wddm_wkmi_adapter_initialize(
    D3DKMT_HANDLE adapter, uint32_t physical_adapter_index,
    amdf_gpu_wddm_wkmi_adapter_t* out_adapter,
    amdf_wkmi_bridge_gpu_properties_t* out_properties, bool* out_available);

// Releases parsed state and unloads WKMI after every dependent object is gone.
amdf_status_t amdf_gpu_wddm_wkmi_adapter_deinitialize(
    amdf_gpu_wddm_wkmi_adapter_t* adapter);

// Queries the native allocation layout for one aggregate allocation.
amdf_status_t amdf_gpu_wddm_wkmi_adapter_query_allocation_layout(
    const amdf_gpu_wddm_wkmi_adapter_t* adapter, uint64_t byte_length,
    uint32_t* out_allocation_count,
    uint64_t* out_maximum_allocation_byte_length);

// Creates grouped native allocations using WKMI-private driver records.
amdf_status_t amdf_gpu_wddm_wkmi_adapter_create_allocations(
    const amdf_gpu_wddm_wkmi_adapter_t* adapter,
    const amdf_wkmi_bridge_gpu_allocation_create_info_t* create_info,
    uint32_t allocation_handle_capacity, D3DKMT_HANDLE* out_allocation_handles,
    D3DKMT_HANDLE* out_resource, uint32_t* out_allocation_count);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // AMDF_SRC_GPU_UMD_WDDM_WKMI_ADAPTER_H_
