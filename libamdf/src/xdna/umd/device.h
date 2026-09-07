// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef AMDF_SRC_XDNA_UMD_DEVICE_H_
#define AMDF_SRC_XDNA_UMD_DEVICE_H_

#include "amdf/xdna.h"
#include "libamdf/src/platform/endpoint.h"
#include "libamdf/src/xdna/endpoint_profile.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

typedef struct amdf_xdna_umd_device_t amdf_xdna_umd_device_t;

// Native result used to publish one successfully materialized XDNA device.
typedef struct amdf_xdna_umd_device_result_t {
  // Opaque identity of the live native context.
  amdf_device_id_t id;
  // Provider epoch invalidating native state after reset.
  uint64_t reset_epoch;
  // Single scheduling mode selected for this context.
  amdf_xdna_scheduling_modes_t scheduling_mode;
  // Generation of the fixed physical placement below.
  uint32_t placement_generation;
  // Origin of the achieved physical backing partition.
  uint32_t physical_column_origin;
  // Width of the achieved physical backing partition.
  uint32_t physical_column_count;
} amdf_xdna_umd_device_result_t;

// Creates one program-independent native XDNA context and address domain.
amdf_status_t amdf_xdna_umd_device_create(
    amdf_platform_endpoint_t* endpoint,
    const amdf_xdna_endpoint_profile_t* profile,
    const amdf_xdna_device_create_info_t* create_info,
    amdf_xdna_umd_device_t** out_device,
    amdf_xdna_umd_device_result_t* out_result);

// Releases native XDNA device state in reverse ownership order.
amdf_status_t amdf_xdna_umd_device_destroy(amdf_xdna_umd_device_t* device);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // AMDF_SRC_XDNA_UMD_DEVICE_H_
