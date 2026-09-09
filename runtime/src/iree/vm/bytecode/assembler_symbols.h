// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef IREE_VM_BYTECODE_ASSEMBLER_SYMBOLS_H_
#define IREE_VM_BYTECODE_ASSEMBLER_SYMBOLS_H_

#include "iree/base/api.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// One declaration in a module or function-local textual symbol domain.
typedef struct iree_vm_bytecode_assembler_symbol_t {
  // Symbol spelling borrowed from the source text.
  iree_string_view_t name;
  // Function-local scope or zero for module declarations.
  uint32_t scope;
  // Encoded ordinal assigned by declaration order.
  uint32_t ordinal;
  // Domain identifying the encoded ordinal space.
  uint8_t domain;
  // Declaration-specific flags used by composite references.
  uint16_t flags;
  // Domain-specific value such as a function-relative block byte offset.
  uint32_t value;
} iree_vm_bytecode_assembler_symbol_t;

// Physical patch behavior for one symbolic or synthetic reference.
typedef enum iree_vm_bytecode_assembler_fixup_kind_t {
  IREE_VM_BYTECODE_ASSEMBLER_FIXUP_ORDINAL = 0,
  IREE_VM_BYTECODE_ASSEMBLER_FIXUP_BLOCK_DISPLACEMENT,
  IREE_VM_BYTECODE_ASSEMBLER_FIXUP_SWITCH_TARGET,
  IREE_VM_BYTECODE_ASSEMBLER_FIXUP_SWITCH_SLICE,
  IREE_VM_BYTECODE_ASSEMBLER_FIXUP_DIRECT_TARGET,
  IREE_VM_BYTECODE_ASSEMBLER_FIXUP_OPTIONAL_IMPORT,
  IREE_VM_BYTECODE_ASSEMBLER_FIXUP_GLOBAL_PARTITION,
  IREE_VM_BYTECODE_ASSEMBLER_FIXUP_RELATION,
} iree_vm_bytecode_assembler_fixup_kind_t;

// One deferred physical field patch produced while parsing source text.
typedef struct iree_vm_bytecode_assembler_fixup_t {
  // Referenced spelling borrowed from the source text, if symbolic.
  iree_string_view_t name;
  // Absolute stream offset of the primary field to patch.
  uint64_t output_offset;
  // Function-local scope or zero for module declarations.
  uint32_t scope;
  // Value carried by synthetic references with no declaration.
  uint32_t value;
  // Symbol domain required by the reference.
  uint8_t domain;
  // Number of bytes in the primary encoded field.
  uint8_t width;
  // One iree_vm_bytecode_assembler_fixup_kind_t value.
  uint8_t kind;
  // Byte offset from the primary field to a related encoded field.
  int8_t related_delta;
  // Number of bytes in the related encoded field, or zero when absent.
  uint8_t related_width;
  // Record-end or section-prefix value used by relative fixups.
  uint32_t base;
  // Base row in a flattened table used by composite ordinals.
  uint32_t ordinal_base;
} iree_vm_bytecode_assembler_fixup_t;

// Exact two-pass storage for source symbols and deferred fixups.
//
// A counting instance has NULL storage and only accumulates counts. A recording
// instance writes the same source sequence into caller-provided exact storage.
// Symbol names remain borrowed from the immutable source through resolution.
typedef struct iree_vm_bytecode_assembler_symbols_t {
  // Caller-provided symbol storage, or NULL during the counting pass.
  iree_vm_bytecode_assembler_symbol_t* symbols;
  // Exact number of entries available in |symbols|.
  iree_host_size_t symbol_capacity;
  // Number of symbol declarations observed in the active pass.
  iree_host_size_t symbol_count;
  // Caller-provided fixup storage, or NULL during the counting pass.
  iree_vm_bytecode_assembler_fixup_t* fixups;
  // Exact number of entries available in |fixups|.
  iree_host_size_t fixup_capacity;
  // Number of deferred fixups observed in the active pass.
  iree_host_size_t fixup_count;
} iree_vm_bytecode_assembler_symbols_t;

// Initializes |out_symbols| to count declarations and references without
// retaining them.
void iree_vm_bytecode_assembler_symbols_initialize_counting(
    iree_vm_bytecode_assembler_symbols_t* out_symbols);

// Initializes |out_symbols| to record declarations and references into exact
// caller-provided storage sized by a preceding counting pass.
void iree_vm_bytecode_assembler_symbols_initialize_recording(
    iree_host_size_t symbol_capacity,
    iree_vm_bytecode_assembler_symbol_t* symbol_storage,
    iree_host_size_t fixup_capacity,
    iree_vm_bytecode_assembler_fixup_t* fixup_storage,
    iree_vm_bytecode_assembler_symbols_t* out_symbols);

// Adds one declaration to the active counting or recording pass.
void iree_vm_bytecode_assembler_symbols_add_symbol(
    iree_vm_bytecode_assembler_symbols_t* symbols,
    const iree_vm_bytecode_assembler_symbol_t* symbol);

// Adds one deferred reference to the active counting or recording pass.
void iree_vm_bytecode_assembler_symbols_add_fixup(
    iree_vm_bytecode_assembler_symbols_t* symbols,
    const iree_vm_bytecode_assembler_fixup_t* fixup);

// Sorts recorded declarations for lookup and rejects duplicate names within a
// scope and domain. Must be called after a successful exact recording pass.
iree_status_t iree_vm_bytecode_assembler_symbols_prepare(
    iree_vm_bytecode_assembler_symbols_t* symbols);

// Looks up a prepared declaration by its exact scope, domain, and spelling.
// Returns NULL when no declaration matches.
const iree_vm_bytecode_assembler_symbol_t*
iree_vm_bytecode_assembler_symbols_lookup(
    const iree_vm_bytecode_assembler_symbols_t* symbols, uint32_t scope,
    uint8_t domain, iree_string_view_t name);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // IREE_VM_BYTECODE_ASSEMBLER_SYMBOLS_H_
