// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef AMDF_SRC_PROVIDER_EXTENSION_H_
#define AMDF_SRC_PROVIDER_EXTENSION_H_

#include "amdf/amdf.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Acquires an optional API table compiled into this library.
amdf_status_t AMDF_CALL amdf_extension_query(amdf_extension_id_t extension_id,
                                             uint32_t minimum_version,
                                             uint32_t maximum_version,
                                             const void** out_extension_api);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // AMDF_SRC_PROVIDER_EXTENSION_H_
