// Copyright 2026 The HRX Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <dlfcn.h>

#include <cstdlib>
#include <cstring>
#include <memory>
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
using HipGetDevicePropertiesFn = hipError_t (*)(hipDeviceProp_t* properties,
                                                int device);
using HipDeviceGetAttributeFn = hipError_t (*)(int* value,
                                               hipDeviceAttribute_t attribute,
                                               int device);
using HipDeviceGetDevResourceFn = hipError_t (*)(hipDevice_t device,
                                                 hipDevResource* resource,
                                                 hipDevResourceType type);
using HipDeviceGetExecutionCtxFn = hipError_t (*)(hipExecutionCtx_t* context,
                                                  int device);
using HipDevSmResourceSplitByCountFn =
    hipError_t (*)(hipDevResource* result, unsigned int* group_count,
                   const hipDevResource* input, hipDevResource* remainder,
                   unsigned int flags, unsigned int minimum_count);
using HipDevResourceGenerateDescFn =
    hipError_t (*)(hipDevResourceDesc_t* descriptor, hipDevResource* resources,
                   unsigned int resource_count);
using HipGreenCtxCreateFn = hipError_t (*)(hipExecutionCtx_t* context,
                                           hipDevResourceDesc_t descriptor,
                                           int device, unsigned int flags);
using HipExecutionCtxDestroyFn = hipError_t (*)(hipExecutionCtx_t context);
using HipExecutionCtxGetDevResourceFn =
    hipError_t (*)(hipExecutionCtx_t context, hipDevResource* resource,
                   hipDevResourceType type);
using HipExecutionCtxGetDeviceFn = hipError_t (*)(hipDevice_t* device,
                                                  hipExecutionCtx_t context);
using HipExecutionCtxGetIdFn = hipError_t (*)(hipExecutionCtx_t context,
                                              unsigned long long* context_id);
using HipExecutionCtxStreamCreateFn = hipError_t (*)(hipStream_t* stream,
                                                     hipExecutionCtx_t context,
                                                     unsigned int flags,
                                                     int priority);
using HipDeviceGetStreamPriorityRangeFn =
    hipError_t (*)(int* least_priority, int* greatest_priority);
using HipStreamCreateWithPriorityFn = hipError_t (*)(hipStream_t* stream,
                                                     unsigned int flags,
                                                     int priority);
using HipExtStreamCreateWithCUMaskFn = hipError_t (*)(hipStream_t* stream,
                                                      uint32_t mask_count,
                                                      const uint32_t* mask);
using HipExtStreamGetCUMaskFn = hipError_t (*)(hipStream_t stream,
                                               uint32_t mask_count,
                                               uint32_t* mask);
using HipStreamGetDevResourceFn = hipError_t (*)(hipStream_t stream,
                                                 hipDevResource* resource,
                                                 hipDevResourceType type);
using HipStreamGetFlagsFn = hipError_t (*)(hipStream_t stream,
                                           unsigned int* flags);
using HipStreamGetPriorityFn = hipError_t (*)(hipStream_t stream,
                                              int* priority);
using HipStreamDestroyFn = hipError_t (*)(hipStream_t stream);

struct ExecutionContextDeleter {
  // Runtime entry point used to destroy a live execution context.
  HipExecutionCtxDestroyFn destroy = nullptr;

  void operator()(ihipExecutionCtx_t* context) const {
    if (context) destroy(context);
  }
};

using ScopedExecutionContext =
    std::unique_ptr<ihipExecutionCtx_t, ExecutionContextDeleter>;

struct StreamDeleter {
  // Runtime entry point used to destroy a live stream.
  HipStreamDestroyFn destroy = nullptr;

  void operator()(hipStream_st* stream) const {
    if (stream) destroy(stream);
  }
};

using ScopedStream = std::unique_ptr<hipStream_st, StreamDeleter>;

// Owns the process-scoped HIP runtime under test and its resource entry points.
// Calls cross the shared-library ABI instead of linking the implementation.
struct HipRuntimeApi {
  // Handle returned by dlopen for the HIP runtime instance.
  void* library = nullptr;

  // Initializes the HIP runtime instance.
  HipInitFn init = nullptr;

