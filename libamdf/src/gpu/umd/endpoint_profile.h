// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef AMDF_SRC_GPU_UMD_ENDPOINT_PROFILE_H_
#define AMDF_SRC_GPU_UMD_ENDPOINT_PROFILE_H_

#include <stdbool.h>

#include "amdf/amdf.h"
#include "libamdf/src/gpu/endpoint_profile.h"
#include "libamdf/src/platform/endpoint.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Queries and qualifies one immutable GPU endpoint profile.
amdf_status_t amdf_gpu_umd_query_endpoint_profile(
    const amdf_platform_endpoint_t* platform_endpoint,
    amdf_gpu_endpoint_profile_t* out_profile, bool* out_available);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // AMDF_SRC_GPU_UMD_ENDPOINT_PROFILE_H_
