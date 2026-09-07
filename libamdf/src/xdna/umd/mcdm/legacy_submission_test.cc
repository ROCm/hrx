// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/xdna/umd/mcdm/legacy_submission.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "gtest/gtest.h"

namespace {

uint32_t ReadU32(const void* bytes, size_t offset) {
  uint32_t value = 0;
  std::memcpy(&value, static_cast<const uint8_t*>(bytes) + offset,
              sizeof(value));
  return value;
}

uint64_t ReadU64(const void* bytes, size_t offset) {
  uint64_t value = 0;
  std::memcpy(&value, static_cast<const uint8_t*>(bytes) + offset,
              sizeof(value));
  return value;
}

TEST(WindowsXdnaLegacySubmissionTest, BuildsContextLifecycleRecords) {
  std::array<uint8_t, 4096> command_bytes = {};
  for (size_t i = 0; i < 512; ++i) {
    command_bytes[i] = static_cast<uint8_t>(i);
  }
  amdf_windows_xdna_private_allocation_t instruction = {};
  instruction.allocation = 0x10;
  instruction.descriptor.allocation_byte_length = UINT64_C(0x4000000);
  amdf_windows_xdna_private_allocation_t command = {};
  command.allocation = 0x20;
  command.host_pointer = command_bytes.data();

  amdf_windows_xdna_legacy_submission_t aperture = {};
  amdf_windows_xdna_legacy_submission_build_aperture(&instruction, &aperture);
  EXPECT_EQ(aperture.byte_length, 104u);
  EXPECT_EQ(ReadU64(aperture.bytes, 0x00), 2u);
  EXPECT_EQ(ReadU64(aperture.bytes, 0x08), 0x10u);
  EXPECT_EQ(ReadU64(aperture.bytes, 0x10), UINT64_C(0x4000000));

  amdf_windows_xdna_legacy_submission_t initialize = {};
  amdf_windows_xdna_legacy_submission_build_context_initialize(&command,
                                                               &initialize);
  EXPECT_EQ(initialize.byte_length, 624u);
  EXPECT_EQ(ReadU64(initialize.bytes, 0x00), 5u);
  EXPECT_EQ(ReadU64(initialize.bytes, 0x28), 0x20u);
  EXPECT_EQ(ReadU32(initialize.bytes, 0x30), 0u);
  EXPECT_EQ(ReadU32(initialize.bytes, 0x34), 8u);
  EXPECT_EQ(ReadU64(initialize.bytes, 0x38),
            reinterpret_cast<uintptr_t>(command_bytes.data()));
  EXPECT_EQ(std::memcmp(initialize.bytes + 104, command_bytes.data(), 512), 0);

  amdf_windows_xdna_legacy_submission_t watermark = {};
  amdf_windows_xdna_legacy_submission_build_watermark(
      &instruction, UINT64_C(0x20000), &watermark);
  EXPECT_EQ(watermark.byte_length, 104u);
  EXPECT_EQ(ReadU64(watermark.bytes, 0x00), 9u);
  EXPECT_EQ(ReadU64(watermark.bytes, 0x08), 0x10u);
  EXPECT_EQ(ReadU64(watermark.bytes, 0x10), UINT64_C(0x20000));
}

TEST(WindowsXdnaLegacySubmissionTest, BuildsFixedBindingExecutionRecords) {
  const std::array<uint64_t, 5> binding_addresses = {
      UINT64_C(0x1000), UINT64_C(0x2000), UINT64_C(0x3000), UINT64_C(0x4000),
      UINT64_C(0x5000)};
  amdf_windows_xdna_legacy_ert_packet_t packet = {};
  ASSERT_TRUE(amdf_status_is_ok(amdf_windows_xdna_legacy_ert_packet_build(
      UINT64_C(0x04008000), 300, binding_addresses.data(),
      static_cast<uint32_t>(binding_addresses.size()), &packet)));
  EXPECT_EQ(ReadU32(packet.bytes, 0x00), 0x30010001u);
  EXPECT_EQ(ReadU32(packet.bytes, 0x04), 1u);
  EXPECT_EQ(ReadU32(packet.bytes, 0x08), 3u);
  EXPECT_EQ(ReadU64(packet.bytes, 0x10), UINT64_C(0x04008000));
  EXPECT_EQ(ReadU32(packet.bytes, 0x18), 75u);
  for (size_t i = 0; i < binding_addresses.size(); ++i) {
    EXPECT_EQ(ReadU64(packet.bytes, 0x1C + i * sizeof(uint64_t)),
              binding_addresses[i]);
  }

  std::array<uint8_t, 4096> command_bytes = {};
  amdf_windows_xdna_private_allocation_t execution = {};
  execution.allocation = 0x30;
  amdf_windows_xdna_private_allocation_t command = {};
  command.allocation = 0x20;
  command.host_pointer = command_bytes.data();
  amdf_windows_xdna_legacy_submission_t submission = {};
  amdf_windows_xdna_legacy_submission_build_execute(&execution, &command,
                                                    &packet, &submission);
  EXPECT_EQ(submission.byte_length, 616u);
  EXPECT_EQ(ReadU64(submission.bytes, 0x00), 3u);
  EXPECT_EQ(ReadU64(submission.bytes, 0x08), 0x30u);
  EXPECT_EQ(ReadU64(submission.bytes, 0x10), 68u);
  EXPECT_EQ(ReadU64(submission.bytes, 0x28), 0x20u);
  EXPECT_EQ(ReadU32(submission.bytes, 0x30), 8u);
  EXPECT_EQ(ReadU32(submission.bytes, 0x34), 8u);
  EXPECT_EQ(ReadU64(submission.bytes, 0x38),
            reinterpret_cast<uintptr_t>(command_bytes.data() + 8));
  EXPECT_EQ(
      std::memcmp(submission.bytes + 104, packet.bytes, sizeof(packet.bytes)),
      0);
}

TEST(WindowsXdnaLegacySubmissionTest, RejectsUnrepresentablePackets) {
  amdf_windows_xdna_legacy_ert_packet_t packet = {};
  EXPECT_EQ(amdf_status_code(amdf_windows_xdna_legacy_ert_packet_build(
                UINT64_C(0x04008000), 3, nullptr, 0, &packet)),
            AMDF_STATUS_CODE_INVALID_ARGUMENT);
  EXPECT_EQ(amdf_status_code(amdf_windows_xdna_legacy_ert_packet_build(
                UINT64_C(0x04008000), 4, nullptr, 1, &packet)),
            AMDF_STATUS_CODE_INVALID_ARGUMENT);
}

}  // namespace