  // Returns the calling thread's current device ordinal.
  HipGetDeviceFn get_device = nullptr;

  // Queries the aggregate properties of one device.
  HipGetDevicePropertiesFn get_device_properties = nullptr;

  // Queries one property of one device.
  HipDeviceGetAttributeFn device_get_attribute = nullptr;

  // Queries the process-visible execution resource of one device.
  HipDeviceGetDevResourceFn device_get_resource = nullptr;

  // Returns the process-managed primary execution context for one device.
  HipDeviceGetExecutionCtxFn device_execution_context = nullptr;

  // Splits one exact SM resource into equal-size partitions.
  HipDevSmResourceSplitByCountFn split_sm_by_count = nullptr;

  // Generates a one-shot descriptor from exact execution resources.
  HipDevResourceGenerateDescFn generate_descriptor = nullptr;

  // Creates a resource-partitioned execution context.
  HipGreenCtxCreateFn create_context = nullptr;

  // Destroys a resource-partitioned execution context.
  HipExecutionCtxDestroyFn destroy_context = nullptr;

  // Queries the canonical resource owned by an execution context.
  HipExecutionCtxGetDevResourceFn context_get_resource = nullptr;

  // Queries the device ordinal associated with an execution context.
  HipExecutionCtxGetDeviceFn context_get_device = nullptr;

  // Queries the process-unique execution-context identifier.
  HipExecutionCtxGetIdFn context_get_id = nullptr;

  // Creates a stream on an exact execution context.
  HipExecutionCtxStreamCreateFn context_stream_create = nullptr;

  // Queries the binding's supported stream priority range.
  HipDeviceGetStreamPriorityRangeFn device_get_stream_priority_range = nullptr;

  // Creates a stream with a scheduling priority hint.
  HipStreamCreateWithPriorityFn stream_create_with_priority = nullptr;

  // Creates a stream confined to an exact HIP CU mask.
  HipExtStreamCreateWithCUMaskFn stream_create_with_cu_mask = nullptr;

  // Queries the exact HIP CU mask assigned to a stream.
  HipExtStreamGetCUMaskFn stream_get_cu_mask = nullptr;

  // Queries the exact SM resource assigned to a stream.
  HipStreamGetDevResourceFn stream_get_resource = nullptr;

  // Queries stream creation flags.
  HipStreamGetFlagsFn stream_get_flags = nullptr;

  // Queries the clamped scheduling priority assigned to a stream.
  HipStreamGetPriorityFn stream_get_priority = nullptr;

