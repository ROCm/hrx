// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/xdna/transaction.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include "gtest/gtest.h"

namespace {

constexpr amdf_xdna_transaction_target_t kTarget = {
    /*.device_generation=*/4,
    /*.row_count=*/6,
    /*.column_count=*/8,
    /*.memory_tile_row_count=*/1,
};

void WriteU32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
  bytes[offset + 0] = static_cast<uint8_t>(value);
  bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
  bytes[offset + 2] = static_cast<uint8_t>(value >> 16);
  bytes[offset + 3] = static_cast<uint8_t>(value >> 24);
}

uint32_t ReadU32(const std::vector<uint8_t>& bytes, size_t offset) {
  return static_cast<uint32_t>(bytes[offset + 0]) |
         (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
         (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
         (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

std::vector<uint8_t> MakeTransaction(
    const std::vector<std::vector<uint8_t>>& operations) {
  std::vector<uint8_t> transaction(16, 0);
  transaction[0] = 0;
  transaction[1] = 1;
  transaction[2] = kTarget.device_generation;
  transaction[3] = kTarget.row_count;
  transaction[4] = kTarget.column_count;
  transaction[5] = kTarget.memory_tile_row_count;
  for (const std::vector<uint8_t>& operation : operations) {
    transaction.insert(transaction.end(), operation.begin(), operation.end());
  }
  WriteU32(transaction, 8, static_cast<uint32_t>(operations.size()));
  WriteU32(transaction, 12, static_cast<uint32_t>(transaction.size()));
  return transaction;
}

std::vector<uint8_t> MakeFixedOperation(uint8_t opcode, size_t byte_length) {
  std::vector<uint8_t> operation(byte_length, 0);
  operation[0] = opcode;
  return operation;
}

std::vector<uint8_t> MakeSizedOperation(uint8_t opcode, size_t byte_length,
                                        size_t size_field_offset) {
  std::vector<uint8_t> operation = MakeFixedOperation(opcode, byte_length);
  WriteU32(operation, size_field_offset, static_cast<uint32_t>(byte_length));
  return operation;
}

amdf_status_code_t Validate(const std::vector<uint8_t>& transaction) {
  return amdf_status_code(amdf_xdna_transaction_validate(
      &kTarget, AMDF_XDNA_TRANSACTION_FORMAT_VERSION_0_1, transaction.data(),
      transaction.size()));
}

TEST(XdnaTransactionTest, AcceptsEmptyStream) {
  EXPECT_EQ(Validate(MakeTransaction({})), AMDF_STATUS_CODE_OK);
}

TEST(XdnaTransactionTest, AcceptsEveryStandardOperationShape) {
  const std::vector<uint8_t> transaction = MakeTransaction({
      MakeSizedOperation(0, 24, 20),
      MakeSizedOperation(1, 20, 12),
      MakeFixedOperation(2, 16),
      MakeSizedOperation(3, 28, 24),
      MakeSizedOperation(3, 32, 24),
      MakeSizedOperation(4, 32, 24),
      MakeFixedOperation(5, 4),
      MakeFixedOperation(6, 4),
      MakeSizedOperation(7, 32, 24),
      MakeFixedOperation(8, 16),
      MakeFixedOperation(9, 8),
      MakeFixedOperation(10, 16),
      MakeFixedOperation(11, 8),
      MakeFixedOperation(12, 12),
      MakeFixedOperation(13, 4),
      MakeSizedOperation(128, 16, 4),
      MakeSizedOperation(129, 48, 4),
  });

  EXPECT_EQ(Validate(transaction), AMDF_STATUS_CODE_OK);
}

TEST(XdnaTransactionTest, RejectsWrongEnvelope) {
  std::vector<uint8_t> transaction = MakeTransaction({});

  transaction[0] = 1;
  EXPECT_EQ(Validate(transaction), AMDF_STATUS_CODE_INVALID_ARGUMENT);
  transaction[0] = 0;

  transaction[2] = 3;
  EXPECT_EQ(Validate(transaction), AMDF_STATUS_CODE_INVALID_ARGUMENT);
  transaction[2] = kTarget.device_generation;

  transaction[6] = 1;
  EXPECT_EQ(Validate(transaction), AMDF_STATUS_CODE_INVALID_ARGUMENT);
  transaction[6] = 0;

  WriteU32(transaction, 12, static_cast<uint32_t>(transaction.size() + 4));
  EXPECT_EQ(Validate(transaction), AMDF_STATUS_CODE_INVALID_ARGUMENT);
}

TEST(XdnaTransactionTest, RejectsMalformedOperationStream) {
  std::vector<uint8_t> transaction =
      MakeTransaction({MakeSizedOperation(0, 24, 20)});

  WriteU32(transaction, 8, 2);
  EXPECT_EQ(Validate(transaction), AMDF_STATUS_CODE_INVALID_ARGUMENT);
  WriteU32(transaction, 8, 1);

  transaction[16] = 14;
  EXPECT_EQ(Validate(transaction), AMDF_STATUS_CODE_INVALID_ARGUMENT);
  transaction[16] = 0;

  WriteU32(transaction, 36, 20);
  EXPECT_EQ(Validate(transaction), AMDF_STATUS_CODE_INVALID_ARGUMENT);
  WriteU32(transaction, 36, 28);
  EXPECT_EQ(Validate(transaction), AMDF_STATUS_CODE_INVALID_ARGUMENT);
}

TEST(XdnaTransactionTest, RejectsUnsupportedFormatVersion) {
  const std::vector<uint8_t> transaction = MakeTransaction({});

  EXPECT_EQ(amdf_status_code(amdf_xdna_transaction_validate(
                &kTarget, AMDF_XDNA_TRANSACTION_FORMAT_VERSION_0_1 + 1,
                transaction.data(), transaction.size())),
            AMDF_STATUS_CODE_UNSUPPORTED);
}

TEST(XdnaTransactionTest, ComposesOperationsUnderOneHeader) {
  const std::vector<uint8_t> first = MakeTransaction({
      MakeFixedOperation(5, 4),
      MakeFixedOperation(6, 4),
  });
  const std::vector<uint8_t> second = MakeTransaction({
      MakeFixedOperation(13, 4),
  });
  std::vector<uint8_t> combined(first.size() + second.size(), 0xCD);
  uint64_t combined_byte_length = 0;

  ASSERT_EQ(amdf_status_code(amdf_xdna_transaction_compose(
                first.data(), first.size(), second.data(), second.size(),
                combined.data(), combined.size(), &combined_byte_length)),
            AMDF_STATUS_CODE_OK);
  combined.resize(static_cast<size_t>(combined_byte_length));

  EXPECT_EQ(combined_byte_length, first.size() + second.size() - 16);
  EXPECT_EQ(Validate(combined), AMDF_STATUS_CODE_OK);
  EXPECT_EQ(ReadU32(combined, 8), 3u);
}

TEST(XdnaTransactionTest, RejectsIncompatibleHeadersAndSmallOutput) {
  const std::vector<uint8_t> first = MakeTransaction({});
  std::vector<uint8_t> second = MakeTransaction({});
  std::vector<uint8_t> output(16, 0);
  uint64_t output_byte_length = 0;

  second[2] ^= 1;
  EXPECT_EQ(amdf_status_code(amdf_xdna_transaction_compose(
                first.data(), first.size(), second.data(), second.size(),
                output.data(), output.size(), &output_byte_length)),
            AMDF_STATUS_CODE_INVALID_ARGUMENT);

  second[2] ^= 1;
  EXPECT_EQ(amdf_status_code(amdf_xdna_transaction_compose(
                first.data(), first.size(), second.data(), second.size(),
                output.data(), output.size() - 1, &output_byte_length)),
            AMDF_STATUS_CODE_BUFFER_TOO_SMALL);
}

}  // namespace
