// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef AMDF_SRC_XDNA_PROGRAM_H_
#define AMDF_SRC_XDNA_PROGRAM_H_

#include "amdf/xdna.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Copies and owns one immutable target-native program for `device`.
amdf_status_t AMDF_CALL amdf_xdna_program_create(
    amdf_device_t* device, const amdf_xdna_program_create_info_t* create_info,
    amdf_xdna_program_t** out_program);

// Copies immutable program properties.
amdf_status_t AMDF_CALL amdf_xdna_program_query_info(
    amdf_xdna_program_t* program, amdf_xdna_program_info_t* out_info);

// Returns the device borrowed by one program.
amdf_device_t* amdf_xdna_program_get_device(amdf_xdna_program_t* program);

// Returns immutable program information borrowed from one program.
const amdf_xdna_program_info_t* amdf_xdna_program_get_info(
    const amdf_xdna_program_t* program);

// Returns the copied component array borrowed from one program.
const amdf_xdna_program_component_t* amdf_xdna_program_get_components(
    const amdf_xdna_program_t* program, uint32_t* out_component_count);

// Registers one command borrowing a program.
amdf_status_t amdf_xdna_program_register_command(amdf_xdna_program_t* program);

// Releases one command borrow.
void amdf_xdna_program_unregister_command(amdf_xdna_program_t* program);

// Destroys a program with no remaining commands.
amdf_status_t AMDF_CALL amdf_xdna_program_destroy(amdf_xdna_program_t* program);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // AMDF_SRC_XDNA_PROGRAM_H_
