// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef AMDF_GPU_H_
#define AMDF_GPU_H_

#include <stdint.h>

#include "amdf/amdf.h"

#ifdef __cplusplus
extern "C" {
#endif

/// The first supported GPU extension version.
#define AMDF_GPU_EXTENSION_VERSION_1 1u

/// The most recent GPU extension version described by this header.
#define AMDF_GPU_EXTENSION_VERSION_LATEST AMDF_GPU_EXTENSION_VERSION_1

/// An `amdf_gpu_endpoint_info_t` output structure.
#define AMDF_STRUCTURE_TYPE_GPU_ENDPOINT_INFO \
  ((amdf_structure_type_t)0x00020001u)

/// Immutable target identity and compute topology of one GPU endpoint.
///
/// This record contains live provider-qualified hardware facts. It does not
/// select an executable format, process-level target features, memory policy,
/// or queue implementation.
typedef struct amdf_gpu_endpoint_info_t {
  /// Must be `AMDF_STRUCTURE_TYPE_GPU_ENDPOINT_INFO`.
  amdf_structure_type_t type;
  /// Must be at least `sizeof(amdf_gpu_endpoint_info_t)`.
  uint32_t structure_size;
  /// Optional output extension chain. No extensions are currently defined.
  void* next;
  /// Exact Graphics IP identity.
  struct {
    /// GFX IP major version.
    uint32_t major;
    /// GFX IP minor version.
    uint32_t minor;
    /// GFX IP stepping encoded in the canonical `gfxMmn` target name.
    uint32_t stepping;
  } gfx_ip;
  /// ASIC revision used for physical compiler-target selection.
  ///
  /// This is the HSA/KFD target revision and is distinct from the PCI
  /// config-space revision in `amdf_endpoint_info_t`. Gfx1250 uses values zero
  /// and one to distinguish A0 and B0 targets.
  uint32_t asic_revision;
  /// Active compute geometry and per-compute-unit limits.
  struct {
    /// Number of lanes in one hardware wavefront.
    uint32_t wavefront_size;
    /// Total active compute units across all XCCs after harvesting.
    uint32_t compute_unit_count;
    /// Maximum resident hardware waves per compute unit.
    uint32_t maximum_wave_count_per_compute_unit;
    /// Effective maximum scratch-backed waves per compute unit.
    uint32_t maximum_scratch_wave_count_per_compute_unit;
    /// Local data share capacity per compute unit in bytes.
    uint64_t local_data_share_byte_length;
  } compute;
  /// Multi-chiplet command-processor topology.
  struct {
    /// Nonzero number of active XCCs represented by the endpoint.
    uint32_t xcc_count;
    /// Uniform number of shader engines within each XCC.
    uint32_t shader_engine_count_per_xcc;
  } topology;
} amdf_gpu_endpoint_info_t;

/// Immutable entry-point table for one negotiated GPU extension version.
///
/// Tables grow only by appending fields. The table and every function pointer
/// covered by `structure_size` remain valid until the providing library is
/// unloaded.
typedef struct amdf_gpu_api_t {
  /// Size in bytes of this table version.
  uint32_t structure_size;
  /// GPU extension version implemented by this table.
  uint32_t extension_version;

  /// Copies immutable GPU properties cached while opening `endpoint`.
  ///
  /// The endpoint must have a provider-qualified GPU profile. Passing another
  /// engine or a GPU that the active provider could not qualify returns the
  /// cached qualification error without modifying the output. The operation is
  /// thread-safe and performs no system call, allocation, provider-library
  /// load, retry, sleep, or device wait. The caller initializes `out_info` and
  /// its complete extension chain.
  amdf_status_t(AMDF_CALL* endpoint_query_info)(
      amdf_endpoint_t* endpoint, amdf_gpu_endpoint_info_t* out_info);
} amdf_gpu_api_t;

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AMDF_GPU_H_
