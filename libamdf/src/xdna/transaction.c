// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/xdna/transaction.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
  AMDF_XDNA_TRANSACTION_HEADER_SIZE = 16,
};

static uint32_t amdf_xdna_transaction_read_u32(const uint8_t* bytes) {
  return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
         ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static void amdf_xdna_transaction_write_u32(uint8_t* bytes, uint32_t value) {
  bytes[0] = (uint8_t)value;
  bytes[1] = (uint8_t)(value >> 8);
  bytes[2] = (uint8_t)(value >> 16);
  bytes[3] = (uint8_t)(value >> 24);
}

static amdf_status_t amdf_xdna_transaction_query_operation_size(
    const uint8_t* bytes, size_t remaining_byte_length,
    size_t* out_operation_size) {
  if (remaining_byte_length < sizeof(uint32_t)) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  const uint8_t opcode = bytes[0];
  size_t minimum_size = 0;
  size_t size_field_offset = 0;
  switch (opcode) {
    case 0:  // XAIE_IO_WRITE
      minimum_size = 24;
      size_field_offset = 20;
      break;
    case 1:  // XAIE_IO_BLOCKWRITE
      minimum_size = 16;
      size_field_offset = 12;
      break;
    case 2:  // XAIE_IO_BLOCKSET
      minimum_size = 16;
      break;
    case 3:  // XAIE_IO_MASKWRITE
      minimum_size = 28;
      size_field_offset = 24;
      break;
    case 4:  // XAIE_IO_MASKPOLL
    case 7:  // XAIE_IO_MASKPOLL_BUSY
      minimum_size = 32;
      size_field_offset = 24;
      break;
    case 5:   // XAIE_IO_NOOP
    case 6:   // XAIE_IO_PREEMPT
    case 13:  // XAIE_IO_UPDATE_SCRATCH
      minimum_size = 4;
      break;
    case 8:   // XAIE_IO_LOADPDI
    case 10:  // XAIE_IO_CREATE_SCRATCHPAD
      minimum_size = 16;
      break;
    case 9:   // XAIE_IO_LOAD_PM_START
    case 11:  // XAIE_IO_UPDATE_STATE_TABLE
      minimum_size = 8;
      break;
    case 12:  // XAIE_IO_UPDATE_REG
      minimum_size = 12;
      break;
    default:
      if (opcode < 128) {
        return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
      }
      minimum_size = 8;
      size_field_offset = 4;
      break;
  }

  if (minimum_size > remaining_byte_length) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  const size_t operation_size =
      size_field_offset == 0
          ? minimum_size
          : (size_t)amdf_xdna_transaction_read_u32(bytes + size_field_offset);
  if (operation_size < minimum_size || operation_size % sizeof(uint32_t) != 0 ||
      operation_size > remaining_byte_length) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  *out_operation_size = operation_size;
  return AMDF_STATUS_OK;
}

amdf_status_t amdf_xdna_transaction_validate(
    const amdf_xdna_transaction_target_t* target, uint32_t format_version,
    const void* bytes, uint64_t byte_length) {
  if (target == NULL || bytes == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  if (format_version != AMDF_XDNA_TRANSACTION_FORMAT_VERSION_0_1) {
    return amdf_make_api_status(AMDF_STATUS_CODE_UNSUPPORTED);
  }
  if (byte_length < AMDF_XDNA_TRANSACTION_HEADER_SIZE ||
      byte_length > UINT32_MAX || byte_length > SIZE_MAX ||
      byte_length % sizeof(uint32_t) != 0) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }

  const uint8_t* data = (const uint8_t*)bytes;
  if (data[0] != 0 || data[1] != 1 || data[2] != target->device_generation ||
      data[3] != target->row_count || data[4] != target->column_count ||
      data[5] != target->memory_tile_row_count || data[6] != 0 ||
      data[7] != 0 ||
      amdf_xdna_transaction_read_u32(data + 12) != (uint32_t)byte_length) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }

  const uint32_t operation_count = amdf_xdna_transaction_read_u32(data + 8);
  const size_t transaction_size = (size_t)byte_length;
  if ((size_t)operation_count >
      (transaction_size - AMDF_XDNA_TRANSACTION_HEADER_SIZE) /
          sizeof(uint32_t)) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }

  size_t offset = AMDF_XDNA_TRANSACTION_HEADER_SIZE;
  for (uint32_t i = 0; i < operation_count; ++i) {
    size_t operation_size = 0;
    const amdf_status_t status = amdf_xdna_transaction_query_operation_size(
        data + offset, transaction_size - offset, &operation_size);
    if (!amdf_status_is_ok(status)) {
      return status;
    }
    offset += operation_size;
  }
  return offset == transaction_size
             ? AMDF_STATUS_OK
             : amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
}

amdf_status_t amdf_xdna_transaction_compose(
    const void* first_bytes, uint64_t first_byte_length,
    const void* second_bytes, uint64_t second_byte_length, void* output_bytes,
    uint64_t output_capacity, uint64_t* out_byte_length) {
  if (first_bytes == NULL || second_bytes == NULL || output_bytes == NULL ||
      out_byte_length == NULL ||
      first_byte_length < AMDF_XDNA_TRANSACTION_HEADER_SIZE ||
      second_byte_length < AMDF_XDNA_TRANSACTION_HEADER_SIZE ||
      first_byte_length > SIZE_MAX || second_byte_length > SIZE_MAX ||
      memcmp(first_bytes, second_bytes, 8) != 0) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  const uint64_t first_body_byte_length =
      first_byte_length - AMDF_XDNA_TRANSACTION_HEADER_SIZE;
  if (first_body_byte_length > UINT32_MAX ||
      second_byte_length > UINT32_MAX - first_body_byte_length) {
    return amdf_make_api_status(AMDF_STATUS_CODE_OUT_OF_RANGE);
  }
  const uint64_t combined_byte_length =
      first_body_byte_length + second_byte_length;
  if (combined_byte_length > output_capacity) {
    return amdf_make_api_status(AMDF_STATUS_CODE_BUFFER_TOO_SMALL);
  }

  const uint8_t* first_data = (const uint8_t*)first_bytes;
  const uint8_t* second_data = (const uint8_t*)second_bytes;
  const uint32_t first_operation_count =
      amdf_xdna_transaction_read_u32(first_data + 8);
  const uint32_t second_operation_count =
      amdf_xdna_transaction_read_u32(second_data + 8);
  if (second_operation_count > UINT32_MAX - first_operation_count) {
    return amdf_make_api_status(AMDF_STATUS_CODE_OUT_OF_RANGE);
  }

  uint8_t* output_data = (uint8_t*)output_bytes;
  memcpy(output_data, first_data, (size_t)first_byte_length);
  memcpy(output_data + first_byte_length,
         second_data + AMDF_XDNA_TRANSACTION_HEADER_SIZE,
         (size_t)(second_byte_length - AMDF_XDNA_TRANSACTION_HEADER_SIZE));
  amdf_xdna_transaction_write_u32(
      output_data + 8, first_operation_count + second_operation_count);
  amdf_xdna_transaction_write_u32(output_data + 12,
                                  (uint32_t)combined_byte_length);
  *out_byte_length = combined_byte_length;
  return AMDF_STATUS_OK;
}
