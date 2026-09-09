// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/tooling/execution/hal/device_provider.h"

#include <memory>

#include "iree/hal/api.h"
#include "iree/testing/gtest.h"
#include "iree/testing/status_matchers.h"
#include "loom/tooling/execution/hal/runtime.h"

namespace loom {
namespace {

struct DeviceSpecDeleter {
  void operator()(iree_hal_device_spec_t* device_spec) const {
    iree_hal_device_spec_release(device_spec);
  }
};
using DeviceSpecPtr =
    std::unique_ptr<iree_hal_device_spec_t, DeviceSpecDeleter>;

typedef struct FakeHalDevice {
  // HAL resource header used by device vtable dispatch.
  iree_hal_resource_t resource;
  // Immutable device facts borrowed from the test fixture.
  const iree_hal_device_spec_t* device_spec;
} FakeHalDevice;

static const iree_hal_device_spec_t* FakeHalDeviceSpec(
    iree_hal_device_t* base_device) {
  return reinterpret_cast<FakeHalDevice*>(base_device)->device_spec;
}

static iree_hal_device_vtable_t MakeFakeHalDeviceVtable() {
  iree_hal_device_vtable_t vtable = {};
  vtable.device_spec = FakeHalDeviceSpec;
  return vtable;
}

static const iree_hal_device_vtable_t kFakeHalDeviceVtable =
    MakeFakeHalDeviceVtable();

static const loom_target_profile_type_t kFakeProfileType = {
    /*.name=*/IREE_SVL("fake"),
};
static const loom_target_profile_type_t kOtherProfileType = {
    /*.name=*/IREE_SVL("other"),
};
static const loom_target_profile_t kFakeProfile = {
    /*.type=*/&kFakeProfileType,
};
static const loom_target_profile_t kAlternateFakeProfile = {
    /*.type=*/&kFakeProfileType,
};
static const loom_target_profile_t kOtherProfile = {
    /*.type=*/&kOtherProfileType,
};
static const loom_artifact_provider_t kFakeArtifactProvider = {
    /*.name=*/IREE_SVL("fake-artifact"),
    /*.public_artifact_format=*/IREE_SVL("FakeExecutableFormat123"),
    /*.flags=*/LOOM_ARTIFACT_PROVIDER_FLAG_CANONICAL,
    /*.target_profile_type=*/&kFakeProfileType,
};

typedef struct FakeDeviceProvider {
  // Device provider exposed to the production selection wrapper.
  loom_device_provider_t base;
  // Executable target returned by the fake selection callback.
  const iree_hal_executable_target_t* returned_executable_target;
  // Artifact profile returned by the fake selection callback.
  const loom_target_profile_t* returned_profile;
  // Artifact target key returned by the fake selection callback.
  iree_string_view_t returned_target_key;
} FakeDeviceProvider;

static iree_status_t FakeSelectProfileTarget(
    const loom_device_provider_t* base_provider,
    const loom_run_hal_runtime_t* runtime,
    const loom_target_profile_t* target_profile,
    loom_device_target_t* out_target) {
  (void)runtime;
  const FakeDeviceProvider* provider =
      reinterpret_cast<const FakeDeviceProvider*>(base_provider);
  *out_target = (loom_device_target_t){
      /*.executable_target=*/provider->returned_executable_target,
      /*.artifact_target=*/
      {
          /*.target_profile=*/provider->returned_profile != nullptr
              ? provider->returned_profile
              : target_profile,
          /*.target_key=*/provider->returned_target_key,
      },
  };
  return iree_ok_status();
}

class DeviceProviderTest : public ::testing::Test {
 protected:
  void Initialize(uint64_t target_affinity = 1, uint64_t queue_affinity = 1) {
    const iree_hal_executable_target_t executable_target = {
        /*.family=*/IREE_SV("fake"),
        /*.target_key=*/IREE_SV("target-123"),
        /*.kind=*/IREE_HAL_EXECUTABLE_TARGET_KIND_EXACT,
        /*.priority=*/100,
        /*.physical_device_affinity=*/target_affinity,
    };
    const iree_hal_device_executable_spec_t executables = {
        /*.target_count=*/1,
        /*.targets=*/&executable_target,
    };
    const iree_hal_queue_family_spec_t queue_family = {
        /*.name=*/IREE_SV("dispatch"),
        /*.provisioned_queue_count=*/1,
        /*.priority_count=*/1,
        /*.timestamp_valid_bits=*/0,
        /*.timestamp_frequency_hz=*/0,
        /*.physical_device_affinity=*/queue_affinity,
        /*.role_flags=*/IREE_HAL_QUEUE_FAMILY_ROLE_FLAG_DISPATCH,
    };
    const iree_hal_device_queue_spec_t queues = {
        /*.family_count=*/1,
        /*.families=*/&queue_family,
    };
    const iree_hal_device_spec_params_t params = {
        /*.identity=*/nullptr,
        /*.memory=*/nullptr,
        /*.virtual_memory=*/nullptr,
        /*.queues=*/&queues,
        /*.dispatch=*/nullptr,
        /*.timing=*/nullptr,
        /*.executables=*/&executables,
        /*.sanitizer=*/nullptr,
        /*.facet_count=*/0,
        /*.facets=*/nullptr,
    };
    iree_hal_device_spec_t* device_spec = nullptr;
    IREE_ASSERT_OK(iree_hal_device_spec_create(&params, iree_allocator_system(),
                                               &device_spec));
    device_spec_.reset(device_spec);

    device_.device_spec = device_spec_.get();
    iree_hal_resource_initialize(&kFakeHalDeviceVtable, &device_.resource);
    iree_hal_queue_family_initialize(/*ordinal=*/0, &dispatch_queue_family_);
    dispatch_queue_.queue_family = &dispatch_queue_family_;
    runtime_.device = reinterpret_cast<iree_hal_device_t*>(&device_);
    runtime_.dispatch_queue = &dispatch_queue_;

    provider_.base.artifact_provider = &kFakeArtifactProvider;
    provider_.base.select_profile_target = FakeSelectProfileTarget;
    provider_.returned_executable_target =
        &iree_hal_device_spec_executables(device_spec_.get())->targets[0];
    provider_.returned_target_key = IREE_SV("target-123");
  }

