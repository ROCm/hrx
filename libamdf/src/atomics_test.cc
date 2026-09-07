// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/atomics.h"

#include <cstdint>

#include "gtest/gtest.h"

namespace {

TEST(AtomicUint32Test, InitializesAndLoads) {
  amdf_atomic_uint32_t atomic;
  amdf_atomic_uint32_initialize(&atomic, UINT32_MAX);

  EXPECT_EQ(amdf_atomic_uint32_load_relaxed(&atomic), UINT32_MAX);
  EXPECT_EQ(amdf_atomic_uint32_load_acquire(&atomic), UINT32_MAX);
}

TEST(AtomicUint32Test, CompareExchangeUpdatesValueOnSuccess) {
  amdf_atomic_uint32_t atomic;
  amdf_atomic_uint32_initialize(&atomic, UINT32_MAX);

  uint32_t expected = UINT32_MAX;
  EXPECT_TRUE(
      amdf_atomic_uint32_compare_exchange_acq_rel(&atomic, &expected, 0));
  EXPECT_EQ(expected, UINT32_MAX);
  EXPECT_EQ(amdf_atomic_uint32_load_acquire(&atomic), 0u);
}

TEST(AtomicUint32Test, CompareExchangeUpdatesExpectedOnFailure) {
  amdf_atomic_uint32_t atomic;
  amdf_atomic_uint32_initialize(&atomic, 1);

  uint32_t expected = 0;
  EXPECT_FALSE(
      amdf_atomic_uint32_compare_exchange_acq_rel(&atomic, &expected, 2));
  EXPECT_EQ(expected, 1u);
  EXPECT_EQ(amdf_atomic_uint32_load_acquire(&atomic), 1u);
}

}  // namespace
