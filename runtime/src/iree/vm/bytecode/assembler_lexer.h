// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef IREE_VM_BYTECODE_ASSEMBLER_LEXER_H_
#define IREE_VM_BYTECODE_ASSEMBLER_LEXER_H_

#include "iree/base/api.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Canonical integer spelling accepted by the assembler lexer.
typedef enum iree_vm_bytecode_assembler_radix_t {
  IREE_VM_BYTECODE_ASSEMBLER_RADIX_DECIMAL = 0,
  IREE_VM_BYTECODE_ASSEMBLER_RADIX_HEXADECIMAL = 1,
} iree_vm_bytecode_assembler_radix_t;

// Cursor over one complete borrowed VM assembly source.
typedef struct iree_vm_bytecode_assembler_lexer_t {
  // Complete source used to report line and column diagnostics.
  iree_string_view_t source;
  // Current parse cursor within |source|.
  const char* cursor;
  // One-past-end parser bound.
  const char* end;
} iree_vm_bytecode_assembler_lexer_t;

// Callback receiving decoded fragments of quoted or hexadecimal byte syntax.
// The fragment remains valid only for the callback duration.
typedef iree_status_t(
    IREE_API_PTR* iree_vm_bytecode_assembler_lexer_write_fn_t)(
    void* user_data, iree_const_byte_span_t fragment);

// Initializes |out_lexer| at the beginning of |source|.
void iree_vm_bytecode_assembler_lexer_initialize(
    iree_string_view_t source, iree_vm_bytecode_assembler_lexer_t* out_lexer);

// Returns an INVALID_ARGUMENT diagnostic at the current source position.
iree_status_t iree_vm_bytecode_assembler_lexer_error(
    const iree_vm_bytecode_assembler_lexer_t* lexer,
    const char* message) IREE_ATTRIBUTE_COLD IREE_ATTRIBUTE_NOINLINE;

// Skips ASCII whitespace at the current position.
void iree_vm_bytecode_assembler_lexer_skip_space(
    iree_vm_bytecode_assembler_lexer_t* lexer);

// Tries to consume |literal|, treating each whitespace run as one or more
// source whitespace bytes. Leading source whitespace is permitted unless it
// is already required by a leading whitespace run in |literal|.
bool iree_vm_bytecode_assembler_lexer_try_literal(
    iree_vm_bytecode_assembler_lexer_t* lexer, iree_string_view_t literal);

// Tries to parse one symbolic name and returns a borrowed source view.
// Leaves the cursor unchanged when no name begins at the current position.
bool iree_vm_bytecode_assembler_lexer_try_name(
    iree_vm_bytecode_assembler_lexer_t* lexer, iree_string_view_t* out_name);

// Parses one symbolic name and returns a view borrowed from the source.
iree_status_t iree_vm_bytecode_assembler_lexer_parse_name(
    iree_vm_bytecode_assembler_lexer_t* lexer, iree_string_view_t* out_name);

// Parses an unsigned integer that fits exactly within |width| bytes.
iree_status_t iree_vm_bytecode_assembler_lexer_parse_unsigned(
    iree_vm_bytecode_assembler_lexer_t* lexer, uint8_t width,
    iree_vm_bytecode_assembler_radix_t radix, uint64_t* out_value);

// Parses a decimal two's-complement integer fitting within |width| bytes and
// returns its physical bits.
iree_status_t iree_vm_bytecode_assembler_lexer_parse_signed(
    iree_vm_bytecode_assembler_lexer_t* lexer, uint8_t width,
    uint64_t* out_bits);

// Parses canonical quoted NUL-free UTF-8 bytes. When |write_fn| is NULL bytes
// are validated and counted without being produced.
iree_status_t iree_vm_bytecode_assembler_lexer_parse_quoted_string(
    iree_vm_bytecode_assembler_lexer_t* lexer,
    iree_vm_bytecode_assembler_lexer_write_fn_t write_fn, void* user_data,
    uint64_t* out_length);

// Parses canonical quoted UTF-8 bytes that may include NUL. When |write_fn| is
// NULL bytes are validated and counted without being produced.
iree_status_t iree_vm_bytecode_assembler_lexer_parse_quoted_bytes(
    iree_vm_bytecode_assembler_lexer_t* lexer,
    iree_vm_bytecode_assembler_lexer_write_fn_t write_fn, void* user_data,
    uint64_t* out_length);

// Parses a hexadecimal byte span. When |write_fn| is NULL bytes are validated
// and counted without being produced.
iree_status_t iree_vm_bytecode_assembler_lexer_parse_hex_bytes(
    iree_vm_bytecode_assembler_lexer_t* lexer,
    iree_vm_bytecode_assembler_lexer_write_fn_t write_fn, void* user_data,
    uint64_t* out_length);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // IREE_VM_BYTECODE_ASSEMBLER_LEXER_H_
