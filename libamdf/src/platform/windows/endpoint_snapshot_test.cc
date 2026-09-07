// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/platform/windows/endpoint_snapshot.h"

#include <cstdint>
#include <cstring>

#include "gtest/gtest.h"
#include "libamdf/src/platform/endpoint.h"
#include "libamdf/src/platform/windows/instance.h"

namespace {

constexpr NTSTATUS kSuccess = 0;
constexpr NTSTATUS kFailure = static_cast<NTSTATUS>(0xC0000001u);
constexpr D3DKMT_HANDLE kAmdAdapter = 1;
constexpr D3DKMT_HANDLE kOtherAdapter = 2;

enum class FailurePoint {
  kNone,
  kOpen,
  kOpenWithHandle,
  kFirstEnumeration,
  kSecondEnumeration,
  kPhysicalAdapterCount,
  kDeviceIds,
  kAdapterType,
  kDescription,
  kClose,
};

struct FakeKmt {
  FailurePoint failure_point = FailurePoint::kNone;
  uint32_t amd_physical_adapter_count = 1;
  uint32_t enumeration_call_count = 0;
  uint32_t close_call_count = 0;
};

FakeKmt* current_fake = nullptr;

NTSTATUS APIENTRY
FakeOpenAdapterFromLuid(D3DKMT_OPENADAPTERFROMLUID* open_adapter) {
  if (open_adapter->AdapterLuid.LowPart != kAmdAdapter) {
    return kFailure;
  }
  if (current_fake->failure_point == FailurePoint::kOpen) {
    return kFailure;
  }
  open_adapter->hAdapter = kAmdAdapter;
  return current_fake->failure_point == FailurePoint::kOpenWithHandle
             ? kFailure
             : kSuccess;
}

NTSTATUS APIENTRY FakeEnumerateAdapters(D3DKMT_ENUMADAPTERS3* enumeration) {
  ++current_fake->enumeration_call_count;
  if (current_fake->enumeration_call_count == 1) {
    if (current_fake->failure_point == FailurePoint::kFirstEnumeration) {
      return kFailure;
    }
    enumeration->NumAdapters = 2;
    return kSuccess;
  }

  if (enumeration->NumAdapters < 2 || enumeration->pAdapters == nullptr) {
    return kFailure;
  }
  enumeration->NumAdapters = 2;
  enumeration->pAdapters[0].hAdapter = kAmdAdapter;
  enumeration->pAdapters[0].AdapterLuid.LowPart = kAmdAdapter;
  enumeration->pAdapters[1].hAdapter = kOtherAdapter;
  enumeration->pAdapters[1].AdapterLuid.LowPart = kOtherAdapter;
  if (current_fake->failure_point == FailurePoint::kSecondEnumeration) {
    return kFailure;
  }
  return kSuccess;
}

NTSTATUS APIENTRY FakeQueryAdapterInfo(const D3DKMT_QUERYADAPTERINFO* query) {
  switch (query->Type) {
    case KMTQAITYPE_PHYSICALADAPTERCOUNT: {
      if (current_fake->failure_point == FailurePoint::kPhysicalAdapterCount) {
        return kFailure;
      }
      auto* count = static_cast<D3DKMT_PHYSICAL_ADAPTER_COUNT*>(
          query->pPrivateDriverData);
      count->Count = query->hAdapter == kAmdAdapter
                         ? current_fake->amd_physical_adapter_count
                         : 1;
      return kSuccess;
    }
    case KMTQAITYPE_PHYSICALADAPTERDEVICEIDS: {
      if (current_fake->failure_point == FailurePoint::kDeviceIds) {
        return kFailure;
      }
      auto* ids =
          static_cast<D3DKMT_QUERY_DEVICE_IDS*>(query->pPrivateDriverData);
      ids->DeviceIds.VendorID =
          query->hAdapter == kAmdAdapter ? 0x1002u : 0x8086u;
      ids->DeviceIds.DeviceID = 0x1000u + ids->PhysicalAdapterIndex;
      ids->DeviceIds.SubVendorID = ids->DeviceIds.VendorID;
      ids->DeviceIds.SubSystemID = 0x2000u;
      ids->DeviceIds.RevisionID = 1;
      ids->DeviceIds.BusType = 1;
      return kSuccess;
    }
    case KMTQAITYPE_ADAPTERTYPE: {
      if (current_fake->failure_point == FailurePoint::kAdapterType) {
        return kFailure;
      }
      auto* type = static_cast<D3DKMT_ADAPTERTYPE*>(query->pPrivateDriverData);
      type->Value = 0;
      type->RenderSupported = 1;
      return kSuccess;
    }
    case KMTQAITYPE_DRIVER_DESCRIPTION: {
      if (current_fake->failure_point == FailurePoint::kDescription) {
        return kFailure;
      }
      auto* description =
          static_cast<D3DKMT_DRIVER_DESCRIPTION*>(query->pPrivateDriverData);
      static constexpr wchar_t kName[] = L"Fake adapter";
      std::memcpy(description->DriverDescription, kName, sizeof(kName));
      return kSuccess;
    }
    default:
      return kFailure;
  }
}

NTSTATUS APIENTRY FakeCloseAdapter(const D3DKMT_CLOSEADAPTER* close) {
  EXPECT_TRUE(close->hAdapter == kAmdAdapter ||
              close->hAdapter == kOtherAdapter);
  ++current_fake->close_call_count;
  return current_fake->failure_point == FailurePoint::kClose ? kFailure
                                                             : kSuccess;
}

class EndpointSnapshotTest : public ::testing::Test {
 protected:
  void SetUp() override {
    current_fake = &fake_;
    api_.enumerate_adapters = FakeEnumerateAdapters;
    api_.open_adapter_from_luid = FakeOpenAdapterFromLuid;
    api_.query_adapter_info = FakeQueryAdapterInfo;
    api_.close_adapter = FakeCloseAdapter;
    platform_instance_.kmt = api_;
  }

