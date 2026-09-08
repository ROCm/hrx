// Copyright 2026 The HRX Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "binding/hip/execution_resource.h"

#include <cstring>

#include "common/internal.h"
#include "iree/hal/testing/mock_device.h"
#include "iree/testing/gtest.h"
#include "iree/testing/status_matchers.h"

namespace iree::hip {
namespace {

using iree::testing::status::StatusIs;

static constexpr iree_hal_queue_priority_t kQueuePriorities[] = {
    IREE_HAL_QUEUE_PRIORITY_NORMAL,
};
static constexpr iree_hal_queue_execution_resource_group_spec_t
    kExecutionResourceGroups[] = {
        {/*.minimum_selected_resource_count=*/1},
        {/*.minimum_selected_resource_count=*/1},
};
static constexpr iree_hal_queue_execution_resource_spec_t
    kExecutionResources[] = {
        {/*.group_ordinal=*/0,
         /*.first_execution_unit_ordinal=*/0,
         /*.execution_unit_count=*/2},
        {/*.group_ordinal=*/0,
         /*.first_execution_unit_ordinal=*/2,
         /*.execution_unit_count=*/4},
        {/*.group_ordinal=*/1,
         /*.first_execution_unit_ordinal=*/6,
         /*.execution_unit_count=*/2},
        {/*.group_ordinal=*/1,
         /*.first_execution_unit_ordinal=*/8,
         /*.execution_unit_count=*/6},
};

static iree_hal_device_spec_t* CreateDeviceSpec() {
  const iree_hal_physical_device_spec_t physical_device = {
      /*.identity=*/
      {
          /*.display_name=*/IREE_SV("Test GPU"),
          /*.backend_path=*/IREE_SV("test://gpu"),
      },
      /*.physical_ordinal=*/0,
      /*.partition_ordinal=*/0,
      /*.partition_count=*/1,
      /*.physical_device_affinity=*/1,
  };
  const iree_hal_device_identity_spec_t identity = {
      /*.logical_device_id=*/IREE_SV("hip-execution-resource-test"),
      /*.display_name=*/IREE_SV("HIP execution resource test device"),
      /*.driver_id=*/IREE_SV("mock"),
      /*.driver_version=*/IREE_SV("test"),
      /*.backend_id=*/IREE_SV("mock"),
      /*.device_path=*/IREE_SV("test://device"),
      /*.vendor_name=*/IREE_SV("Test"),
      /*.vendor_id=*/0,
      /*.device_id=*/0,
      /*.revision_id=*/0,
      /*.logical_ordinal=*/0,
      /*.physical_device_count=*/1,
      /*.physical_devices=*/&physical_device,
      /*.flags=*/IREE_HAL_DEVICE_IDENTITY_FLAG_NONE,
  };
  const iree_hal_queue_family_spec_t queue_family = {
      /*.name=*/IREE_SV("dispatch"),
      /*.provisioned_queue_count=*/0,
      /*.priority_count=*/IREE_ARRAYSIZE(kQueuePriorities),
      /*.priorities=*/kQueuePriorities,
      /*.execution_unit_count=*/14,
      /*.execution_resource_group_count=*/
      IREE_ARRAYSIZE(kExecutionResourceGroups),
      /*.execution_resource_groups=*/kExecutionResourceGroups,
      /*.execution_resource_count=*/IREE_ARRAYSIZE(kExecutionResources),
      /*.execution_resources=*/kExecutionResources,
      /*.supported_queue_features=*/IREE_HAL_QUEUE_FEATURE_FLAG_NONE,
      /*.timestamp_valid_bits=*/0,
      /*.timestamp_frequency_hz=*/0,
      /*.physical_device_affinity=*/1,
      /*.role_flags=*/IREE_HAL_QUEUE_FAMILY_ROLE_FLAG_DISPATCH,
      /*.atomic_capabilities=*/{},
      /*.zero_compute_atomic_capabilities=*/{},
      /*.flags=*/IREE_HAL_QUEUE_FAMILY_SPEC_FLAG_DYNAMIC_ACQUISITION,
  };
  const iree_hal_device_queue_spec_t queues = {
      /*.family_count=*/1,
      /*.families=*/&queue_family,
  };
  const iree_hal_device_spec_params_t params = {
      /*.identity=*/&identity,
      /*.memory=*/nullptr,
      /*.virtual_memory=*/nullptr,
      /*.queues=*/&queues,
  };
  iree_hal_device_spec_t* device_spec = nullptr;
  IREE_CHECK_OK(iree_hal_device_spec_create(&params, iree_allocator_system(),
                                            &device_spec));
  return device_spec;
}

class ExecutionResourceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    iree_hal_device_spec_t* device_spec = CreateDeviceSpec();
    iree_hal_mock_device_options_t options;
    iree_hal_mock_device_options_initialize(&options);
    options.identifier = IREE_SV("hip-execution-resource-test");
    options.device_spec = device_spec;
    IREE_CHECK_OK(iree_hal_mock_device_create(&options, iree_allocator_system(),
                                              &hal_device_));
    iree_hal_device_spec_release(device_spec);

    device_.ordinal = 7;
    device_.hal_device = hal_device_;
    IREE_CHECK_OK(iree_hal_streaming_execution_resource_table_initialize(
        hal_device_, iree_allocator_system(),
        &device_.execution_resource_table));
  }

  void TearDown() override {
    iree_hal_streaming_execution_resource_table_deinitialize(
        &device_.execution_resource_table);
    iree_hal_device_release(hal_device_);
  }

