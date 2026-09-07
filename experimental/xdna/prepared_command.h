// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Fixed-binding XDNA command prepared for native queue publication.

#ifndef IREE_EXPERIMENTAL_XDNA_PREPARED_COMMAND_H_
#define IREE_EXPERIMENTAL_XDNA_PREPARED_COMMAND_H_

#include "amdf/xdna.h"
#include "iree/base/api.h"
#include "iree/hal/api.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// One direct HAL buffer binding resolved into an exact XDNA address domain.
//
// The command-buffer implementation validates and resolves the HAL buffer and
// its AMD attachment before constructing this record. All fields are borrowed
// during prepared-command creation. The resulting command retains
// |buffer_ref.buffer| and the libamdf command borrows |memory| until
// destruction.
typedef struct iree_hal_amd_xdna_prepared_command_binding_t {
  // Direct logical HAL buffer range whose access contract is validated.
  iree_hal_buffer_ref_t buffer_ref;
  // XDNA memory attachment backing |buffer_ref.buffer|.
  amdf_memory_t* memory;
  // Byte offset of the bound range within |memory|.
  uint64_t memory_byte_offset;
  // Exact XDNA address of the first bound byte.
  uint64_t device_address;
} iree_hal_amd_xdna_prepared_command_binding_t;

// One immutable fixed-binding XDNA command.
typedef struct iree_hal_amd_xdna_prepared_command_t
    iree_hal_amd_xdna_prepared_command_t;

// Prepares one executable function for native queue publication.
//
// Each binding must be direct and already resolved into the executable's XDNA
// address domain. Creation validates the function's canonical access, range,
// and alignment contracts before asking libamdf to prepare native state. The
// returned command retains |executable| and every logical HAL buffer. It owns
// no queue or submission state. On failure, |out_prepared_command| is set to
// NULL and no native command remains live.
iree_status_t iree_hal_amd_xdna_prepared_command_create(
    iree_hal_executable_t* executable, iree_hal_executable_function_t function,
    iree_host_size_t binding_count,
    const iree_hal_amd_xdna_prepared_command_binding_t* bindings,
    iree_allocator_t host_allocator,
    iree_hal_amd_xdna_prepared_command_t** out_prepared_command);

// Destroys |prepared_command| and releases its retained resources.
//
// A provider teardown failure leaves the prepared command and every retained
// resource live so the operation may be retried after native uses retire.
iree_status_t iree_hal_amd_xdna_prepared_command_destroy(
    iree_hal_amd_xdna_prepared_command_t* prepared_command);

// Returns the provider command borrowed from |prepared_command|.
amdf_xdna_command_t* iree_hal_amd_xdna_prepared_command_get_native_command(
    const iree_hal_amd_xdna_prepared_command_t* prepared_command);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // IREE_EXPERIMENTAL_XDNA_PREPARED_COMMAND_H_
