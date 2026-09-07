// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/xdna/umd/mcdm/legacy_submission.h"

#include <stddef.h>
#include <string.h>

enum {
  AMDF_WINDOWS_XDNA_LEGACY_SUBMISSION_HEADER_SIZE = 104,
  AMDF_WINDOWS_XDNA_LEGACY_SUBMISSION_COMMAND_COPY_SIZE = 512,
};

static void amdf_windows_xdna_legacy_write_u32(uint8_t* bytes, size_t offset,
                                               uint32_t value) {
  memcpy(bytes + offset, &value, sizeof(value));
}

static void amdf_windows_xdna_legacy_write_u64(uint8_t* bytes, size_t offset,
                                               uint64_t value) {
  memcpy(bytes + offset, &value, sizeof(value));
}

static void amdf_windows_xdna_legacy_submission_initialize(
    uint32_t byte_length,
    amdf_windows_xdna_legacy_submission_t* out_submission) {
  memset(out_submission, 0, sizeof(*out_submission));
  out_submission->byte_length = byte_length;
}

void amdf_windows_xdna_legacy_submission_build_aperture(
    const amdf_windows_xdna_private_allocation_t* instruction_allocation,
    amdf_windows_xdna_legacy_submission_t* out_submission) {
  amdf_windows_xdna_legacy_submission_initialize(
      AMDF_WINDOWS_XDNA_LEGACY_SUBMISSION_HEADER_SIZE, out_submission);
  amdf_windows_xdna_legacy_write_u64(out_submission->bytes, 0x00, 2);
  amdf_windows_xdna_legacy_write_u64(out_submission->bytes, 0x08,
                                     instruction_allocation->allocation);
  amdf_windows_xdna_legacy_write_u64(
      out_submission->bytes, 0x10,
      instruction_allocation->descriptor.allocation_byte_length);
}

void amdf_windows_xdna_legacy_submission_build_context_initialize(
    const amdf_windows_xdna_private_allocation_t* command_allocation,
    amdf_windows_xdna_legacy_submission_t* out_submission) {
  amdf_windows_xdna_legacy_submission_initialize(
      AMDF_WINDOWS_XDNA_LEGACY_SUBMISSION_CAPACITY, out_submission);
  amdf_windows_xdna_legacy_write_u64(out_submission->bytes, 0x00, 5);
  amdf_windows_xdna_legacy_write_u64(out_submission->bytes, 0x28,
                                     command_allocation->allocation);
  amdf_windows_xdna_legacy_write_u32(out_submission->bytes, 0x30, 0);
  amdf_windows_xdna_legacy_write_u32(out_submission->bytes, 0x34, 8);
  amdf_windows_xdna_legacy_write_u64(
      out_submission->bytes, 0x38,
      (uint64_t)(uintptr_t)command_allocation->host_pointer);
  memcpy(
      out_submission->bytes + AMDF_WINDOWS_XDNA_LEGACY_SUBMISSION_HEADER_SIZE,
      command_allocation->host_pointer,
      AMDF_WINDOWS_XDNA_LEGACY_SUBMISSION_COMMAND_COPY_SIZE);
}

void amdf_windows_xdna_legacy_submission_build_watermark(
    const amdf_windows_xdna_private_allocation_t* instruction_allocation,
    uint64_t watermark, amdf_windows_xdna_legacy_submission_t* out_submission) {
  amdf_windows_xdna_legacy_submission_initialize(
      AMDF_WINDOWS_XDNA_LEGACY_SUBMISSION_HEADER_SIZE, out_submission);
  amdf_windows_xdna_legacy_write_u64(out_submission->bytes, 0x00, 9);
  amdf_windows_xdna_legacy_write_u64(out_submission->bytes, 0x08,
                                     instruction_allocation->allocation);
  amdf_windows_xdna_legacy_write_u64(out_submission->bytes, 0x10, watermark);
}

amdf_status_t amdf_windows_xdna_legacy_ert_packet_build(
    uint64_t instruction_address, uint32_t instruction_byte_length,
    const uint64_t* binding_addresses, uint32_t binding_count,
    amdf_windows_xdna_legacy_ert_packet_t* out_packet) {
  if (out_packet == NULL || instruction_address == 0 ||
      instruction_byte_length == 0 ||
      instruction_byte_length % sizeof(uint32_t) != 0 || binding_count > 5 ||
      (binding_count != 0) != (binding_addresses != NULL)) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  memset(out_packet, 0, sizeof(*out_packet));
  amdf_windows_xdna_legacy_write_u32(out_packet->bytes, 0x00, 0x30010001);
  amdf_windows_xdna_legacy_write_u32(out_packet->bytes, 0x04, 1);
  amdf_windows_xdna_legacy_write_u32(out_packet->bytes, 0x08, 3);
  amdf_windows_xdna_legacy_write_u64(out_packet->bytes, 0x10,
                                     instruction_address);
  amdf_windows_xdna_legacy_write_u32(
      out_packet->bytes, 0x18, instruction_byte_length / sizeof(uint32_t));
  for (uint32_t i = 0; i < binding_count; ++i) {
    amdf_windows_xdna_legacy_write_u64(
        out_packet->bytes, 0x1C + i * sizeof(uint64_t), binding_addresses[i]);
  }
  return AMDF_STATUS_OK;
}

void amdf_windows_xdna_legacy_submission_build_execute(
    const amdf_windows_xdna_private_allocation_t* execution_allocation,
    const amdf_windows_xdna_private_allocation_t* command_allocation,
    const amdf_windows_xdna_legacy_ert_packet_t* packet,
    amdf_windows_xdna_legacy_submission_t* out_submission) {
  amdf_windows_xdna_legacy_submission_initialize(
      AMDF_WINDOWS_XDNA_LEGACY_SUBMISSION_HEADER_SIZE +
          AMDF_WINDOWS_XDNA_LEGACY_SUBMISSION_COMMAND_COPY_SIZE,
      out_submission);
  amdf_windows_xdna_legacy_write_u64(out_submission->bytes, 0x00, 3);
  amdf_windows_xdna_legacy_write_u64(out_submission->bytes, 0x08,
                                     execution_allocation->allocation);
  amdf_windows_xdna_legacy_write_u64(out_submission->bytes, 0x10,
                                     sizeof(*packet));
  amdf_windows_xdna_legacy_write_u64(out_submission->bytes, 0x28,
                                     command_allocation->allocation);
  amdf_windows_xdna_legacy_write_u32(out_submission->bytes, 0x30, 8);
  amdf_windows_xdna_legacy_write_u32(out_submission->bytes, 0x34, 8);
  amdf_windows_xdna_legacy_write_u64(
      out_submission->bytes, 0x38,
      (uint64_t)(uintptr_t)command_allocation->host_pointer + 8);
  memcpy(
      out_submission->bytes + AMDF_WINDOWS_XDNA_LEGACY_SUBMISSION_HEADER_SIZE,
      packet, sizeof(*packet));
}
