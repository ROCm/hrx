// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/xdna/umd/memory.h"

#include <cstdint>
#include <vector>

#include "gtest/gtest.h"
#include "libamdf/src/xdna/umd/mcdm/device.h"

namespace {

constexpr NTSTATUS kStatusPending = static_cast<NTSTATUS>(0x00000103u);
constexpr NTSTATUS kStatusNoMemory = static_cast<NTSTATUS>(0xC0000017u);

enum class FailurePoint {
  kNone,
  kCreate,
  kMap,
  kInvalidMapAddress,
  kFirstWait,
  kResident,
  kPartialResident,
  kSecondWait,
  kDestroy,
};

enum class Operation {
  kCreate,
  kMap,
  kWait,
  kResident,
  kDestroy,
};

struct FakeKmtState {
  FailurePoint failure_point = FailurePoint::kNone;
  uint32_t destroy_failures_remaining = 0;
  uint32_t wait_count = 0;
  D3DDDI_MAKERESIDENT_FLAGS resident_flags = {};
  std::vector<Operation> operations;
};

FakeKmtState* g_fake_state = nullptr;

NTSTATUS APIENTRY FakeCreateAllocation(D3DKMT_CREATEALLOCATION* create) {
  g_fake_state->operations.push_back(Operation::kCreate);
  if (g_fake_state->failure_point == FailurePoint::kCreate) {
    return kStatusNoMemory;
  }
  EXPECT_EQ(create->Flags.StandardAllocation, 1u);
  EXPECT_EQ(create->Flags.ExistingSysMem, 1u);
  EXPECT_EQ(create->NumAllocations, 1u);
  EXPECT_NE(create->pAllocationInfo2[0].pSystemMem, nullptr);
  create->pAllocationInfo2[0].hAllocation = 0x20;
  return 0;
}

NTSTATUS APIENTRY
FakeDestroyAllocation(const D3DKMT_DESTROYALLOCATION2* destroy) {
  g_fake_state->operations.push_back(Operation::kDestroy);
  if (g_fake_state->destroy_failures_remaining != 0) {
    --g_fake_state->destroy_failures_remaining;
    return kStatusNoMemory;
  }
  if (g_fake_state->failure_point == FailurePoint::kDestroy) {
    return kStatusNoMemory;
  }
  EXPECT_EQ(destroy->hDevice, 0x10u);
  EXPECT_EQ(destroy->hResource, 0u);
  EXPECT_EQ(destroy->AllocationCount, 1u);
  EXPECT_EQ(destroy->phAllocationList[0], 0x20u);
  EXPECT_EQ(destroy->Flags.AssumeNotInUse, 1u);
  return 0;
}

NTSTATUS APIENTRY FakeMapGpuVirtualAddress(D3DDDI_MAPGPUVIRTUALADDRESS* map) {
  g_fake_state->operations.push_back(Operation::kMap);
  if (g_fake_state->failure_point == FailurePoint::kMap) {
    return kStatusNoMemory;
  }
  EXPECT_EQ(map->hPagingQueue, 0x30u);
  EXPECT_EQ(map->hAllocation, 0x20u);
  EXPECT_EQ(map->SizeInPages, 16u);
  EXPECT_EQ(map->Protection.Write, 1u);
  map->VirtualAddress =
      g_fake_state->failure_point == FailurePoint::kInvalidMapAddress
          ? UINT64_C(0x12340001)
          : UINT64_C(0x12340000);
  map->PagingFenceValue = 1;
  return kStatusPending;
}

NTSTATUS APIENTRY FakeMakeResident(D3DDDI_MAKERESIDENT* resident) {
  g_fake_state->operations.push_back(Operation::kResident);
  g_fake_state->resident_flags = resident->Flags;
  if (g_fake_state->failure_point == FailurePoint::kResident) {
    return kStatusNoMemory;
  }
  EXPECT_EQ(resident->hPagingQueue, 0x30u);
  EXPECT_EQ(resident->NumAllocations, 1u);
  EXPECT_EQ(resident->AllocationList[0], 0x20u);
  if (g_fake_state->failure_point == FailurePoint::kPartialResident) {
    resident->NumAllocations = 0;
  }
  resident->PagingFenceValue = 2;
  return kStatusPending;
}

NTSTATUS APIENTRY
FakeWaitFromCpu(const D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU* wait) {
  g_fake_state->operations.push_back(Operation::kWait);
  ++g_fake_state->wait_count;
  EXPECT_EQ(wait->hDevice, 0x10u);
  EXPECT_EQ(wait->ObjectCount, 1u);
  EXPECT_EQ(wait->ObjectHandleArray[0], 0x40u);
  EXPECT_EQ(wait->FenceValueArray[0], g_fake_state->wait_count);
  if ((g_fake_state->failure_point == FailurePoint::kFirstWait &&
       g_fake_state->wait_count == 1) ||
      (g_fake_state->failure_point == FailurePoint::kSecondWait &&
       g_fake_state->wait_count == 2)) {
    return kStatusNoMemory;
  }
  return 0;
}

class WindowsXdnaMemoryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    g_fake_state = &state_;
    kmt_.create_allocation = FakeCreateAllocation;
    kmt_.destroy_allocation = FakeDestroyAllocation;
    kmt_.map_gpu_virtual_address = FakeMapGpuVirtualAddress;
    kmt_.make_resident = FakeMakeResident;
    kmt_.wait_from_cpu = FakeWaitFromCpu;
    device_.kmt = &kmt_;
    device_.device = 0x10;
    device_.paging_queue = 0x30;
    device_.paging_sync_object = 0x40;
    device_.paging_fence = &paging_fence_;
    InitializeSRWLock(&device_.deferred_memory_release_lock);

