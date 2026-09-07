// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef AMDF_SRC_INSTANCE_H_
#define AMDF_SRC_INSTANCE_H_

#include "amdf/amdf.h"
#include "libamdf/src/platform/instance.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Creates an independent provider instance.
amdf_status_t AMDF_CALL
amdf_instance_create(const amdf_instance_create_info_t* create_info,
                     amdf_instance_t** out_instance);

// Destroys an instance with no remaining children.
amdf_status_t AMDF_CALL amdf_instance_destroy(amdf_instance_t* instance);

// Enumerates a bounded snapshot of independently selectable AMD endpoints.
amdf_status_t AMDF_CALL amdf_endpoint_enumerate(
    amdf_instance_t* instance, uint32_t capacity,
    amdf_endpoint_summary_t* summaries, uint32_t* out_count);

// Returns the platform implementation borrowed by the instance.
amdf_platform_instance_t* amdf_instance_platform(amdf_instance_t* instance);

// Registers an endpoint that borrows the instance.
amdf_status_t amdf_instance_register_endpoint(amdf_instance_t* instance);

// Unregisters one previously registered endpoint.
void amdf_instance_unregister_endpoint(amdf_instance_t* instance);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // AMDF_SRC_INSTANCE_H_
