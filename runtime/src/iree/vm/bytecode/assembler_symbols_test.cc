// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/vm/bytecode/assembler_symbols.h"

#include "iree/testing/gtest.h"
#include "iree/testing/status_matchers.h"

namespace {

TEST(AssemblerSymbolsTest, CountsWithoutStorage) {
  iree_vm_bytecode_assembler_symbols_t symbols;
  iree_vm_bytecode_assembler_symbols_initialize_counting(&symbols);
  const iree_vm_bytecode_assembler_symbol_t symbol = {
      IREE_SV("foo"), 0, 3, 1, 0, 0};
  const iree_vm_bytecode_assembler_fixup_t fixup = {};
  iree_vm_bytecode_assembler_symbols_add_symbol(&symbols, &symbol);
  iree_vm_bytecode_assembler_symbols_add_fixup(&symbols, &fixup);
  EXPECT_EQ(symbols.symbol_count, 1u);
  EXPECT_EQ(symbols.fixup_count, 1u);
}

TEST(AssemblerSymbolsTest, SortsAndLooksUpScopedDomains) {
  iree_vm_bytecode_assembler_symbol_t symbol_storage[4];
  iree_vm_bytecode_assembler_symbols_t symbols;
  iree_vm_bytecode_assembler_symbols_initialize_recording(
      IREE_ARRAYSIZE(symbol_storage), symbol_storage, 0, nullptr, &symbols);
  const iree_vm_bytecode_assembler_symbol_t inputs[] = {
      {IREE_SV("beta"), 1, 2, 3, 0, 20},
      {IREE_SV("alpha"), 0, 0, 3, 0, 10},
      {IREE_SV("alpha"), 1, 1, 3, 0, 11},
      {IREE_SV("alpha"), 1, 4, 2, 0, 12},
  };
  for (const auto& input : inputs) {
    iree_vm_bytecode_assembler_symbols_add_symbol(&symbols, &input);
  }
  IREE_ASSERT_OK(iree_vm_bytecode_assembler_symbols_prepare(&symbols));

  const auto* match = iree_vm_bytecode_assembler_symbols_lookup(
      &symbols, 1, 3, IREE_SV("alpha"));
  ASSERT_NE(match, nullptr);
  EXPECT_EQ(match->ordinal, 1u);
  EXPECT_EQ(match->value, 11u);
  EXPECT_EQ(iree_vm_bytecode_assembler_symbols_lookup(&symbols, 0, 2,
                                                      IREE_SV("alpha")),
            nullptr);
}

TEST(AssemblerSymbolsTest, RejectsDuplicateScopedDomainName) {
  iree_vm_bytecode_assembler_symbol_t symbol_storage[2];
  iree_vm_bytecode_assembler_symbols_t symbols;
  iree_vm_bytecode_assembler_symbols_initialize_recording(
      IREE_ARRAYSIZE(symbol_storage), symbol_storage, 0, nullptr, &symbols);
  const iree_vm_bytecode_assembler_symbol_t symbol = {
      IREE_SV("same"), 2, 0, 7, 0, 0};
  iree_vm_bytecode_assembler_symbols_add_symbol(&symbols, &symbol);
  iree_vm_bytecode_assembler_symbols_add_symbol(&symbols, &symbol);
  IREE_EXPECT_STATUS_IS(IREE_STATUS_INVALID_ARGUMENT,
                        iree_vm_bytecode_assembler_symbols_prepare(&symbols));
}

}  // namespace
