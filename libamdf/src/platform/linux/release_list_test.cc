// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/platform/linux/release_list.h"

#include <atomic>
#include <thread>
#include <vector>

#include "gtest/gtest.h"

namespace {

// A fallible native release dependency. The list itself is the production code.
struct NativeAllocation {
  // Intrusive ownership transferred to the parent when rollback fails.
  amdf_linux_release_t release = {};
  // Test-controlled native ownership condition preventing release.
  bool* blocked = nullptr;
  // External destruction count surviving the allocation's deletion.
  std::atomic<size_t>* destroyed = nullptr;

  static amdf_status_t Destroy(amdf_linux_release_t* release) {
    auto* allocation = reinterpret_cast<NativeAllocation*>(release);
    if (allocation->blocked && *allocation->blocked) {
      return amdf_make_api_status(AMDF_STATUS_CODE_BUSY);
    }
    ++*allocation->destroyed;
    delete allocation;
    return AMDF_STATUS_OK;
  }
};

TEST(LinuxReleaseListTest, FailedReleaseRetainsRemainingOwnership) {
  amdf_linux_release_list_t list = {};
  std::atomic<size_t> destroyed{0};
  bool blocked = true;
  for (size_t i = 0; i < 3; ++i) {
    auto* allocation = new NativeAllocation;
    allocation->release.destroy = NativeAllocation::Destroy;
    allocation->destroyed = &destroyed;
    if (i == 1) allocation->blocked = &blocked;
    amdf_linux_release_list_push(&list, &allocation->release);
  }
  EXPECT_EQ(amdf_linux_release_list_drain(&list),
            amdf_make_api_status(AMDF_STATUS_CODE_BUSY));
  EXPECT_EQ(destroyed.load(), 1u);
  blocked = false;
  EXPECT_EQ(amdf_linux_release_list_drain(&list), AMDF_STATUS_OK);
  EXPECT_EQ(destroyed.load(), 3u);
  EXPECT_EQ(amdf_linux_release_list_drain(&list), AMDF_STATUS_OK);
  EXPECT_EQ(destroyed.load(), 3u);
}

TEST(LinuxReleaseListTest, ConcurrentConstructionFailuresHaveOneOwner) {
  amdf_linux_release_list_t list = {};
  std::atomic<size_t> destroyed{0};
  std::vector<std::thread> producers;
  for (size_t i = 0; i < 4; ++i) {
    producers.emplace_back([&] {
      for (size_t j = 0; j < 64; ++j) {
        auto* allocation = new NativeAllocation;
        allocation->release.destroy = NativeAllocation::Destroy;
        allocation->destroyed = &destroyed;
        amdf_linux_release_list_push(&list, &allocation->release);
      }
    });
  }
  for (auto& producer : producers) producer.join();
  EXPECT_EQ(destroyed.load(), 0u);
  EXPECT_EQ(amdf_linux_release_list_drain(&list), AMDF_STATUS_OK);
  EXPECT_EQ(destroyed.load(), 256u);
}

}  // namespace
