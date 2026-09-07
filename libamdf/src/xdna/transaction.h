// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef AMDF_SRC_XDNA_TRANSACTION_H_
#define AMDF_SRC_XDNA_TRANSACTION_H_

#include "amdf/xdna.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Immutable target fields required in every transaction header.
typedef struct amdf_xdna_transaction_target_t {
  // AIE-RT device-generation value encoded in the transaction header.
  uint8_t device_generation;
  // Total row count encoded in the transaction header.
  uint8_t row_count;
  // Total column count encoded in the transaction header.
  uint8_t column_count;
  // Memory-tile row count encoded in the transaction header.
  uint8_t memory_tile_row_count;
} amdf_xdna_transaction_target_t;

// Validates one complete transaction stream for `target` and `format_version`.
amdf_status_t amdf_xdna_transaction_validate(
    const amdf_xdna_transaction_target_t* target, uint32_t format_version,
    const void* bytes, uint64_t byte_length);

// Composes two validated transaction streams into caller-owned storage.
amdf_status_t amdf_xdna_transaction_compose(
    const void* first_bytes, uint64_t first_byte_length,
    const void* second_bytes, uint64_t second_byte_length, void* output_bytes,
    uint64_t output_capacity, uint64_t* out_byte_length);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // AMDF_SRC_XDNA_TRANSACTION_H_