  const iree_hal_queue_family_t* queue_family() const {
    return iree_hal_device_queue_family(hal_device_, 0);
  }

  iree_hal_device_t* hal_device_ = nullptr;
  iree_hal_streaming_device_t device_ = {};
};

TEST_F(ExecutionResourceTest, CreatesCopyableResourcesFromExactSets) {
  hipDevResource full_resource;
  IREE_ASSERT_OK(iree_hip_execution_resource_create_sm(
      &device_, queue_family(), {/*.count=*/0, /*.ordinals=*/nullptr},
      hipDevSmResourceGroupDefault, &full_resource));
  EXPECT_EQ(full_resource.type, hipDevResourceTypeSm);
  EXPECT_EQ(full_resource.sm.smCount, 14u);
  EXPECT_EQ(full_resource.sm.minSmPartitionSize, 4u);
  EXPECT_EQ(full_resource.sm.smCoscheduledAlignment, 2u);
  EXPECT_EQ(full_resource.sm.flags, hipDevSmResourceGroupDefault);
  EXPECT_EQ(full_resource.nextResource, nullptr);

  hipDevResource copied_resource = full_resource;
  const iree_hal_streaming_execution_resource_set_t* full_set = nullptr;
  EXPECT_EQ(iree_hip_execution_resource_resolve_sm_for_device(
                &copied_resource, &device_, &full_set),
            hipSuccess);
  ASSERT_NE(full_set, nullptr);
  EXPECT_EQ(full_set->resources.count, IREE_ARRAYSIZE(kExecutionResources));

  const iree_hal_queue_execution_resource_ordinal_t partition_ordinals[] = {1,
                                                                            3};
  hipDevResource partition_resource;
  IREE_ASSERT_OK(iree_hip_execution_resource_create_sm(
      &device_, queue_family(),
      {/*.count=*/IREE_ARRAYSIZE(partition_ordinals),
       /*.ordinals=*/partition_ordinals},
      hipDevSmResourceGroupBackfill, &partition_resource));
  EXPECT_EQ(partition_resource.sm.smCount, 10u);
  EXPECT_EQ(partition_resource.sm.minSmPartitionSize, 10u);
  EXPECT_EQ(partition_resource.sm.smCoscheduledAlignment, 2u);
  EXPECT_EQ(partition_resource.sm.flags, hipDevSmResourceGroupBackfill);
  const iree_hal_streaming_execution_resource_set_t* partition_set = nullptr;
  EXPECT_EQ(iree_hip_execution_resource_resolve_sm_for_device(
                &partition_resource, &device_, &partition_set),
            hipSuccess);
}

TEST_F(ExecutionResourceTest, RejectsStaleAndTamperedCopies) {
  hipDevResource unchanged_resource;
  std::memset(&unchanged_resource, 0xA5, sizeof(unchanged_resource));
  const hipDevResource expected_unchanged_resource = unchanged_resource;
  EXPECT_THAT(
      iree_hip_execution_resource_create_sm(
          &device_, queue_family(), {/*.count=*/0, /*.ordinals=*/nullptr},
          /*flags=*/2, &unchanged_resource),
      StatusIs(StatusCode::kInvalidArgument));
  EXPECT_EQ(std::memcmp(&unchanged_resource, &expected_unchanged_resource,
                        sizeof(unchanged_resource)),
            0);

  hipDevResource stale_resource;
  IREE_ASSERT_OK(iree_hip_execution_resource_create_sm(
      &device_, queue_family(), {/*.count=*/0, /*.ordinals=*/nullptr},
      hipDevSmResourceGroupDefault, &stale_resource));

  iree_hal_streaming_execution_resource_table_deinitialize(
      &device_.execution_resource_table);
  IREE_ASSERT_OK(iree_hal_streaming_execution_resource_table_initialize(
      hal_device_, iree_allocator_system(), &device_.execution_resource_table));
  const iree_hal_streaming_execution_resource_set_t* resolved_set =
      reinterpret_cast<const iree_hal_streaming_execution_resource_set_t*>(1);
  EXPECT_EQ(iree_hip_execution_resource_resolve_sm_for_device(
                &stale_resource, &device_, &resolved_set),
            hipErrorInvalidResourceConfiguration);
  EXPECT_EQ(
      resolved_set,
      reinterpret_cast<const iree_hal_streaming_execution_resource_set_t*>(1));

  hipDevResource tampered_resource;
  IREE_ASSERT_OK(iree_hip_execution_resource_create_sm(
      &device_, queue_family(), {/*.count=*/0, /*.ordinals=*/nullptr},
      hipDevSmResourceGroupDefault, &tampered_resource));
  ++tampered_resource.sm.smCount;
  EXPECT_EQ(iree_hip_execution_resource_resolve_sm_for_device(
                &tampered_resource, &device_, &resolved_set),
            hipErrorInvalidResourceConfiguration);
  EXPECT_EQ(
      resolved_set,
      reinterpret_cast<const iree_hal_streaming_execution_resource_set_t*>(1));

  --tampered_resource.sm.smCount;
  tampered_resource.sm.flags = hipDevSmResourceGroupBackfill;
  EXPECT_EQ(iree_hip_execution_resource_resolve_sm_for_device(
                &tampered_resource, &device_, &resolved_set),
            hipErrorInvalidResourceConfiguration);
  EXPECT_EQ(
      resolved_set,
      reinterpret_cast<const iree_hal_streaming_execution_resource_set_t*>(1));
}

}  // namespace
}  // namespace iree::hip