    create_info_.type = AMDF_STRUCTURE_TYPE_MEMORY_CREATE_INFO;
    create_info_.structure_size = sizeof(create_info_);
    create_info_.memory_class = AMDF_MEMORY_CLASS_SYSTEM;
    create_info_.required_flags =
        AMDF_MEMORY_FLAG_HOST_VISIBLE | AMDF_MEMORY_FLAG_DEVICE_ADDRESS;
    create_info_.byte_length = 4097;
    create_info_.minimum_alignment = 4096;
  }

  void TearDown() override { g_fake_state = nullptr; }

  FakeKmtState state_;
  amdf_kmt_api_t kmt_ = {};
  amdf_xdna_umd_device_t device_ = {};
  volatile uint64_t paging_fence_ = 0;
  amdf_memory_create_info_t create_info_ = {};
};

TEST_F(WindowsXdnaMemoryTest, PublishesOnlyAfterMapAndOrdinaryResidency) {
  amdf_xdna_umd_memory_t* memory = nullptr;
  amdf_xdna_umd_memory_result_t result = {};
  ASSERT_TRUE(amdf_status_is_ok(
      amdf_xdna_umd_memory_create(&device_, &create_info_, &memory, &result)));
  ASSERT_NE(memory, nullptr);

  EXPECT_EQ(state_.operations,
            (std::vector<Operation>{Operation::kCreate, Operation::kMap,
                                    Operation::kWait, Operation::kResident,
                                    Operation::kWait}));
  EXPECT_EQ(state_.resident_flags.CantTrimFurther, 0u);
  EXPECT_EQ(state_.resident_flags.MustSucceed, 0u);
  EXPECT_EQ(result.memory_class, AMDF_MEMORY_CLASS_SYSTEM);
  EXPECT_EQ(result.flags,
            AMDF_MEMORY_FLAG_HOST_VISIBLE | AMDF_MEMORY_FLAG_DEVICE_ADDRESS);
  EXPECT_EQ(result.byte_length, UINT64_C(65536));
  EXPECT_EQ(result.alignment, UINT64_C(65536));
  EXPECT_TRUE(amdf_physical_memory_id_is_valid(&result.physical_backing_id));
  EXPECT_EQ(result.device_address, UINT64_C(0x12340000));

  amdf_memory_map_info_t map_info = {};
  map_info.byte_offset = 32;
  map_info.byte_length = 4096;
  map_info.flags = AMDF_MEMORY_MAP_FLAG_READ | AMDF_MEMORY_MAP_FLAG_WRITE;
  amdf_xdna_umd_host_mapping_t* mapping = nullptr;
  amdf_xdna_umd_host_mapping_result_t map_result = {};
  ASSERT_TRUE(amdf_status_is_ok(
      amdf_xdna_umd_memory_map(memory, &map_info, &mapping, &map_result)));
  ASSERT_NE(mapping, nullptr);
  EXPECT_NE(map_result.pointer, nullptr);
  EXPECT_EQ(map_result.byte_length, map_info.byte_length);
  EXPECT_EQ(map_result.cacheability, AMDF_HOST_CACHEABILITY_WRITE_BACK);
  EXPECT_EQ(map_result.cache_line_size, 64u);
  EXPECT_TRUE(amdf_status_is_ok(amdf_xdna_umd_host_mapping_cache_control(
      mapping, AMDF_HOST_CACHE_OPERATION_FLUSH, 0, map_result.byte_length)));
  EXPECT_TRUE(amdf_status_is_ok(amdf_xdna_umd_host_mapping_cache_control(
      mapping, AMDF_HOST_CACHE_OPERATION_INVALIDATE, 0,
      map_result.byte_length)));
  EXPECT_TRUE(amdf_status_is_ok(amdf_xdna_umd_host_mapping_destroy(mapping)));

  EXPECT_TRUE(amdf_status_is_ok(amdf_xdna_umd_memory_destroy(memory)));
  EXPECT_EQ(state_.operations.back(), Operation::kDestroy);
}

