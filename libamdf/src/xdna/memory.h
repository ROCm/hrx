// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef AMDF_SRC_XDNA_MEMORY_H_
#define AMDF_SRC_XDNA_MEMORY_H_

#include "amdf/amdf.h"
#include "libamdf/src/xdna/umd/memory.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Creates memory attached to one XDNA device.
amdf_status_t amdf_xdna_memory_create(
    amdf_device_t* device, const amdf_memory_create_info_t* create_info,
    amdf_memory_t** out_memory);

// Returns the native XDNA attachment borrowed from one XDNA memory object.
amdf_xdna_umd_memory_t* amdf_xdna_memory_get_umd(amdf_memory_t* memory);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // AMDF_SRC_XDNA_MEMORY_H_
