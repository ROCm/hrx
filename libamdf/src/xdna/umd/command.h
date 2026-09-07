// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef AMDF_SRC_XDNA_UMD_COMMAND_H_
#define AMDF_SRC_XDNA_UMD_COMMAND_H_

#include <stdint.h>

#include "amdf/amdf.h"
#include "libamdf/src/xdna/umd/device.h"
#include "libamdf/src/xdna/umd/memory.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

typedef struct amdf_xdna_umd_command_t amdf_xdna_umd_command_t;

// One native memory binding resolved for immutable command preparation.
typedef struct amdf_xdna_umd_command_binding_t {
  // Native memory attachment borrowed by the public command.
  amdf_xdna_umd_memory_t* memory;
  // Device address of the first bound byte.
  uint64_t device_address;
  // Bound byte length.
  uint64_t byte_length;
} amdf_xdna_umd_command_binding_t;

// Materializes one fixed-binding native XDNA command.
amdf_status_t amdf_xdna_umd_command_create(
    amdf_xdna_umd_device_t* device, const void* program_bytes,
    uint64_t program_byte_length, const void* control_bytes,
    uint64_t control_byte_length,
    const amdf_xdna_umd_command_binding_t* bindings, uint32_t binding_count,
    amdf_xdna_umd_command_t** out_command);

// Releases one native command after all accepted uses have retired.
amdf_status_t amdf_xdna_umd_command_destroy(amdf_xdna_umd_command_t* command);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // AMDF_SRC_XDNA_UMD_COMMAND_H_
