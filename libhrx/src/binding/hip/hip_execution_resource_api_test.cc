// Copyright 2026 The HRX Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <dlfcn.h>

#include <cstdlib>
#include <cstring>
#include <vector>

#include "binding/hip/api.h"
#include "iree/testing/gtest.h"

namespace {

const char* CandidateLibPath() {
  if (const char* env = std::getenv("HRX_TEST_LIBAMDHIP64");
      env && *env != '\0') {
    return env;
  }
#ifdef HRX_TEST_LIBAMDHIP64_PATH
  return HRX_TEST_LIBAMDHIP64_PATH;
#else
  return nullptr;
#endif
}

using HipInitFn = hipError_t (*)(unsigned int flags);
using HipGetDeviceFn = hipError_t (*)(int* device);
using HipDeviceGetDevResourceFn = hipError_t (*)(hipDevice_t device,
                                                 hipDevResource* resource,
                                                 hipDevResourceType type);
using HipDevSmResourceSplitByCountFn =
    hipError_t (*)(hipDevResource* result, unsigned int* group_count,
                   const hipDevResource* input, hipDevResource* remainder,
                   unsigned int flags, unsigned int minimum_count);

// Owns the process-scoped HIP runtime under test and its resource entry points.
// Calls cross the shared-library ABI instead of linking the implementation.
struct HipRuntimeApi {
  // Handle returned by dlopen for the HIP runtime instance.
  void* library = nullptr;

  // Initializes the HIP runtime instance.
  HipInitFn init = nullptr;

  // Returns the calling thread's current device ordinal.
  HipGetDeviceFn get_device = nullptr;

  // Queries the process-visible execution resource of one device.
  HipDeviceGetDevResourceFn device_get_resource = nullptr;

  // Splits one exact SM resource into equal-size partitions.
  HipDevSmResourceSplitByCountFn split_sm_by_count = nullptr;
};

template <typename T>
T ResolveHipSymbol(void* library, const char* name) {
  return reinterpret_cast<T>(dlsym(library, name));
}

class HipExecutionResourceApiTest : public testing::Test {
 protected:
  void SetUp() override {
    if (!api_.library) {
      const char* library_path = CandidateLibPath();
      ASSERT_NE(library_path, nullptr)
          << "the build must provide the libamdhip64 artifact under test";
      api_.library = dlopen(library_path, RTLD_LAZY | RTLD_LOCAL);
      ASSERT_NE(api_.library, nullptr)
          << "cannot dlopen " << library_path << ": " << dlerror();

      api_.init = ResolveHipSymbol<HipInitFn>(api_.library, "hipInit");
      api_.get_device =
          ResolveHipSymbol<HipGetDeviceFn>(api_.library, "hipGetDevice");
      api_.device_get_resource = ResolveHipSymbol<HipDeviceGetDevResourceFn>(
          api_.library, "hipDeviceGetDevResource");
      api_.split_sm_by_count = ResolveHipSymbol<HipDevSmResourceSplitByCountFn>(
          api_.library, "hipDevSmResourceSplitByCount");
    }

    ASSERT_NE(api_.init, nullptr);
    ASSERT_NE(api_.get_device, nullptr);
    ASSERT_NE(api_.device_get_resource, nullptr);
    ASSERT_NE(api_.split_sm_by_count, nullptr);

    ASSERT_EQ(hipSuccess, api_.init(/*flags=*/0));
    ASSERT_EQ(hipSuccess, api_.get_device(&device_));
  }

  // Runtime entry points loaded once from the HIP shared object under test.
  static HipRuntimeApi api_;

