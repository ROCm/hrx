// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/vm/bytecode/assembler_lexer.h"

#include <cstdint>
#include <string>

#include "iree/testing/gtest.h"
#include "iree/testing/status_matchers.h"

namespace {

using ::testing::HasSubstr;

iree_status_t AppendBytes(void* user_data, iree_const_byte_span_t fragment) {
  static_cast<std::string*>(user_data)->append(
      reinterpret_cast<const char*>(fragment.data), fragment.data_length);
  return iree_ok_status();
}

iree_vm_bytecode_assembler_lexer_t MakeLexer(iree_string_view_t source) {
  iree_vm_bytecode_assembler_lexer_t lexer;
  iree_vm_bytecode_assembler_lexer_initialize(source, &lexer);
  return lexer;
}

TEST(AssemblerLexerTest, MatchesLiteralsAcrossWhitespace) {
  auto lexer = MakeLexer(IREE_SV("  vm.module\n  core (0.0)"));
  EXPECT_TRUE(iree_vm_bytecode_assembler_lexer_try_literal(
      &lexer, IREE_SV("vm.module core (0.0)")));
  EXPECT_EQ(lexer.cursor, lexer.end);

  lexer = MakeLexer(IREE_SV("section alignment(8)"));
  EXPECT_TRUE(
      iree_vm_bytecode_assembler_lexer_try_literal(&lexer, IREE_SV("section")));
  EXPECT_TRUE(iree_vm_bytecode_assembler_lexer_try_literal(
      &lexer, IREE_SV(" alignment(")));
  EXPECT_EQ(*lexer.cursor, '8');

  lexer = MakeLexer(IREE_SV("vm.modulecore"));
  EXPECT_FALSE(iree_vm_bytecode_assembler_lexer_try_literal(
      &lexer, IREE_SV("vm.module core")));
  EXPECT_EQ(lexer.cursor, lexer.source.data);
}

TEST(AssemblerLexerTest, ParsesNames) {
  auto lexer = MakeLexer(IREE_SV("  name.with-$parts !"));
  iree_string_view_t name;
  EXPECT_TRUE(iree_vm_bytecode_assembler_lexer_try_name(&lexer, &name));
  EXPECT_TRUE(iree_string_view_equal(name, IREE_SV("name.with-$parts")));
  EXPECT_EQ(*lexer.cursor, ' ');
  EXPECT_FALSE(iree_vm_bytecode_assembler_lexer_try_name(&lexer, &name));

  lexer = MakeLexer(IREE_SV("  name.with-$parts remainder"));
  IREE_EXPECT_OK(iree_vm_bytecode_assembler_lexer_parse_name(&lexer, &name));
  EXPECT_TRUE(iree_string_view_equal(name, IREE_SV("name.with-$parts")));
  EXPECT_EQ(*lexer.cursor, ' ');

  lexer = MakeLexer(IREE_SV("  0name"));
  IREE_EXPECT_STATUS_IS(
      IREE_STATUS_INVALID_ARGUMENT,
      iree_vm_bytecode_assembler_lexer_parse_name(&lexer, &name));
}

TEST(AssemblerLexerTest, ParsesBoundedUnsignedIntegers) {
  auto lexer = MakeLexer(IREE_SV("255"));
  uint64_t value = 0;
  IREE_EXPECT_OK(iree_vm_bytecode_assembler_lexer_parse_unsigned(
      &lexer, 1, IREE_VM_BYTECODE_ASSEMBLER_RADIX_DECIMAL, &value));
  EXPECT_EQ(value, 255u);

  lexer = MakeLexer(IREE_SV("0xCAFE"));
  IREE_EXPECT_OK(iree_vm_bytecode_assembler_lexer_parse_unsigned(
      &lexer, 2, IREE_VM_BYTECODE_ASSEMBLER_RADIX_HEXADECIMAL, &value));
  EXPECT_EQ(value, 0xCAFEu);

  lexer = MakeLexer(IREE_SV("256"));
  IREE_EXPECT_STATUS_IS(
      IREE_STATUS_INVALID_ARGUMENT,
      iree_vm_bytecode_assembler_lexer_parse_unsigned(
          &lexer, 1, IREE_VM_BYTECODE_ASSEMBLER_RADIX_DECIMAL, &value));
}

TEST(AssemblerLexerTest, ParsesSignedIntegerBits) {
  auto lexer = MakeLexer(IREE_SV("-128"));
  uint64_t bits = 0;
  IREE_EXPECT_OK(
      iree_vm_bytecode_assembler_lexer_parse_signed(&lexer, 1, &bits));
  EXPECT_EQ(bits, 0x80u);

  lexer = MakeLexer(IREE_SV("127"));
  IREE_EXPECT_OK(
      iree_vm_bytecode_assembler_lexer_parse_signed(&lexer, 1, &bits));
  EXPECT_EQ(bits, 0x7Fu);

  lexer = MakeLexer(IREE_SV("128"));
  IREE_EXPECT_STATUS_IS(
      IREE_STATUS_INVALID_ARGUMENT,
      iree_vm_bytecode_assembler_lexer_parse_signed(&lexer, 1, &bits));

  lexer = MakeLexer(IREE_SV("- 1"));
  IREE_EXPECT_STATUS_IS(
      IREE_STATUS_INVALID_ARGUMENT,
      iree_vm_bytecode_assembler_lexer_parse_signed(&lexer, 1, &bits));
}

TEST(AssemblerLexerTest, DecodesCanonicalQuotedBytes) {
  auto lexer = MakeLexer(IREE_SV(R"("loom\n\t\\\"\x1F")"));
  std::string decoded;
  uint64_t length = 0;
  IREE_EXPECT_OK(iree_vm_bytecode_assembler_lexer_parse_quoted_string(
      &lexer, AppendBytes, &decoded, &length));
  EXPECT_EQ(decoded, std::string("loom\n\t\\\"\x1F", 9));
  EXPECT_EQ(length, decoded.size());
  EXPECT_EQ(lexer.cursor, lexer.end);

  lexer = MakeLexer(IREE_SV(R"("printable\x41")"));
  IREE_EXPECT_STATUS_IS(IREE_STATUS_INVALID_ARGUMENT,
                        iree_vm_bytecode_assembler_lexer_parse_quoted_string(
                            &lexer, AppendBytes, &decoded, &length));

  lexer = MakeLexer(IREE_SV(R"("nul\x00")"));
  IREE_EXPECT_STATUS_IS(IREE_STATUS_INVALID_ARGUMENT,
                        iree_vm_bytecode_assembler_lexer_parse_quoted_string(
                            &lexer, AppendBytes, &decoded, &length));

  lexer = MakeLexer(IREE_SV(R"("nul\x00")"));
  decoded.clear();
  IREE_EXPECT_OK(iree_vm_bytecode_assembler_lexer_parse_quoted_bytes(
      &lexer, AppendBytes, &decoded, &length));
  EXPECT_EQ(decoded, std::string("nul\0", 4));
  EXPECT_EQ(length, decoded.size());
}

TEST(AssemblerLexerTest, DecodesHexBytes) {
  auto lexer = MakeLexer(IREE_SV(R"(hex"0011CAFE")"));
  std::string decoded;
  uint64_t length = 0;
  IREE_EXPECT_OK(iree_vm_bytecode_assembler_lexer_parse_hex_bytes(
      &lexer, AppendBytes, &decoded, &length));
  EXPECT_EQ(decoded, std::string("\x00\x11\xCA\xFE", 4));
  EXPECT_EQ(length, 4u);

  lexer = MakeLexer(IREE_SV(R"(hex"001")"));
  IREE_EXPECT_STATUS_IS(IREE_STATUS_INVALID_ARGUMENT,
                        iree_vm_bytecode_assembler_lexer_parse_hex_bytes(
                            &lexer, AppendBytes, &decoded, &length));
}

TEST(AssemblerLexerTest, ReportsSourcePosition) {
  auto lexer = MakeLexer(IREE_SV("first\n  ?"));
  lexer.cursor = lexer.source.data + 8;
  iree::Status status =
      iree_vm_bytecode_assembler_lexer_error(&lexer, "test failure");
  EXPECT_THAT(status.ToString(), HasSubstr("VM assembly at 2:3: test failure"));
}

}  // namespace