  void TearDown() override { current_fake = nullptr; }

  amdf_endpoint_id_t EnumerateAmdEndpointId() {
    amdf_endpoint_summary_t summary = {};
    uint32_t endpoint_count = 0;
    const amdf_status_t status = amdf_windows_endpoint_snapshot_enumerate(
        &api_, 1, &summary, &endpoint_count);
    EXPECT_TRUE(amdf_status_is_ok(status));
    EXPECT_EQ(endpoint_count, 1u);
    fake_.enumeration_call_count = 0;
    fake_.close_call_count = 0;
    return summary.id;
  }

  FakeKmt fake_;
  amdf_kmt_api_t api_ = {};
  amdf_platform_instance_t platform_instance_ = {};
};

TEST_F(EndpointSnapshotTest, ReturnsOnlyAmdEndpointsAndClosesSnapshot) {
  amdf_endpoint_summary_t summaries[2] = {};
  uint32_t endpoint_count = 0;
  const amdf_status_t status = amdf_windows_endpoint_snapshot_enumerate(
      &api_, 2, summaries, &endpoint_count);

  EXPECT_TRUE(amdf_status_is_ok(status));
  ASSERT_EQ(endpoint_count, 1u);
  EXPECT_STREQ(summaries[0].name, "Fake adapter");
  EXPECT_EQ(summaries[0].engine_kind, AMDF_ENGINE_KIND_GPU);
  EXPECT_EQ(fake_.enumeration_call_count, 2u);
  EXPECT_EQ(fake_.close_call_count, 2u);
}

TEST_F(EndpointSnapshotTest, CountsWithoutOutputStorage) {
  uint32_t endpoint_count = 0;
  const amdf_status_t status = amdf_windows_endpoint_snapshot_enumerate(
      &api_, 0, nullptr, &endpoint_count);

  EXPECT_TRUE(amdf_status_is_ok(status));
  EXPECT_EQ(endpoint_count, 1u);
  EXPECT_EQ(fake_.close_call_count, 2u);
}

TEST_F(EndpointSnapshotTest, EmitsDistinctPhysicalAdapterEndpoints) {
  fake_.amd_physical_adapter_count = 2;
  amdf_endpoint_summary_t summaries[2] = {};
  uint32_t endpoint_count = 0;
  const amdf_status_t status = amdf_windows_endpoint_snapshot_enumerate(
      &api_, 2, summaries, &endpoint_count);

  EXPECT_TRUE(amdf_status_is_ok(status));
  ASSERT_EQ(endpoint_count, 2u);
  EXPECT_FALSE(amdf_endpoint_id_is_equal(&summaries[0].id, &summaries[1].id));
  EXPECT_EQ(fake_.close_call_count, 2u);
}

TEST_F(EndpointSnapshotTest, WritesAvailablePrefixAndReportsTotal) {
  fake_.amd_physical_adapter_count = 2;
  amdf_endpoint_summary_t summary = {};
  uint32_t endpoint_count = 0;
  const amdf_status_t status = amdf_windows_endpoint_snapshot_enumerate(
      &api_, 1, &summary, &endpoint_count);

  EXPECT_EQ(amdf_status_domain(status), AMDF_STATUS_DOMAIN_API);
  EXPECT_EQ(amdf_status_code(status), AMDF_STATUS_CODE_BUFFER_TOO_SMALL);
  EXPECT_EQ(endpoint_count, 2u);
  EXPECT_STREQ(summary.name, "Fake adapter");
  EXPECT_EQ(fake_.close_call_count, 2u);
}

TEST_F(EndpointSnapshotTest, ClosesReturnedHandlesWhenEnumerationFails) {
  fake_.failure_point = FailurePoint::kSecondEnumeration;
  uint32_t endpoint_count = 0;
  const amdf_status_t status = amdf_windows_endpoint_snapshot_enumerate(
      &api_, 0, nullptr, &endpoint_count);

  EXPECT_EQ(amdf_status_domain(status), AMDF_STATUS_DOMAIN_NTSTATUS);
  EXPECT_EQ(amdf_status_code(status), static_cast<uint32_t>(kFailure));
  EXPECT_EQ(fake_.close_call_count, 2u);
}

TEST_F(EndpointSnapshotTest, ReturnsInitialEnumerationFailureWithoutCleanup) {
  fake_.failure_point = FailurePoint::kFirstEnumeration;
  uint32_t endpoint_count = 0;
  const amdf_status_t status = amdf_windows_endpoint_snapshot_enumerate(
      &api_, 0, nullptr, &endpoint_count);

  EXPECT_EQ(amdf_status_domain(status), AMDF_STATUS_DOMAIN_NTSTATUS);
  EXPECT_EQ(amdf_status_code(status), static_cast<uint32_t>(kFailure));
  EXPECT_EQ(fake_.close_call_count, 0u);
}

TEST_F(EndpointSnapshotTest, ClosesEveryHandleAfterQueryFailures) {
  const FailurePoint failure_points[] = {
      FailurePoint::kPhysicalAdapterCount,
      FailurePoint::kDeviceIds,
      FailurePoint::kAdapterType,
      FailurePoint::kDescription,
  };
  for (FailurePoint failure_point : failure_points) {
    fake_ = {};
    fake_.failure_point = failure_point;
    uint32_t endpoint_count = 0;
    const amdf_status_t status = amdf_windows_endpoint_snapshot_enumerate(
        &api_, 0, nullptr, &endpoint_count);
    EXPECT_EQ(amdf_status_domain(status), AMDF_STATUS_DOMAIN_NTSTATUS);
    EXPECT_EQ(amdf_status_code(status), static_cast<uint32_t>(kFailure));
    EXPECT_EQ(fake_.close_call_count, 2u);
  }
}

TEST_F(EndpointSnapshotTest, SurfacesCloseFailureAfterClosingEveryHandle) {
  fake_.failure_point = FailurePoint::kClose;
  uint32_t endpoint_count = 0;
  const amdf_status_t status = amdf_windows_endpoint_snapshot_enumerate(
      &api_, 0, nullptr, &endpoint_count);

  EXPECT_EQ(amdf_status_domain(status), AMDF_STATUS_DOMAIN_NTSTATUS);
  EXPECT_EQ(amdf_status_code(status), static_cast<uint32_t>(kFailure));
  EXPECT_EQ(fake_.close_call_count, 2u);
}

TEST_F(EndpointSnapshotTest, OpensIdentityDirectlyAndCachesProperties) {
  const amdf_endpoint_id_t id = EnumerateAmdEndpointId();
  amdf_platform_endpoint_t* endpoint = nullptr;
  amdf_endpoint_info_t info = {};
  const amdf_status_t status =
      amdf_platform_endpoint_open(&platform_instance_, &id, &endpoint, &info);

  EXPECT_TRUE(amdf_status_is_ok(status));
  ASSERT_NE(endpoint, nullptr);
  EXPECT_TRUE(amdf_endpoint_id_is_equal(&id, &info.id));
  EXPECT_EQ(fake_.enumeration_call_count, 0u);
  EXPECT_EQ(fake_.close_call_count, 0u);
  EXPECT_TRUE(amdf_status_is_ok(amdf_platform_endpoint_close(endpoint)));
  EXPECT_EQ(fake_.close_call_count, 1u);
}

TEST_F(EndpointSnapshotTest, RejectsStaleIdentityAndClosesOpenedAdapter) {
  amdf_endpoint_id_t id = EnumerateAmdEndpointId();
  id.words[1] ^= UINT64_C(1) << 63;
  amdf_platform_endpoint_t* endpoint =
      reinterpret_cast<amdf_platform_endpoint_t*>(uintptr_t{1});
  amdf_endpoint_info_t info = {};
  const amdf_status_t status =
      amdf_platform_endpoint_open(&platform_instance_, &id, &endpoint, &info);

  EXPECT_EQ(amdf_status_domain(status), AMDF_STATUS_DOMAIN_API);
  EXPECT_EQ(amdf_status_code(status), AMDF_STATUS_CODE_NOT_FOUND);
  EXPECT_EQ(endpoint, nullptr);
  EXPECT_EQ(fake_.enumeration_call_count, 0u);
  EXPECT_EQ(fake_.close_call_count, 1u);
}

TEST_F(EndpointSnapshotTest, ClosesHandleReturnedByFailedOpen) {
  const amdf_endpoint_id_t id = EnumerateAmdEndpointId();
  fake_.failure_point = FailurePoint::kOpenWithHandle;
  amdf_platform_endpoint_t* endpoint = nullptr;
  amdf_endpoint_info_t info = {};
  const amdf_status_t status =
      amdf_platform_endpoint_open(&platform_instance_, &id, &endpoint, &info);

  EXPECT_EQ(amdf_status_domain(status), AMDF_STATUS_DOMAIN_NTSTATUS);
  EXPECT_EQ(amdf_status_code(status), static_cast<uint32_t>(kFailure));
  EXPECT_EQ(endpoint, nullptr);
  EXPECT_EQ(fake_.close_call_count, 1u);
}

TEST_F(EndpointSnapshotTest, ReturnsOpenFailureWithoutCleanup) {
  const amdf_endpoint_id_t id = EnumerateAmdEndpointId();
  fake_.failure_point = FailurePoint::kOpen;
  amdf_platform_endpoint_t* endpoint = nullptr;
  amdf_endpoint_info_t info = {};
  const amdf_status_t status =
      amdf_platform_endpoint_open(&platform_instance_, &id, &endpoint, &info);

  EXPECT_EQ(amdf_status_domain(status), AMDF_STATUS_DOMAIN_NTSTATUS);
  EXPECT_EQ(amdf_status_code(status), static_cast<uint32_t>(kFailure));
  EXPECT_EQ(endpoint, nullptr);
  EXPECT_EQ(fake_.close_call_count, 0u);
}

TEST_F(EndpointSnapshotTest, ClosesOpenedAdapterAfterQueryFailure) {
  const amdf_endpoint_id_t id = EnumerateAmdEndpointId();
  fake_.failure_point = FailurePoint::kAdapterType;
  amdf_platform_endpoint_t* endpoint = nullptr;
  amdf_endpoint_info_t info = {};
  const amdf_status_t status =
      amdf_platform_endpoint_open(&platform_instance_, &id, &endpoint, &info);

  EXPECT_EQ(amdf_status_domain(status), AMDF_STATUS_DOMAIN_NTSTATUS);
  EXPECT_EQ(amdf_status_code(status), static_cast<uint32_t>(kFailure));
  EXPECT_EQ(endpoint, nullptr);
  EXPECT_EQ(fake_.close_call_count, 1u);
}

TEST_F(EndpointSnapshotTest, LeavesEndpointLiveWhenCloseFails) {
  const amdf_endpoint_id_t id = EnumerateAmdEndpointId();
  amdf_platform_endpoint_t* endpoint = nullptr;
  amdf_endpoint_info_t info = {};
  ASSERT_TRUE(amdf_status_is_ok(
      amdf_platform_endpoint_open(&platform_instance_, &id, &endpoint, &info)));
  ASSERT_NE(endpoint, nullptr);

  fake_.failure_point = FailurePoint::kClose;
  const amdf_status_t close_status = amdf_platform_endpoint_close(endpoint);
  EXPECT_EQ(amdf_status_domain(close_status), AMDF_STATUS_DOMAIN_NTSTATUS);
  EXPECT_EQ(amdf_status_code(close_status), static_cast<uint32_t>(kFailure));

  fake_.failure_point = FailurePoint::kNone;
  EXPECT_TRUE(amdf_status_is_ok(amdf_platform_endpoint_close(endpoint)));
  EXPECT_EQ(fake_.close_call_count, 2u);
}

}  // namespace

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