  // Destroys a live or execution-context-detached stream.
  HipStreamDestroyFn stream_destroy = nullptr;
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
      api_.get_device_properties = ResolveHipSymbol<HipGetDevicePropertiesFn>(
          api_.library, "hipGetDeviceProperties");
      api_.device_get_attribute = ResolveHipSymbol<HipDeviceGetAttributeFn>(
          api_.library, "hipDeviceGetAttribute");
      api_.device_get_resource = ResolveHipSymbol<HipDeviceGetDevResourceFn>(
          api_.library, "hipDeviceGetDevResource");
      api_.device_execution_context =
          ResolveHipSymbol<HipDeviceGetExecutionCtxFn>(
              api_.library, "hipDeviceGetExecutionCtx");
      api_.split_sm_by_count = ResolveHipSymbol<HipDevSmResourceSplitByCountFn>(
          api_.library, "hipDevSmResourceSplitByCount");
      api_.generate_descriptor = ResolveHipSymbol<HipDevResourceGenerateDescFn>(
          api_.library, "hipDevResourceGenerateDesc");
      api_.create_context = ResolveHipSymbol<HipGreenCtxCreateFn>(
          api_.library, "hipGreenCtxCreate");
      api_.destroy_context = ResolveHipSymbol<HipExecutionCtxDestroyFn>(
          api_.library, "hipExecutionCtxDestroy");
      api_.context_get_resource =
          ResolveHipSymbol<HipExecutionCtxGetDevResourceFn>(
              api_.library, "hipExecutionCtxGetDevResource");
      api_.context_get_device = ResolveHipSymbol<HipExecutionCtxGetDeviceFn>(
          api_.library, "hipExecutionCtxGetDevice");
      api_.context_get_id = ResolveHipSymbol<HipExecutionCtxGetIdFn>(
          api_.library, "hipExecutionCtxGetId");
      api_.context_stream_create =
          ResolveHipSymbol<HipExecutionCtxStreamCreateFn>(
              api_.library, "hipExecutionCtxStreamCreate");
      api_.device_get_stream_priority_range =
          ResolveHipSymbol<HipDeviceGetStreamPriorityRangeFn>(
              api_.library, "hipDeviceGetStreamPriorityRange");
      api_.stream_create_with_priority =
          ResolveHipSymbol<HipStreamCreateWithPriorityFn>(
              api_.library, "hipStreamCreateWithPriority");
      api_.stream_create_with_cu_mask =
          ResolveHipSymbol<HipExtStreamCreateWithCUMaskFn>(
              api_.library, "hipExtStreamCreateWithCUMask");
      api_.stream_get_cu_mask = ResolveHipSymbol<HipExtStreamGetCUMaskFn>(
          api_.library, "hipExtStreamGetCUMask");
      api_.stream_get_resource = ResolveHipSymbol<HipStreamGetDevResourceFn>(
          api_.library, "hipStreamGetDevResource");
      api_.stream_get_flags = ResolveHipSymbol<HipStreamGetFlagsFn>(
          api_.library, "hipStreamGetFlags");
      api_.stream_get_priority = ResolveHipSymbol<HipStreamGetPriorityFn>(
          api_.library, "hipStreamGetPriority");
      api_.stream_destroy = ResolveHipSymbol<HipStreamDestroyFn>(
          api_.library, "hipStreamDestroy");
    }

    ASSERT_NE(api_.init, nullptr);
    ASSERT_NE(api_.get_device, nullptr);
    ASSERT_NE(api_.get_device_properties, nullptr);
    ASSERT_NE(api_.device_get_attribute, nullptr);
    ASSERT_NE(api_.device_get_resource, nullptr);
    ASSERT_NE(api_.device_execution_context, nullptr);
    ASSERT_NE(api_.split_sm_by_count, nullptr);
    ASSERT_NE(api_.generate_descriptor, nullptr);
    ASSERT_NE(api_.create_context, nullptr);
    ASSERT_NE(api_.destroy_context, nullptr);
    ASSERT_NE(api_.context_get_resource, nullptr);
    ASSERT_NE(api_.context_get_device, nullptr);
    ASSERT_NE(api_.context_get_id, nullptr);
    ASSERT_NE(api_.context_stream_create, nullptr);
    ASSERT_NE(api_.device_get_stream_priority_range, nullptr);
    ASSERT_NE(api_.stream_create_with_priority, nullptr);
    ASSERT_NE(api_.stream_create_with_cu_mask, nullptr);
    ASSERT_NE(api_.stream_get_cu_mask, nullptr);
    ASSERT_NE(api_.stream_get_resource, nullptr);
    ASSERT_NE(api_.stream_get_flags, nullptr);
    ASSERT_NE(api_.stream_get_priority, nullptr);
    ASSERT_NE(api_.stream_destroy, nullptr);

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

