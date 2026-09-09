// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/vm/bytecode/assembler_symbols.h"

#include <stdlib.h>
#include <string.h>

static int iree_vm_bytecode_assembler_symbol_compare(const void* lhs_ptr,
                                                     const void* rhs_ptr) {
  const iree_vm_bytecode_assembler_symbol_t* lhs =
      (const iree_vm_bytecode_assembler_symbol_t*)lhs_ptr;
  const iree_vm_bytecode_assembler_symbol_t* rhs =
      (const iree_vm_bytecode_assembler_symbol_t*)rhs_ptr;
  if (lhs->scope != rhs->scope) return lhs->scope < rhs->scope ? -1 : 1;
  if (lhs->domain != rhs->domain) return lhs->domain < rhs->domain ? -1 : 1;
  return iree_string_view_compare(lhs->name, rhs->name);
}

void iree_vm_bytecode_assembler_symbols_initialize_counting(
    iree_vm_bytecode_assembler_symbols_t* out_symbols) {
  memset(out_symbols, 0, sizeof(*out_symbols));
}

void iree_vm_bytecode_assembler_symbols_initialize_recording(
    iree_host_size_t symbol_capacity,
    iree_vm_bytecode_assembler_symbol_t* symbol_storage,
    iree_host_size_t fixup_capacity,
    iree_vm_bytecode_assembler_fixup_t* fixup_storage,
    iree_vm_bytecode_assembler_symbols_t* out_symbols) {
  out_symbols->symbols = symbol_storage;
  out_symbols->symbol_capacity = symbol_capacity;
  out_symbols->symbol_count = 0;
  out_symbols->fixups = fixup_storage;
  out_symbols->fixup_capacity = fixup_capacity;
  out_symbols->fixup_count = 0;
}

void iree_vm_bytecode_assembler_symbols_add_symbol(
    iree_vm_bytecode_assembler_symbols_t* symbols,
    const iree_vm_bytecode_assembler_symbol_t* symbol) {
  if (symbols->symbols) {
    IREE_ASSERT(symbols->symbol_count < symbols->symbol_capacity);
    symbols->symbols[symbols->symbol_count] = *symbol;
  }
  ++symbols->symbol_count;
}

void iree_vm_bytecode_assembler_symbols_add_fixup(
    iree_vm_bytecode_assembler_symbols_t* symbols,
    const iree_vm_bytecode_assembler_fixup_t* fixup) {
  if (symbols->fixups) {
    IREE_ASSERT(symbols->fixup_count < symbols->fixup_capacity);
    symbols->fixups[symbols->fixup_count] = *fixup;
  }
  ++symbols->fixup_count;
}

iree_status_t iree_vm_bytecode_assembler_symbols_prepare(
    iree_vm_bytecode_assembler_symbols_t* symbols) {
  IREE_ASSERT(symbols->symbols || symbols->symbol_count == 0);
  IREE_ASSERT(symbols->symbol_count == symbols->symbol_capacity);
  IREE_ASSERT(symbols->fixups || symbols->fixup_count == 0);
  IREE_ASSERT(symbols->fixup_count == symbols->fixup_capacity);
  if (symbols->symbol_count > 1) {
    qsort(symbols->symbols, symbols->symbol_count, sizeof(symbols->symbols[0]),
          iree_vm_bytecode_assembler_symbol_compare);
  }
  for (iree_host_size_t i = 1; i < symbols->symbol_count; ++i) {
    if (iree_vm_bytecode_assembler_symbol_compare(&symbols->symbols[i - 1],
                                                  &symbols->symbols[i]) == 0) {
      return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                              "duplicate VM assembly symbol declaration");
    }
  }
  return iree_ok_status();
}

const iree_vm_bytecode_assembler_symbol_t*
iree_vm_bytecode_assembler_symbols_lookup(
    const iree_vm_bytecode_assembler_symbols_t* symbols, uint32_t scope,
    uint8_t domain, iree_string_view_t name) {
  const iree_vm_bytecode_assembler_symbol_t key = {
      .name = name,
      .scope = scope,
      .domain = domain,
  };
  iree_host_size_t lower = 0;
  iree_host_size_t upper = symbols->symbol_count;
  while (lower < upper) {
    const iree_host_size_t middle = lower + (upper - lower) / 2;
    const iree_vm_bytecode_assembler_symbol_t* candidate =
        &symbols->symbols[middle];
    const int comparison =
        iree_vm_bytecode_assembler_symbol_compare(&key, candidate);
    if (comparison < 0) {
      upper = middle;
    } else if (comparison > 0) {
      lower = middle + 1;
    } else {
      return candidate;
    }
  }
  return NULL;
}
