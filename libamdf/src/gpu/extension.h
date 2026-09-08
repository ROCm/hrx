// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef AMDF_SRC_GPU_EXTENSION_H_
#define AMDF_SRC_GPU_EXTENSION_H_

#include "amdf/amdf.h"
#include "libamdf/src/endpoint.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Acquires the GPU extension table for one previously validated version range.
amdf_status_t amdf_gpu_extension_query(uint32_t minimum_version,
                                       uint32_t maximum_version,
                                       const void** out_extension_api);

// Qualifies and caches the immutable profile for one opened GPU endpoint.
void amdf_gpu_extension_initialize_endpoint(amdf_endpoint_t* endpoint);

// Writes the GPU queue families fully constructible for an opened endpoint.
uint32_t amdf_gpu_extension_query_endpoint_queue_families(
    const amdf_endpoint_info_t* endpoint_info, uint32_t capacity,
    amdf_queue_family_info_t* out_families);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // AMDF_SRC_GPU_EXTENSION_H_
