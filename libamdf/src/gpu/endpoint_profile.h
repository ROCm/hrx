// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef AMDF_SRC_GPU_ENDPOINT_PROFILE_H_
#define AMDF_SRC_GPU_ENDPOINT_PROFILE_H_

#include <stdbool.h>
#include <stdint.h>

#include "amdf/gpu.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Provider-neutral facts used to qualify one immutable GPU profile.
typedef struct amdf_gpu_endpoint_properties_t {
  // Exact Graphics IP identity.
  struct {
    // GFX IP major version.
    uint32_t major;
    // GFX IP minor version.
    uint32_t minor;
    // GFX IP stepping.
    uint32_t stepping;
  } gfx_ip;
  // HSA/KFD ASIC revision used for physical target selection.
  uint32_t asic_revision;
  // Active compute geometry and per-compute-unit limits.
  struct {
    // Number of lanes in one hardware wavefront.
    uint32_t wavefront_size;
    // Total active compute units across all XCCs.
    uint32_t compute_unit_count;
    // Maximum resident hardware waves per compute unit.
    uint32_t maximum_wave_count_per_compute_unit;
    // Effective scratch wave slots per compute unit.
    uint32_t maximum_scratch_wave_count_per_compute_unit;
    // Local data share capacity per compute unit in bytes.
    uint64_t local_data_share_byte_length;
  } compute;
  // Multi-chiplet command-processor topology.
  struct {
    // Number of active XCCs represented by the endpoint.
    uint32_t xcc_count;
    // Uniform number of shader engines within each XCC.
    uint32_t shader_engine_count_per_xcc;
  } topology;
  // Whether the native provider can construct a kernel-published PM4 queue.
  bool supports_pm4_kernel_queue;
} amdf_gpu_endpoint_properties_t;

// Immutable qualified GPU profile owned by one core endpoint.
typedef struct amdf_gpu_endpoint_profile_t {
  // Public target and compute properties copied by the GPU extension.
  amdf_gpu_endpoint_info_t info;
  // Whether the native provider can construct a kernel-published PM4 queue.
  bool supports_pm4_kernel_queue;
} amdf_gpu_endpoint_profile_t;

// Validates and normalizes |properties| into |out_profile|.
bool amdf_gpu_endpoint_profile_initialize(
    const amdf_gpu_endpoint_properties_t* properties,
    amdf_gpu_endpoint_profile_t* out_profile);

// Returns the borrowed public information stored in |profile|.
const amdf_gpu_endpoint_info_t* amdf_gpu_endpoint_profile_get_info(
    const amdf_gpu_endpoint_profile_t* profile);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // AMDF_SRC_GPU_ENDPOINT_PROFILE_H_
