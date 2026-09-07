// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef AMDF_SRC_XDNA_COMMAND_H_
#define AMDF_SRC_XDNA_COMMAND_H_

#include "amdf/xdna.h"
#include "libamdf/src/xdna/umd/command.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// One device address and extent resolved for the command lifetime.
typedef struct amdf_xdna_resolved_binding_t {
  // Stable base address including the caller's binding offset.
  uint64_t device_address;
  // Bound byte length.
  uint64_t byte_length;
} amdf_xdna_resolved_binding_t;

// Creates one immutable fixed-binding XDNA command.
amdf_status_t AMDF_CALL
amdf_xdna_command_create(amdf_xdna_program_t* program,
                         const amdf_xdna_command_create_info_t* create_info,
                         amdf_xdna_command_t** out_command);

// Copies immutable command properties.
amdf_status_t AMDF_CALL amdf_xdna_command_query_info(
    amdf_xdna_command_t* command, amdf_xdna_command_info_t* out_info);

// Returns the program borrowed by one command.
amdf_xdna_program_t* amdf_xdna_command_get_program(
    amdf_xdna_command_t* command);

// Returns the copied invocation control borrowed from one command.
const void* amdf_xdna_command_get_control_bytes(
    const amdf_xdna_command_t* command, uint64_t* out_byte_length);

// Returns the resolved binding array borrowed from one command.
const amdf_xdna_resolved_binding_t* amdf_xdna_command_get_bindings(
    const amdf_xdna_command_t* command, uint32_t* out_binding_count);

// Returns the native prepared command borrowed from one public command.
const amdf_xdna_umd_command_t* amdf_xdna_command_get_umd(
    const amdf_xdna_command_t* command);

// Registers one accepted queue use borrowing this command.
amdf_status_t amdf_xdna_command_register_submission(
    amdf_xdna_command_t* command);

// Releases one retired queue use.
void amdf_xdna_command_unregister_submission(amdf_xdna_command_t* command);

// Destroys one command after all accepted queue uses have retired.
amdf_status_t AMDF_CALL amdf_xdna_command_destroy(amdf_xdna_command_t* command);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // AMDF_SRC_XDNA_COMMAND_H_