  // Live device ordinal used by resource queries.
  int device_ = -1;
};

HipRuntimeApi HipExecutionResourceApiTest::api_;

TEST_F(HipExecutionResourceApiTest, ReturnsFullVisibleSmResource) {
  hipDevResource resource;
  ASSERT_EQ(hipSuccess,
            api_.device_get_resource(device_, &resource, hipDevResourceTypeSm));
  EXPECT_EQ(resource.type, hipDevResourceTypeSm);
  ASSERT_GT(resource.sm.smCount, 0u);
  ASSERT_GT(resource.sm.minSmPartitionSize, 0u);
  ASSERT_GT(resource.sm.smCoscheduledAlignment, 0u);
  EXPECT_LE(resource.sm.minSmPartitionSize, resource.sm.smCount);
  EXPECT_EQ(resource.sm.minSmPartitionSize % resource.sm.smCoscheduledAlignment,
            0u);
  EXPECT_EQ(resource.sm.smCount % resource.sm.smCoscheduledAlignment, 0u);
  EXPECT_EQ(resource.sm.flags, hipDevSmResourceGroupDefault);
  EXPECT_EQ(resource.nextResource, nullptr);
}

TEST_F(HipExecutionResourceApiTest, RejectsInvalidQueriesWithoutPublishing) {
  EXPECT_EQ(hipErrorInvalidValue,
            api_.device_get_resource(device_, nullptr, hipDevResourceTypeSm));

  hipDevResource resource;
  std::memset(&resource, 0xA5, sizeof(resource));
  const hipDevResource expected_resource = resource;
  EXPECT_EQ(hipErrorInvalidResourceType,
            api_.device_get_resource(device_, &resource,
                                     hipDevResourceTypeWorkqueueConfig));
  EXPECT_EQ(std::memcmp(&resource, &expected_resource, sizeof(resource)), 0);

  EXPECT_EQ(
      hipErrorInvalidDevice,
      api_.device_get_resource(/*device=*/-1, &resource, hipDevResourceTypeSm));
  EXPECT_EQ(std::memcmp(&resource, &expected_resource, sizeof(resource)), 0);
}

TEST_F(HipExecutionResourceApiTest, SplitsFullResourceThroughPublicAbi) {
  hipDevResource full_resource;
  ASSERT_EQ(hipSuccess, api_.device_get_resource(device_, &full_resource,
                                                 hipDevResourceTypeSm));

  hipDevResource untouched_remainder;
  std::memset(&untouched_remainder, 0xA5, sizeof(untouched_remainder));
  const hipDevResource expected_untouched_remainder = untouched_remainder;
  unsigned int possible_partition_count = 1;
  ASSERT_EQ(hipSuccess, api_.split_sm_by_count(
                            /*result=*/nullptr, &possible_partition_count,
                            &full_resource, &untouched_remainder, /*flags=*/0,
                            full_resource.sm.minSmPartitionSize));
  ASSERT_GT(possible_partition_count, 0u);
  EXPECT_EQ(std::memcmp(&untouched_remainder, &expected_untouched_remainder,
                        sizeof(untouched_remainder)),
            0);

  const unsigned int requested_partition_count =
      possible_partition_count < 2 ? possible_partition_count : 2;
  std::vector<hipDevResource> partitions(requested_partition_count);
  unsigned int actual_partition_count = requested_partition_count;
  hipDevResource remainder;
  ASSERT_EQ(hipSuccess,
            api_.split_sm_by_count(partitions.data(), &actual_partition_count,
                                   &full_resource, &remainder, /*flags=*/0,
                                   full_resource.sm.minSmPartitionSize));
  ASSERT_EQ(actual_partition_count, requested_partition_count);

  uint64_t returned_sm_count = 0;
  for (const hipDevResource& partition : partitions) {
    EXPECT_EQ(partition.type, hipDevResourceTypeSm);
    ASSERT_GT(partition.sm.smCoscheduledAlignment, 0u);
    EXPECT_GE(partition.sm.smCount, full_resource.sm.minSmPartitionSize);
    EXPECT_EQ(partition.sm.smCount % partition.sm.smCoscheduledAlignment, 0u);
    EXPECT_EQ(partition.sm.flags, hipDevSmResourceGroupDefault);
    returned_sm_count += partition.sm.smCount;
  }
  if (remainder.type == hipDevResourceTypeSm) {
    ASSERT_GT(remainder.sm.smCoscheduledAlignment, 0u);
    EXPECT_GT(remainder.sm.smCount, 0u);
    EXPECT_EQ(remainder.sm.smCount % remainder.sm.smCoscheduledAlignment, 0u);
    returned_sm_count += remainder.sm.smCount;
  } else {
    EXPECT_EQ(remainder.type, hipDevResourceTypeInvalid);
  }
  EXPECT_EQ(returned_sm_count, full_resource.sm.smCount);
}

TEST_F(HipExecutionResourceApiTest, RejectsUnsupportedSplitWithoutPublishing) {
  hipDevResource full_resource;
  ASSERT_EQ(hipSuccess, api_.device_get_resource(device_, &full_resource,
                                                 hipDevResourceTypeSm));

  auto expect_failure_without_publication = [&](unsigned int flags,
                                                unsigned int minimum_count,
                                                hipError_t expected_result) {
    hipDevResource partition;
    std::memset(&partition, 0xA5, sizeof(partition));
    const hipDevResource expected_partition = partition;
    hipDevResource remainder;
    std::memset(&remainder, 0x5A, sizeof(remainder));
    const hipDevResource expected_remainder = remainder;
    unsigned int partition_count = 1;

    EXPECT_EQ(
        api_.split_sm_by_count(&partition, &partition_count, &full_resource,
                               &remainder, flags, minimum_count),
        expected_result);
    EXPECT_EQ(partition_count, 1u);
    EXPECT_EQ(std::memcmp(&partition, &expected_partition, sizeof(partition)),
              0);
    EXPECT_EQ(std::memcmp(&remainder, &expected_remainder, sizeof(remainder)),
              0);
  };

  expect_failure_without_publication(hipDevSmResourceSplitIgnoreSmCoscheduling,
                                     full_resource.sm.minSmPartitionSize,
                                     hipErrorNotSupported);
  expect_failure_without_publication(
      hipDevSmResourceSplitMaxPotentialClusterSize,
      full_resource.sm.minSmPartitionSize, hipErrorNotSupported);
  expect_failure_without_publication(
      /*flags=*/4, full_resource.sm.minSmPartitionSize, hipErrorInvalidValue);
  expect_failure_without_publication(/*flags=*/0, full_resource.sm.smCount + 1,
                                     hipErrorInvalidValue);
}

}  // namespace
