// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef AMDF_SRC_ENDPOINT_H_
#define AMDF_SRC_ENDPOINT_H_

#include "amdf/amdf.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Opens one query-only endpoint identity directly.
amdf_status_t AMDF_CALL amdf_endpoint_open(amdf_instance_t* instance,
                                           const amdf_endpoint_id_t* id,
                                           amdf_endpoint_t** out_endpoint);

// Copies immutable endpoint properties cached during open.
amdf_status_t AMDF_CALL amdf_endpoint_query_info(
    amdf_endpoint_t* endpoint, amdf_endpoint_info_t* out_info);

// Closes an endpoint and releases its instance borrow.
amdf_status_t AMDF_CALL amdf_endpoint_close(amdf_endpoint_t* endpoint);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // AMDF_SRC_ENDPOINT_H_
