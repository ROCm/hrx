// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef AMDF_SRC_XDNA_EXTENSION_H_
#define AMDF_SRC_XDNA_EXTENSION_H_

#include "amdf/amdf.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Acquires the XDNA extension table for one previously validated version range.
amdf_status_t amdf_xdna_extension_query(uint32_t minimum_version,
                                        uint32_t maximum_version,
                                        const void** out_extension_api);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // AMDF_SRC_XDNA_EXTENSION_H_
