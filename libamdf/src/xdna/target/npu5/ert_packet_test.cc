// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/xdna/target/npu5/ert_packet.h"

#include <array>
#include <cstring>

#include "gtest/gtest.h"

namespace {

TEST(Npu5ErtPacketTest, EncodesTransactionInterpreterCallingConvention) {
  const uint64_t addresses[] = {UINT64_C(0x1122334455667788), 0x2000, 0x3000,
                                0x4000, UINT64_C(0xffeeddccbbaa9988)};
  amdf_xdna_npu5_ert_packet_t packet;
  amdf_xdna_npu5_ert_packet_build(0x04008000, 300, addresses, 5, &packet);
  const std::array<uint8_t, 68> expected = {
      0x01, 0x00, 0x01, 0x30, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00,
      0x4b, 0x00, 0x00, 0x00, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11,
      0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
  EXPECT_EQ(std::memcmp(packet.bytes, expected.data(), expected.size()), 0);
}

TEST(Npu5ErtPacketTest, ClearsUnusedBindingsAndReservedBytes) {
  amdf_xdna_npu5_ert_packet_t packet;
  std::memset(&packet, 0xa5, sizeof(packet));
  amdf_xdna_npu5_ert_packet_build(0x04010000, 20, nullptr, 0, &packet);
  for (size_t i = 12; i < 16; ++i) EXPECT_EQ(packet.bytes[i], 0);
  for (size_t i = 28; i < sizeof(packet); ++i) EXPECT_EQ(packet.bytes[i], 0);
}

}  // namespace