TEST_F(HipExecutionResourceApiTest,
       ReturnsStableDeviceManagedExecutionContext) {
  hipExecutionCtx_t first_context = nullptr;
  ASSERT_EQ(hipSuccess, api_.device_execution_context(&first_context, device_));
  ASSERT_NE(first_context, nullptr);
  hipExecutionCtx_t second_context = nullptr;
  ASSERT_EQ(hipSuccess,
            api_.device_execution_context(&second_context, device_));
  EXPECT_EQ(second_context, first_context);

  hipDevResource full_resource;
  ASSERT_EQ(hipSuccess, api_.device_get_resource(device_, &full_resource,
                                                 hipDevResourceTypeSm));
  hipDevResource context_resource;
  ASSERT_EQ(hipSuccess,
            api_.context_get_resource(first_context, &context_resource,
                                      hipDevResourceTypeSm));
  EXPECT_EQ(
      std::memcmp(&context_resource, &full_resource, sizeof(full_resource)), 0);

  hipDevice_t context_device = -1;
  EXPECT_EQ(hipSuccess,
            api_.context_get_device(&context_device, first_context));
  EXPECT_EQ(context_device, device_);
  unsigned long long context_id = 0;
  EXPECT_EQ(hipSuccess, api_.context_get_id(first_context, &context_id));
  EXPECT_NE(context_id, 0u);

  EXPECT_EQ(hipErrorInvalidValue, api_.destroy_context(first_context));
  EXPECT_EQ(hipSuccess, api_.context_get_id(first_context, &context_id));

  hipExecutionCtx_t untouched_context =
      reinterpret_cast<hipExecutionCtx_t>(uintptr_t{1});
  EXPECT_EQ(hipErrorInvalidDevice,
            api_.device_execution_context(&untouched_context, -1));
  EXPECT_EQ(untouched_context,
            reinterpret_cast<hipExecutionCtx_t>(uintptr_t{1}));
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

TEST_F(HipExecutionResourceApiTest,
       CreatesOverlappingContextsWithCanonicalResources) {
  hipDevResource full_resource;
  ASSERT_EQ(hipSuccess, api_.device_get_resource(device_, &full_resource,
                                                 hipDevResourceTypeSm));

  hipDevResourceDesc_t first_descriptor = nullptr;
  ASSERT_EQ(hipSuccess,
            api_.generate_descriptor(&first_descriptor, &full_resource, 1));
  hipDevResourceDesc_t second_descriptor = nullptr;
  ASSERT_EQ(hipSuccess,
            api_.generate_descriptor(&second_descriptor, &full_resource, 1));

  hipExecutionCtx_t first_context = nullptr;
  ASSERT_EQ(hipSuccess, api_.create_context(&first_context, first_descriptor,
                                            device_, /*flags=*/0));
  ScopedExecutionContext first_context_guard(
      first_context, ExecutionContextDeleter{api_.destroy_context});
  hipExecutionCtx_t second_context = nullptr;
  ASSERT_EQ(hipSuccess, api_.create_context(&second_context, second_descriptor,
                                            device_, /*flags=*/0));
  ScopedExecutionContext second_context_guard(
      second_context, ExecutionContextDeleter{api_.destroy_context});

  hipDevResource first_resource;
  ASSERT_EQ(hipSuccess,
            api_.context_get_resource(first_context, &first_resource,
                                      hipDevResourceTypeSm));
  EXPECT_EQ(std::memcmp(&first_resource, &full_resource, sizeof(full_resource)),
            0);
  hipDevResource second_resource;
  ASSERT_EQ(hipSuccess,
            api_.context_get_resource(second_context, &second_resource,
                                      hipDevResourceTypeSm));
  EXPECT_EQ(
      std::memcmp(&second_resource, &full_resource, sizeof(full_resource)), 0);

  hipDevice_t first_device = -1;
  EXPECT_EQ(hipSuccess, api_.context_get_device(&first_device, first_context));
  EXPECT_EQ(first_device, device_);
  unsigned long long first_id = 0;
  unsigned long long second_id = 0;
  EXPECT_EQ(hipSuccess, api_.context_get_id(first_context, &first_id));
  EXPECT_EQ(hipSuccess, api_.context_get_id(second_context, &second_id));
  EXPECT_NE(first_id, 0u);
  EXPECT_NE(second_id, 0u);
  EXPECT_NE(first_id, second_id);
}

TEST_F(HipExecutionResourceApiTest,
       CreatesExactStreamsAndOrphansThemWithTheirContext) {
  hipDevResource full_resource;
  ASSERT_EQ(hipSuccess, api_.device_get_resource(device_, &full_resource,
                                                 hipDevResourceTypeSm));

  hipDevResource partition;
  unsigned int partition_count = 1;
  hipDevResource remainder;
  ASSERT_EQ(hipSuccess,
            api_.split_sm_by_count(&partition, &partition_count, &full_resource,
                                   &remainder, /*flags=*/0,
                                   full_resource.sm.minSmPartitionSize));
  ASSERT_EQ(partition_count, 1u);

  hipDevResourceDesc_t descriptor = nullptr;
  ASSERT_EQ(hipSuccess, api_.generate_descriptor(&descriptor, &partition, 1));
  hipExecutionCtx_t context = nullptr;
  ASSERT_EQ(hipSuccess, api_.create_context(&context, descriptor, device_, 0));
  ScopedExecutionContext context_guard(
      context, ExecutionContextDeleter{api_.destroy_context});

  hipStream_t untouched_stream =
      reinterpret_cast<hipStream_t>(uintptr_t{0x1234});
  EXPECT_EQ(hipErrorInvalidValue,
            api_.context_stream_create(&untouched_stream, context,
                                       /*flags=*/2, /*priority=*/0));
  EXPECT_EQ(untouched_stream, reinterpret_cast<hipStream_t>(uintptr_t{0x1234}));

  hipStream_t stream = nullptr;
  ASSERT_EQ(hipSuccess,
            api_.context_stream_create(&stream, context, hipStreamDefault,
                                       /*priority=*/0));
  ScopedStream stream_guard(stream, StreamDeleter{api_.stream_destroy});

  unsigned int stream_flags = 0;
  ASSERT_EQ(hipSuccess, api_.stream_get_flags(stream, &stream_flags));
  EXPECT_EQ(stream_flags, hipStreamNonBlocking);

  hipDevResource context_resource;
  ASSERT_EQ(hipSuccess, api_.context_get_resource(context, &context_resource,
                                                  hipDevResourceTypeSm));
  hipDevResource stream_resource;
  ASSERT_EQ(hipSuccess, api_.stream_get_resource(stream, &stream_resource,
                                                 hipDevResourceTypeSm));
  EXPECT_EQ(std::memcmp(&stream_resource, &context_resource,
                        sizeof(context_resource)),
            0);

  // CU-mask streams use the same canonical partition as resource-context
  // streams instead of projecting a second mask model into the HAL.
  const uint32_t mask_count = (full_resource.sm.smCount + 31u) / 32u;
  std::vector<uint32_t> mask(mask_count, 0u);
  ASSERT_EQ(hipSuccess,
            api_.stream_get_cu_mask(stream, mask_count, mask.data()));
  hipStream_t masked_stream = nullptr;
  ASSERT_EQ(hipSuccess, api_.stream_create_with_cu_mask(
                            &masked_stream, mask_count, mask.data()));
  ScopedStream masked_stream_guard(masked_stream,
                                   StreamDeleter{api_.stream_destroy});
  hipDevResource masked_stream_resource;
  ASSERT_EQ(hipSuccess,
            api_.stream_get_resource(masked_stream, &masked_stream_resource,
                                     hipDevResourceTypeSm));
  EXPECT_EQ(std::memcmp(&masked_stream_resource, &context_resource,
                        sizeof(context_resource)),
            0);

  ASSERT_EQ(hipSuccess, api_.destroy_context(context_guard.release()));

  std::memset(&stream_resource, 0xA5, sizeof(stream_resource));
  const hipDevResource expected_resource = stream_resource;
  EXPECT_EQ(
      hipErrorContextIsDestroyed,
      api_.stream_get_resource(stream, &stream_resource, hipDevResourceTypeSm));
  EXPECT_EQ(std::memcmp(&stream_resource, &expected_resource,
                        sizeof(stream_resource)),
            0);
  EXPECT_EQ(hipSuccess, api_.stream_destroy(stream_guard.release()));
}

TEST_F(HipExecutionResourceApiTest, CreatesStreamsAtClampedHardwarePriorities) {
  int least_priority = 7;
  int greatest_priority = 7;
  ASSERT_EQ(hipSuccess, api_.device_get_stream_priority_range(
                            &least_priority, &greatest_priority));
  ASSERT_LT(greatest_priority, least_priority);

  hipDeviceProp_t properties;
  std::memset(&properties, 0, sizeof(properties));
  ASSERT_EQ(hipSuccess, api_.get_device_properties(&properties, device_));
  EXPECT_TRUE(properties.streamPrioritiesSupported);

  int priorities_supported = 0;
  ASSERT_EQ(hipSuccess,
            api_.device_get_attribute(
                &priorities_supported,
                hipDeviceAttributeStreamPrioritiesSupported, device_));
  EXPECT_EQ(priorities_supported, 1);

  hipStream_t high_priority_stream = nullptr;
  ASSERT_EQ(hipSuccess, api_.stream_create_with_priority(
                            &high_priority_stream, hipStreamNonBlocking,
                            greatest_priority - 1));
  ScopedStream high_priority_stream_guard(high_priority_stream,
                                          StreamDeleter{api_.stream_destroy});

  int actual_priority = 7;
  ASSERT_EQ(hipSuccess,
            api_.stream_get_priority(high_priority_stream, &actual_priority));
  EXPECT_EQ(actual_priority, greatest_priority);

  hipStream_t low_priority_stream = nullptr;
  ASSERT_EQ(hipSuccess, api_.stream_create_with_priority(&low_priority_stream,
                                                         hipStreamNonBlocking,
                                                         least_priority + 1));
  ScopedStream low_priority_stream_guard(low_priority_stream,
                                         StreamDeleter{api_.stream_destroy});
  actual_priority = 7;
  ASSERT_EQ(hipSuccess,
            api_.stream_get_priority(low_priority_stream, &actual_priority));
  EXPECT_EQ(actual_priority, least_priority);

  hipDevResource full_resource;
  ASSERT_EQ(hipSuccess, api_.device_get_resource(device_, &full_resource,
                                                 hipDevResourceTypeSm));
  hipDevResource stream_resource;
  ASSERT_EQ(hipSuccess,
            api_.stream_get_resource(high_priority_stream, &stream_resource,
                                     hipDevResourceTypeSm));
  EXPECT_EQ(
      std::memcmp(&stream_resource, &full_resource, sizeof(full_resource)), 0);
}

TEST_F(HipExecutionResourceApiTest,
       ConsumesDescriptorsOnlyForSuccessfulCreation) {
  hipDevResource full_resource;
  ASSERT_EQ(hipSuccess, api_.device_get_resource(device_, &full_resource,
                                                 hipDevResourceTypeSm));
  hipDevResourceDesc_t descriptor = nullptr;
  ASSERT_EQ(hipSuccess,
            api_.generate_descriptor(&descriptor, &full_resource, 1));

  hipExecutionCtx_t untouched_context =
      reinterpret_cast<hipExecutionCtx_t>(uintptr_t{1});
  EXPECT_EQ(hipErrorInvalidValue,
            api_.create_context(&untouched_context, descriptor, device_,
                                /*flags=*/1));
  EXPECT_EQ(untouched_context,
            reinterpret_cast<hipExecutionCtx_t>(uintptr_t{1}));

  hipExecutionCtx_t context = nullptr;
  ASSERT_EQ(hipSuccess, api_.create_context(&context, descriptor, device_,
                                            /*flags=*/0));
  ScopedExecutionContext context_guard(
      context, ExecutionContextDeleter{api_.destroy_context});

  untouched_context = reinterpret_cast<hipExecutionCtx_t>(uintptr_t{1});
  EXPECT_EQ(hipErrorInvalidValue,
            api_.create_context(&untouched_context, descriptor, device_,
                                /*flags=*/0));
  EXPECT_EQ(untouched_context,
            reinterpret_cast<hipExecutionCtx_t>(uintptr_t{1}));

  hipDevResource untouched_resource;
  std::memset(&untouched_resource, 0xA5, sizeof(untouched_resource));
  const hipDevResource expected_resource = untouched_resource;
  EXPECT_EQ(hipErrorInvalidResourceType,
            api_.context_get_resource(context, &untouched_resource,
                                      hipDevResourceTypeWorkqueueConfig));
  EXPECT_EQ(std::memcmp(&untouched_resource, &expected_resource,
                        sizeof(untouched_resource)),
            0);

  ASSERT_EQ(hipSuccess, api_.destroy_context(context_guard.release()));
  hipDevice_t untouched_device = -7;
  EXPECT_EQ(hipErrorInvalidValue,
            api_.context_get_device(&untouched_device, context));
  EXPECT_EQ(untouched_device, -7);
  EXPECT_EQ(hipErrorInvalidValue, api_.destroy_context(context));
}

}  // namespace
