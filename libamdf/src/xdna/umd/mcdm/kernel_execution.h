// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef AMDF_SRC_XDNA_UMD_MCDM_KERNEL_EXECUTION_H_
#define AMDF_SRC_XDNA_UMD_MCDM_KERNEL_EXECUTION_H_

#include <stdint.h>

#include "libamdf/src/wait.h"
#include "libamdf/src/xdna/umd/mcdm/legacy_submission.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

typedef struct amdf_windows_xdna_kernel_execution_t
    amdf_windows_xdna_kernel_execution_t;

// Immutable native record owned by one prepared command.
typedef struct amdf_windows_xdna_kernel_command_t {
  // Device execution state borrowing the command until slot release.
  amdf_windows_xdna_kernel_execution_t* execution;
  // Dense device-owned instruction and execution slot.
  uint32_t slot_ordinal;
  // Fixed command-buffer length required by the private KMQ contract.
  uint32_t command_byte_length;
  // Device address of the ERT execution allocation.
  uint64_t command_address;
  // Prebuilt private execution record consumed synchronously by KMT submit.
  amdf_windows_xdna_legacy_submission_t submission;
} amdf_windows_xdna_kernel_command_t;

// Allocates inert host state for one device-owned execution path.
amdf_status_t amdf_windows_xdna_kernel_execution_create(
    amdf_xdna_umd_device_t* device,
    amdf_windows_xdna_kernel_execution_t** out_execution);

// Tears down device-owned execution state without waiting for native work.
amdf_status_t amdf_windows_xdna_kernel_execution_destroy(
    amdf_windows_xdna_kernel_execution_t* execution);

// Reserves and materializes one immutable native command slot.
amdf_status_t amdf_windows_xdna_kernel_execution_prepare_command(
    amdf_windows_xdna_kernel_execution_t* execution, const void* program_bytes,
    uint64_t program_byte_length, const void* control_bytes,
    uint64_t control_byte_length, const uint64_t* binding_addresses,
    uint32_t binding_count, amdf_windows_xdna_kernel_command_t* out_command);

// Returns one command slot after all native uses have retired.
void amdf_windows_xdna_kernel_execution_release_command(
    amdf_windows_xdna_kernel_command_t* command);

// Acquires the device's single known-correct public KMQ lease.
amdf_status_t amdf_windows_xdna_kernel_execution_acquire_queue(
    amdf_windows_xdna_kernel_execution_t* execution);

// Releases one queue lease after all accepted work has retired.
void amdf_windows_xdna_kernel_execution_release_queue(
    amdf_windows_xdna_kernel_execution_t* execution);

// Publishes one prepared command without parsing or allocating.
amdf_status_t amdf_windows_xdna_kernel_execution_submit(
    amdf_windows_xdna_kernel_execution_t* execution,
    const amdf_windows_xdna_kernel_command_t* command,
    uint64_t* out_native_submission);

// Samples the mapped native retirement watermark.
uint64_t amdf_windows_xdna_kernel_execution_query_progress(
    const amdf_windows_xdna_kernel_execution_t* execution);

// Returns the device's observed terminal execution failure without polling.
amdf_status_t amdf_windows_xdna_kernel_execution_query_terminal_status(
    const amdf_windows_xdna_kernel_execution_t* execution);

// Waits for one native submission with caller-selected active polling.
amdf_status_t amdf_windows_xdna_kernel_execution_wait(
    amdf_windows_xdna_kernel_execution_t* execution, uint64_t native_submission,
    const amdf_wait_deadline_t* deadline);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // AMDF_SRC_XDNA_UMD_MCDM_KERNEL_EXECUTION_H_
