// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef AMDF_SRC_PROVIDER_EXTENSION_H_
#define AMDF_SRC_PROVIDER_EXTENSION_H_

#include "amdf/amdf.h"
#include "libamdf/src/platform/endpoint.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Opens one endpoint and composes all compiled engine queue families.
amdf_status_t AMDF_CALL amdf_provider_endpoint_open(
    amdf_instance_t* instance, const amdf_endpoint_id_t* id,
    amdf_endpoint_t** out_endpoint);

// Acquires an optional API table compiled into this library.
amdf_status_t AMDF_CALL amdf_extension_query(amdf_extension_id_t extension_id,
                                             uint32_t minimum_version,
                                             uint32_t maximum_version,
                                             const void** out_extension_api);

// Writes the queue families fully constructible for one opened endpoint.
uint32_t amdf_extension_query_endpoint_queue_families(
    const amdf_endpoint_info_t* endpoint_info,
    const amdf_platform_endpoint_t* platform_endpoint, uint32_t capacity,
    amdf_queue_family_info_t* out_families);
#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // AMDF_SRC_PROVIDER_EXTENSION_H_