  loom_device_target_t Select(const loom_target_profile_t* profile) {
    loom_device_target_t target = {};
    IREE_EXPECT_OK(loom_device_provider_select_profile_target(
        &provider_.base, &runtime_, profile, &target));
    return target;
  }

  DeviceSpecPtr device_spec_;
  FakeHalDevice device_ = {};
  iree_hal_queue_family_t dispatch_queue_family_ = {};
  iree_hal_queue_t dispatch_queue_ = {};
  loom_run_hal_runtime_t runtime_ = {};
  FakeDeviceProvider provider_ = {};
};

TEST_F(DeviceProviderTest, AcceptsBorrowedProfileAndDeviceTarget) {
  Initialize();
  const loom_device_target_t target = Select(&kFakeProfile);

  EXPECT_EQ(target.artifact_target.target_profile, &kFakeProfile);
  EXPECT_EQ(target.executable_target, provider_.returned_executable_target);
}

TEST_F(DeviceProviderTest, RejectsAnotherProfileFamily) {
  Initialize();
  loom_device_target_t target = {};
  IREE_EXPECT_STATUS_IS(
      IREE_STATUS_INVALID_ARGUMENT,
      loom_device_provider_select_profile_target(&provider_.base, &runtime_,
                                                 &kOtherProfile, &target));
  EXPECT_EQ(target.executable_target, nullptr);
}

TEST_F(DeviceProviderTest, RejectsChangedProfileIdentity) {
  Initialize();
  provider_.returned_profile = &kAlternateFakeProfile;
  loom_device_target_t target = {};
  IREE_EXPECT_STATUS_IS(
      IREE_STATUS_FAILED_PRECONDITION,
      loom_device_provider_select_profile_target(&provider_.base, &runtime_,
                                                 &kFakeProfile, &target));
  EXPECT_EQ(target.artifact_target.target_profile, nullptr);
}

TEST_F(DeviceProviderTest, RejectsForeignExecutableTarget) {
  Initialize();
  iree_hal_executable_target_t copied_target =
      *provider_.returned_executable_target;
  provider_.returned_executable_target = &copied_target;
  loom_device_target_t target = {};
  IREE_EXPECT_STATUS_IS(
      IREE_STATUS_FAILED_PRECONDITION,
      loom_device_provider_select_profile_target(&provider_.base, &runtime_,
                                                 &kFakeProfile, &target));
  EXPECT_EQ(target.executable_target, nullptr);
}

TEST_F(DeviceProviderTest, RejectsChangedTargetKey) {
  Initialize();
  provider_.returned_target_key = IREE_SV("other-target");
  loom_device_target_t target = {};
  IREE_EXPECT_STATUS_IS(
      IREE_STATUS_FAILED_PRECONDITION,
      loom_device_provider_select_profile_target(&provider_.base, &runtime_,
                                                 &kFakeProfile, &target));
  EXPECT_EQ(target.executable_target, nullptr);
}

TEST_F(DeviceProviderTest, RejectsInsufficientPhysicalDeviceAffinity) {
  Initialize(/*target_affinity=*/1, /*queue_affinity=*/3);
  loom_device_target_t target = {};
  IREE_EXPECT_STATUS_IS(
      IREE_STATUS_INCOMPATIBLE,
      loom_device_provider_select_profile_target(&provider_.base, &runtime_,
                                                 &kFakeProfile, &target));
  EXPECT_EQ(target.executable_target, nullptr);
}

}  // namespace
}  // namespace loom
