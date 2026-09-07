// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef AMDF_SRC_XDNA_UMD_MCDM_LEGACY_SUBMISSION_H_
#define AMDF_SRC_XDNA_UMD_MCDM_LEGACY_SUBMISSION_H_

#include <stdint.h>

#include "amdf/amdf.h"
#include "libamdf/src/xdna/umd/mcdm/private_allocation.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Exact byte length of one legacy ERT start-NPU packet.
#define AMDF_WINDOWS_XDNA_LEGACY_ERT_PACKET_SIZE 68u

// Maximum private record accepted by the installed NPU5 submission ABI.
#define AMDF_WINDOWS_XDNA_LEGACY_SUBMISSION_CAPACITY 624u

// Fixed ERT command packet copied into one execution allocation.
typedef struct amdf_windows_xdna_legacy_ert_packet_t {
  // Opaque installed-ABI bytes.
  uint8_t bytes[AMDF_WINDOWS_XDNA_LEGACY_ERT_PACKET_SIZE];
} amdf_windows_xdna_legacy_ert_packet_t;

// Bounded private record passed beside one KMT command submission.
typedef struct amdf_windows_xdna_legacy_submission_t {
  // Opaque installed-ABI bytes.
  uint8_t bytes[AMDF_WINDOWS_XDNA_LEGACY_SUBMISSION_CAPACITY];
  // Number of initialized bytes in `bytes`.
  uint32_t byte_length;
} amdf_windows_xdna_legacy_submission_t;

// Builds the context-aperture publication record.
void amdf_windows_xdna_legacy_submission_build_aperture(
    const amdf_windows_xdna_private_allocation_t* instruction_allocation,
    amdf_windows_xdna_legacy_submission_t* out_submission);

// Builds the program-independent context initialization record.
void amdf_windows_xdna_legacy_submission_build_context_initialize(
    const amdf_windows_xdna_private_allocation_t* command_allocation,
    amdf_windows_xdna_legacy_submission_t* out_submission);

// Builds the instruction-aperture watermark update record.
void amdf_windows_xdna_legacy_submission_build_watermark(
    const amdf_windows_xdna_private_allocation_t* instruction_allocation,
    uint64_t watermark, amdf_windows_xdna_legacy_submission_t* out_submission);

// Builds one fixed-binding ERT start-NPU packet.
amdf_status_t amdf_windows_xdna_legacy_ert_packet_build(
    uint64_t instruction_address, uint32_t instruction_byte_length,
    const uint64_t* binding_addresses, uint32_t binding_count,
    amdf_windows_xdna_legacy_ert_packet_t* out_packet);

// Builds the private execution record adjoining one ERT packet.
void amdf_windows_xdna_legacy_submission_build_execute(
    const amdf_windows_xdna_private_allocation_t* execution_allocation,
    const amdf_windows_xdna_private_allocation_t* command_allocation,
    const amdf_windows_xdna_legacy_ert_packet_t* packet,
    amdf_windows_xdna_legacy_submission_t* out_submission);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // AMDF_SRC_XDNA_UMD_MCDM_LEGACY_SUBMISSION_H_
