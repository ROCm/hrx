// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// HAL executable backed by qualified XDNA images and libamdf programs.

#ifndef IREE_EXPERIMENTAL_XDNA_EXECUTABLE_H_
#define IREE_EXPERIMENTAL_XDNA_EXECUTABLE_H_

#include "amdf/xdna.h"
#include "iree/base/api.h"
#include "iree/base/byte_sequence.h"
#include "iree/hal/api.h"
#include "iree/hal/drivers/amd/xdna/image/aie2p/target.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Native objects and immutable metadata selected by one executable entry.
//
// All pointers borrow storage from the executable and remain valid until it is
// destroyed. The program may be used to create fixed-binding libamdf commands.
typedef struct iree_hal_amd_xdna_executable_entry_t {
  // Provider-owned immutable program used by this entry.
  amdf_xdna_program_t* program;
  // Invocation CONTROL transaction containing entry-relative address patches.
  iree_const_byte_span_t control;
  // Number of dense buffer bindings accepted by this entry.
  uint32_t binding_count;
} iree_hal_amd_xdna_executable_entry_t;

// Creates an XDNA HAL executable from one canonical image.
//
// The image is completely decoded, target-qualified, and lowered before any
// provider program is published. One libamdf program is created for each
// distinct ARRAY realization and shared by every entry selecting that ARRAY.
// |queue_family| identifies the exact family on which the executable may run
// and is borrowed from the parent device. |source_sequence| and |target| are
// borrowed during the call. |xdna_api| and |device| are borrowed for the
// lifetime of the executable and every program it owns. On failure,
// |out_executable| is set to NULL and no provider program remains live.
iree_status_t iree_hal_amd_xdna_executable_create(
    const amdf_xdna_api_t* xdna_api, amdf_device_t* device,
    const iree_hal_queue_family_t* queue_family,
    iree_byte_sequence_t* source_sequence,
    const iree_hal_amd_xdna_aie2p_target_t* target,
    amdf_xdna_program_flags_t required_program_flags,
    iree_allocator_t host_allocator, iree_hal_executable_t** out_executable);

// Returns true when |executable| is an AMD XDNA executable.
bool iree_hal_amd_xdna_executable_isa(iree_hal_executable_t* executable);

// Copies the native entry selected by |function| into |out_entry|.
iree_status_t iree_hal_amd_xdna_executable_query_entry(
    iree_hal_executable_t* executable, iree_hal_executable_function_t function,
    iree_hal_amd_xdna_executable_entry_t* out_entry);

// Copies one entry-relative binding contract into |out_binding|.
iree_status_t iree_hal_amd_xdna_executable_query_binding(
    iree_hal_executable_t* executable, iree_hal_executable_function_t function,
    iree_host_size_t binding_ordinal,
    iree_hal_amd_xdna_elf_binding_record_t* out_binding);

// Creates one fixed-binding provider command for |function|.
//
// |bindings| must contain exactly the number of buffers declared by the
// executable entry. The provider consumes the array during the call and owns
// the returned command. The caller must destroy it with
// iree_hal_amd_xdna_executable_destroy_native_command before releasing
// |executable|. On failure, |out_command| is set to NULL.
iree_status_t iree_hal_amd_xdna_executable_create_native_command(
    iree_hal_executable_t* executable, iree_hal_executable_function_t function,
    uint32_t binding_count, const amdf_xdna_command_binding_t* bindings,
    amdf_xdna_command_t** out_command);

// Destroys one command created from |executable|.
//
// A provider teardown failure leaves |command| live and may be retried. The
// executable and every bound memory attachment must remain live until this
// succeeds.
iree_status_t iree_hal_amd_xdna_executable_destroy_native_command(
    iree_hal_executable_t* executable, amdf_xdna_command_t* command);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // IREE_EXPERIMENTAL_XDNA_EXECUTABLE_H_
