// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef AMDF_SRC_XDNA_DEVICE_H_
#define AMDF_SRC_XDNA_DEVICE_H_

#include "amdf/xdna.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Materializes one program-independent XDNA context and address domain.
amdf_status_t AMDF_CALL
amdf_xdna_device_create(amdf_endpoint_t* endpoint,
                        const amdf_xdna_device_create_info_t* create_info,
                        amdf_device_t** out_device);

// Copies immutable identity and achieved XDNA placement information.
amdf_status_t AMDF_CALL amdf_xdna_device_query_info(
    amdf_device_t* device, amdf_xdna_device_info_t* out_info);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // AMDF_SRC_XDNA_DEVICE_H_
