// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef AMDF_SRC_GPU_UMD_WDDM_WKMI_BRIDGE_API_H_
#define AMDF_SRC_GPU_UMD_WDDM_WKMI_BRIDGE_API_H_

#include <stdint.h>

#if defined(_WIN32)
#define AMDF_WKMI_BRIDGE_CALL __cdecl
#else
#define AMDF_WKMI_BRIDGE_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// First supported private bridge ABI version.
#define AMDF_WKMI_BRIDGE_ABI_VERSION_1 1u

// Most recent private bridge ABI version described by this header.
#define AMDF_WKMI_BRIDGE_ABI_VERSION_LATEST AMDF_WKMI_BRIDGE_ABI_VERSION_1

// Result of one bridge operation.
typedef uint32_t amdf_wkmi_bridge_result_t;
enum amdf_wkmi_bridge_result_e {
  // The operation completed successfully.
  AMDF_WKMI_BRIDGE_RESULT_SUCCESS = 0,
  // The adapter or requested operation is not represented by pinned WKMI.
  AMDF_WKMI_BRIDGE_RESULT_UNSUPPORTED = 1,
  // A required argument or output structure was invalid.
  AMDF_WKMI_BRIDGE_RESULT_INVALID_ARGUMENT = 2,
  // The caller and bridge have no mutually supported ABI version.
  AMDF_WKMI_BRIDGE_RESULT_VERSION_MISMATCH = 3,
  // WKMI or the Windows kernel-mode driver returned a native failure.
  AMDF_WKMI_BRIDGE_RESULT_NATIVE_FAILURE = 4,
  // A bridge-owned allocation failed.
  AMDF_WKMI_BRIDGE_RESULT_RESOURCE_EXHAUSTED = 5,
  // The binary dependency failed without a representable native status.
  AMDF_WKMI_BRIDGE_RESULT_INTERNAL = 6,
};

// Provider properties returned for one qualified physical GPU adapter.
typedef struct amdf_wkmi_bridge_gpu_properties_t {
  // Graphics IP major version, or a negative value when unavailable.
  int32_t gfx_ip_major;
  // Graphics IP minor version, or a negative value when unavailable.
  int32_t gfx_ip_minor;
  // Graphics IP stepping, or a negative value when unavailable.
  int32_t gfx_ip_stepping;
  // Raw HSA/KFD ASIC revision published by WKMI.
  uint32_t asic_revision;
  // Number of lanes in one hardware wavefront.
  uint32_t wavefront_size;
  // Total active compute units across every XCC.
  uint32_t compute_unit_count;
  // Maximum resident hardware waves per compute unit.
  uint32_t maximum_wave_count_per_compute_unit;
  // Maximum scratch-backed waves per compute unit.
  uint32_t maximum_scratch_wave_count_per_compute_unit;
  // Local data share capacity per compute unit in bytes.
  uint64_t local_data_share_byte_length;
  // Number of active XCCs represented by the physical adapter.
  uint32_t xcc_count;
  // Total number of active shader engines across every XCC.
  uint32_t shader_engine_count;
} amdf_wkmi_bridge_gpu_properties_t;

#ifdef __cplusplus
static_assert(sizeof(amdf_wkmi_bridge_gpu_properties_t) == 48,
              "WKMI GPU property ABI must remain stable");
#else
_Static_assert(sizeof(amdf_wkmi_bridge_gpu_properties_t) == 48,
               "WKMI GPU property ABI must remain stable");
#endif

// Immutable entry-point table for private bridge ABI version 1.
typedef struct amdf_wkmi_bridge_api_t {
  // Size in bytes of this table version.
  uint32_t structure_size;
  // Bridge ABI version implemented by this table.
  uint32_t abi_version;

  // Queries one physical GPU adapter without retaining native state.
  //
  // |adapter_handle| is a live D3DKMT adapter handle and
  // |physical_adapter_index| selects one physical adapter represented by it.
  // |out_native_status| receives the NTSTATUS only for
  // AMDF_WKMI_BRIDGE_RESULT_NATIVE_FAILURE and is zero otherwise.
  amdf_wkmi_bridge_result_t(AMDF_WKMI_BRIDGE_CALL* query_gpu_properties)(
      uint32_t adapter_handle, uint32_t physical_adapter_index,
      amdf_wkmi_bridge_gpu_properties_t* out_properties,
      uint32_t* out_native_status);

} amdf_wkmi_bridge_api_t;

#ifdef __cplusplus
static_assert(sizeof(amdf_wkmi_bridge_api_t) == 16,
              "WKMI entry-point table ABI must remain stable");
#else
_Static_assert(sizeof(amdf_wkmi_bridge_api_t) == 16,
               "WKMI entry-point table ABI must remain stable");
#endif

// Negotiates one immutable bridge API table.
typedef amdf_wkmi_bridge_result_t(
    AMDF_WKMI_BRIDGE_CALL* amdf_wkmi_bridge_query_api_fn_t)(
    uint32_t minimum_version, uint32_t maximum_version,
    const amdf_wkmi_bridge_api_t** out_api);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // AMDF_SRC_GPU_UMD_WDDM_WKMI_BRIDGE_API_H_