TEST_F(WindowsXdnaMemoryTest, RejectsUnavailablePropertiesBeforeAllocation) {
  auto expect_unsupported = [&](const amdf_memory_create_info_t& create_info) {
    amdf_xdna_umd_memory_t* memory =
        reinterpret_cast<amdf_xdna_umd_memory_t*>(uintptr_t{1});
    amdf_xdna_umd_memory_result_t result = {};

    const amdf_status_t status =
        amdf_xdna_umd_memory_create(&device_, &create_info, &memory, &result);

    EXPECT_EQ(amdf_status_code(status), AMDF_STATUS_CODE_UNSUPPORTED);
    EXPECT_EQ(memory, nullptr);
    EXPECT_TRUE(state_.operations.empty());
  };

  amdf_memory_create_info_t create_info = create_info_;
  create_info.required_flags |= AMDF_MEMORY_FLAG_EXECUTABLE;
  expect_unsupported(create_info);

  create_info = create_info_;
  create_info.memory_class = AMDF_MEMORY_CLASS_LOCAL;
  expect_unsupported(create_info);

  create_info = create_info_;
  create_info.minimum_alignment = UINT64_C(131072);
  expect_unsupported(create_info);
}

TEST_F(WindowsXdnaMemoryTest, ReclaimsEverySynchronousFailurePrefix) {
  for (FailurePoint failure_point :
       {FailurePoint::kCreate, FailurePoint::kMap,
        FailurePoint::kInvalidMapAddress, FailurePoint::kFirstWait,
        FailurePoint::kResident, FailurePoint::kPartialResident,
        FailurePoint::kSecondWait}) {
    state_ = {};
    state_.failure_point = failure_point;
    amdf_xdna_umd_memory_t* memory =
        reinterpret_cast<amdf_xdna_umd_memory_t*>(uintptr_t{1});
    amdf_xdna_umd_memory_result_t result = {};

    const amdf_status_t status =
        amdf_xdna_umd_memory_create(&device_, &create_info_, &memory, &result);

    EXPECT_FALSE(amdf_status_is_ok(status));
    EXPECT_EQ(memory, nullptr);
    const bool allocation_was_created = failure_point != FailurePoint::kCreate;
    EXPECT_EQ(!state_.operations.empty() &&
                  state_.operations.back() == Operation::kDestroy,
              allocation_was_created);
  }
}

TEST_F(WindowsXdnaMemoryTest, KeepsPublishedMemoryLiveAfterDestroyFailure) {
  amdf_xdna_umd_memory_t* memory = nullptr;
  amdf_xdna_umd_memory_result_t result = {};
  ASSERT_TRUE(amdf_status_is_ok(
      amdf_xdna_umd_memory_create(&device_, &create_info_, &memory, &result)));
  ASSERT_NE(memory, nullptr);

  state_.failure_point = FailurePoint::kDestroy;
  EXPECT_FALSE(amdf_status_is_ok(amdf_xdna_umd_memory_destroy(memory)));
  state_.failure_point = FailurePoint::kNone;
  EXPECT_TRUE(amdf_status_is_ok(amdf_xdna_umd_memory_destroy(memory)));
}

TEST_F(WindowsXdnaMemoryTest, DefersFailedConstructionRollbackToDevice) {
  state_.failure_point = FailurePoint::kMap;
  state_.destroy_failures_remaining = 1;
  amdf_xdna_umd_memory_t* memory =
      reinterpret_cast<amdf_xdna_umd_memory_t*>(uintptr_t{1});
  amdf_xdna_umd_memory_result_t result = {};

  EXPECT_FALSE(amdf_status_is_ok(
      amdf_xdna_umd_memory_create(&device_, &create_info_, &memory, &result)));
  EXPECT_EQ(memory, nullptr);
  EXPECT_NE(device_.deferred_memory_release_head, nullptr);

  state_.failure_point = FailurePoint::kNone;
  EXPECT_TRUE(amdf_status_is_ok(
      amdf_windows_xdna_device_drain_memory_releases(&device_)));
  EXPECT_EQ(device_.deferred_memory_release_head, nullptr);
  EXPECT_EQ(state_.operations,
            (std::vector<Operation>{Operation::kCreate, Operation::kMap,
                                    Operation::kDestroy, Operation::kDestroy}));
}

}  // namespace
