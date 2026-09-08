// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <array>
#include <cstdint>
#include <cstring>

#include "amdf/amdf.h"
#include "amdf/gpu.h"
#include "gpu_device_fixture.h"
#include "gtest/gtest.h"

namespace {

constexpr uint64_t kCommandByteOffset = 0;
constexpr uint64_t kStagedCommandByteOffset = 512;
constexpr uint64_t kVerifiedCommandByteOffset = 640;
constexpr uint64_t kSourceByteOffset = 1024;
constexpr uint64_t kTargetByteOffset = 1088;
constexpr uint64_t kLocalByteOffset = 256;
constexpr uint64_t kMemoryByteLength = 4096;
constexpr uint32_t kCopyDataDwordCount = 6;
constexpr uint32_t kSdmaCopyDwordCount = 7;

uint32_t MakePm4Header(uint32_t opcode, uint32_t dword_count) {
  return (3u << 30) | (opcode << 8) | ((dword_count - 2u) << 16);
}

std::array<uint32_t, kCopyDataDwordCount> MakeCopyData32(
    uint64_t source_address, uint64_t target_address) {
  constexpr uint32_t kCopyDataOpcode = 0x40;
  constexpr uint32_t kSourceTcL2 = 2u << 0;
  constexpr uint32_t kTargetTcL2 = 2u << 8;
  constexpr uint32_t kWaitForWriteConfirmation = 1u << 20;
  return {
      MakePm4Header(kCopyDataOpcode, kCopyDataDwordCount),
      kSourceTcL2 | kTargetTcL2 | kWaitForWriteConfirmation,
      static_cast<uint32_t>(source_address & UINT64_C(0xFFFFFFFC)),
      static_cast<uint32_t>(source_address >> 32),
      static_cast<uint32_t>(target_address & UINT64_C(0xFFFFFFFC)),
      static_cast<uint32_t>(target_address >> 32),
  };
}

std::array<uint32_t, kSdmaCopyDwordCount> MakeSdmaCopy32(
    const amdf_gpu_endpoint_info_t& endpoint_info, uint64_t source_address,
    uint64_t target_address) {
  constexpr uint32_t kSdmaCopyLinearOpcode = 1;
  constexpr uint32_t kSystemScope = 3;
  const bool has_scope_fields =
      endpoint_info.gfx_ip.major == 12 && endpoint_info.gfx_ip.minor >= 5;
  const uint32_t scope_fields =
      has_scope_fields ? (kSystemScope << 18) | (kSystemScope << 26) : 0;
  return {
      kSdmaCopyLinearOpcode,
      sizeof(uint32_t) - 1,
      scope_fields,
      static_cast<uint32_t>(source_address),
      static_cast<uint32_t>(source_address >> 32),
      static_cast<uint32_t>(target_address),
      static_cast<uint32_t>(target_address >> 32),
  };
}

class GpuKernelQueueTest : public GpuDeviceFixture {
 protected:
  void TearDown() override {
    if (mapping_ != nullptr && !indirect_memory_may_be_in_use_) {
      EXPECT_TRUE(amdf_status_is_ok(api_->host_mapping_destroy(mapping_)));
    }
    if (queue_ != nullptr) {
      EXPECT_TRUE(amdf_status_is_ok(api_->kernel_queue_destroy(queue_)));
    }
    if (local_memory_ != nullptr && !indirect_memory_may_be_in_use_) {
      EXPECT_TRUE(amdf_status_is_ok(api_->memory_destroy(local_memory_)));
    }
    if (memory_ != nullptr && !indirect_memory_may_be_in_use_) {
      EXPECT_TRUE(amdf_status_is_ok(api_->memory_destroy(memory_)));
    }
    GpuDeviceFixture::TearDown();
  }

  uint32_t FindKernelQueueFamily(amdf_queue_command_type_t command_type) {
    amdf_endpoint_info_t endpoint_info = {};
    endpoint_info.type = AMDF_STRUCTURE_TYPE_ENDPOINT_INFO;
    endpoint_info.structure_size = sizeof(endpoint_info);
    EXPECT_TRUE(amdf_status_is_ok(
        api_->endpoint_query_info(endpoint_, &endpoint_info)));
    for (uint32_t ordinal = 0; ordinal < endpoint_info.queue_family_count;
         ++ordinal) {
      amdf_queue_family_info_t family_info = {};
      family_info.type = AMDF_STRUCTURE_TYPE_QUEUE_FAMILY_INFO;
      family_info.structure_size = sizeof(family_info);
      EXPECT_TRUE(amdf_status_is_ok(api_->endpoint_query_queue_family_info(
          endpoint_, ordinal, &family_info)));
      if (family_info.command_type == command_type &&
          (family_info.publication_modes &
           AMDF_QUEUE_PUBLICATION_MODE_KERNEL) != 0) {
        return ordinal;
      }
    }
    return UINT32_MAX;
  }

  amdf_status_t CreateQueue(uint32_t family_ordinal) {
    amdf_gpu_kernel_queue_create_info_t create_info = {};
    create_info.type = AMDF_STRUCTURE_TYPE_GPU_KERNEL_QUEUE_CREATE_INFO;
    create_info.structure_size = sizeof(create_info);
    create_info.queue_family_ordinal = family_ordinal;
    return gpu_api_->kernel_queue_create(device_, &create_info, &queue_);
  }

  amdf_status_t QueryGpuEndpointInfo(amdf_gpu_endpoint_info_t* out_info) {
    *out_info = {};
    out_info->type = AMDF_STRUCTURE_TYPE_GPU_ENDPOINT_INFO;
    out_info->structure_size = sizeof(*out_info);
    return gpu_api_->endpoint_query_info(endpoint_, out_info);
  }

  amdf_memory_info_t CreateCommandMemory() {
    amdf_memory_create_info_t create_info = {};
    create_info.type = AMDF_STRUCTURE_TYPE_MEMORY_CREATE_INFO;
    create_info.structure_size = sizeof(create_info);
    create_info.memory_class = AMDF_MEMORY_CLASS_SYSTEM;
    create_info.required_flags = AMDF_MEMORY_FLAG_HOST_VISIBLE |
                                 AMDF_MEMORY_FLAG_EXECUTABLE |
                                 AMDF_MEMORY_FLAG_DEVICE_ADDRESS;
    create_info.byte_length = kMemoryByteLength;
    EXPECT_TRUE(amdf_status_is_ok(
        api_->memory_create(device_, &create_info, &memory_)));

    amdf_memory_info_t memory_info = {};
    memory_info.type = AMDF_STRUCTURE_TYPE_MEMORY_INFO;
    memory_info.structure_size = sizeof(memory_info);
    EXPECT_TRUE(
        amdf_status_is_ok(api_->memory_query_info(memory_, &memory_info)));
    return memory_info;
  }

  amdf_host_mapping_info_t MapCommandMemory() {
    amdf_memory_map_info_t map_info = {};
    map_info.type = AMDF_STRUCTURE_TYPE_MEMORY_MAP_INFO;
    map_info.structure_size = sizeof(map_info);
    map_info.byte_length = kMemoryByteLength;
    map_info.flags = AMDF_MEMORY_MAP_FLAG_READ | AMDF_MEMORY_MAP_FLAG_WRITE;
    EXPECT_TRUE(
        amdf_status_is_ok(api_->memory_map(memory_, &map_info, &mapping_)));

    amdf_host_mapping_info_t mapping_info = {};
    mapping_info.type = AMDF_STRUCTURE_TYPE_HOST_MAPPING_INFO;
    mapping_info.structure_size = sizeof(mapping_info);
    EXPECT_TRUE(amdf_status_is_ok(
        api_->host_mapping_query_info(mapping_, &mapping_info)));
    return mapping_info;
  }

  amdf_memory_info_t CreateLocalExecutableMemory() {
    amdf_memory_create_info_t create_info = {};
    create_info.type = AMDF_STRUCTURE_TYPE_MEMORY_CREATE_INFO;
    create_info.structure_size = sizeof(create_info);
    create_info.memory_class = AMDF_MEMORY_CLASS_LOCAL;
    create_info.required_flags = AMDF_MEMORY_FLAG_DEVICE_LOCAL |
                                 AMDF_MEMORY_FLAG_EXECUTABLE |
                                 AMDF_MEMORY_FLAG_DEVICE_ADDRESS;
    create_info.byte_length = kMemoryByteLength;
    EXPECT_TRUE(amdf_status_is_ok(
        api_->memory_create(device_, &create_info, &local_memory_)));

    amdf_memory_info_t memory_info = {};
    memory_info.type = AMDF_STRUCTURE_TYPE_MEMORY_INFO;
    memory_info.structure_size = sizeof(memory_info);
    EXPECT_TRUE(amdf_status_is_ok(
        api_->memory_query_info(local_memory_, &memory_info)));
    EXPECT_EQ(memory_info.flags & create_info.required_flags,
              create_info.required_flags);
    EXPECT_GE(memory_info.byte_length, kMemoryByteLength);
    return memory_info;
  }

  amdf_memory_t* memory_ = nullptr;
  amdf_memory_t* local_memory_ = nullptr;
  amdf_host_mapping_t* mapping_ = nullptr;
  amdf_kernel_queue_t* queue_ = nullptr;
  bool indirect_memory_may_be_in_use_ = false;
};

TEST_F(GpuKernelQueueTest, ExecutesMaterializedCopyData) {
  const uint32_t family_ordinal =
      FindKernelQueueFamily(AMDF_QUEUE_COMMAND_TYPE_GPU_PM4);
  if (family_ordinal == UINT32_MAX) {
    GTEST_SKIP() << "GPU endpoint exposes no kernel-mediated PM4 queue";
  }
  ASSERT_TRUE(amdf_status_is_ok(CreateQueue(family_ordinal)));
  EXPECT_EQ(amdf_status_code(api_->device_destroy(device_)),
            AMDF_STATUS_CODE_BUSY);

  amdf_kernel_queue_info_t queue_info = {};
  queue_info.type = AMDF_STRUCTURE_TYPE_KERNEL_QUEUE_INFO;
  queue_info.structure_size = sizeof(queue_info);
  ASSERT_TRUE(
      amdf_status_is_ok(api_->kernel_queue_query_info(queue_, &queue_info)));
  EXPECT_EQ(queue_info.queue_family_ordinal, family_ordinal);
  EXPECT_EQ(queue_info.command_type, AMDF_QUEUE_COMMAND_TYPE_GPU_PM4);
  EXPECT_EQ(queue_info.maximum_pending_submission_count, 1u);
  EXPECT_EQ(queue_info.maximum_command_count, 1u);

  const amdf_memory_info_t memory_info = CreateCommandMemory();
  ASSERT_NE(memory_, nullptr);
  const amdf_host_mapping_info_t mapping_info = MapCommandMemory();
  ASSERT_NE(mapping_, nullptr);
  ASSERT_NE(mapping_info.pointer, nullptr);
  ASSERT_GE(mapping_info.byte_length, kMemoryByteLength);

  constexpr uint32_t kSourceValue = 0x13579BDFu;
  constexpr uint32_t kTargetSentinel = 0xA5A5A5A5u;
  uint8_t* const bytes = static_cast<uint8_t*>(mapping_info.pointer);
  std::memcpy(bytes + kSourceByteOffset, &kSourceValue, sizeof(kSourceValue));
  std::memcpy(bytes + kTargetByteOffset, &kTargetSentinel,
              sizeof(kTargetSentinel));
  const std::array<uint32_t, kCopyDataDwordCount> command =
      MakeCopyData32(memory_info.device_address + kSourceByteOffset,
                     memory_info.device_address + kTargetByteOffset);
  std::memcpy(bytes + kCommandByteOffset, command.data(), sizeof(command));
  ASSERT_TRUE(amdf_status_is_ok(api_->host_mapping_cache_control(
      mapping_, AMDF_HOST_CACHE_OPERATION_FLUSH, 0, kMemoryByteLength)));

  amdf_gpu_kernel_command_t command_descriptor = {};
  command_descriptor.memory = memory_;
  command_descriptor.byte_offset = kCommandByteOffset;
  command_descriptor.byte_length = sizeof(command);
  amdf_gpu_kernel_queue_submission_info_t submission_info = {};
  submission_info.type = AMDF_STRUCTURE_TYPE_GPU_KERNEL_QUEUE_SUBMISSION_INFO;
  submission_info.structure_size = sizeof(submission_info);
  submission_info.command_count = 1;
  submission_info.commands = &command_descriptor;

  command_descriptor.byte_length = 0;
  uint64_t invalid_submission = 42;
  EXPECT_EQ(amdf_status_code(gpu_api_->kernel_queue_submit(
                queue_, &submission_info, &invalid_submission)),
            AMDF_STATUS_CODE_INVALID_ARGUMENT);
  EXPECT_EQ(invalid_submission, 42u);
  command_descriptor.byte_length = sizeof(command);

  uint64_t submission = 42;
  ASSERT_TRUE(amdf_status_is_ok(
      gpu_api_->kernel_queue_submit(queue_, &submission_info, &submission)));
  EXPECT_EQ(submission, 1u);

  uint64_t rejected_submission = 42;
  EXPECT_EQ(amdf_status_code(gpu_api_->kernel_queue_submit(
                queue_, &submission_info, &rejected_submission)),
            AMDF_STATUS_CODE_BUSY);
  EXPECT_EQ(rejected_submission, 42u);

  ASSERT_TRUE(amdf_status_is_ok(api_->host_mapping_destroy(mapping_)));
  mapping_ = nullptr;
  EXPECT_EQ(amdf_status_code(api_->memory_destroy(memory_)),
            AMDF_STATUS_CODE_BUSY);

  ASSERT_TRUE(amdf_status_is_ok(api_->kernel_queue_wait(
      queue_, submission, UINT64_C(5000000000), UINT64_C(50000))));
  amdf_kernel_queue_status_t queue_status = {};
  queue_status.type = AMDF_STRUCTURE_TYPE_KERNEL_QUEUE_STATUS;
  queue_status.structure_size = sizeof(queue_status);
  ASSERT_TRUE(amdf_status_is_ok(
      api_->kernel_queue_query_status(queue_, &queue_status)));
  EXPECT_EQ(queue_status.retired_submission, submission);
  EXPECT_EQ(queue_status.state, AMDF_KERNEL_QUEUE_STATE_ACTIVE);

  const amdf_host_mapping_info_t result_mapping_info = MapCommandMemory();
  ASSERT_NE(mapping_, nullptr);
  ASSERT_TRUE(amdf_status_is_ok(api_->host_mapping_cache_control(
      mapping_, AMDF_HOST_CACHE_OPERATION_INVALIDATE, kTargetByteOffset,
      sizeof(uint32_t))));
  uint32_t target_value = 0;
  std::memcpy(
      &target_value,
      static_cast<uint8_t*>(result_mapping_info.pointer) + kTargetByteOffset,
      sizeof(target_value));
  EXPECT_EQ(target_value, kSourceValue);

  ASSERT_TRUE(amdf_status_is_ok(api_->host_mapping_destroy(mapping_)));
  mapping_ = nullptr;
  ASSERT_TRUE(amdf_status_is_ok(api_->memory_destroy(memory_)));
  memory_ = nullptr;
  ASSERT_TRUE(amdf_status_is_ok(api_->kernel_queue_destroy(queue_)));
  queue_ = nullptr;
}

TEST_F(GpuKernelQueueTest, ExecutesMaterializedSdmaCopy) {
  const uint32_t family_ordinal =
      FindKernelQueueFamily(AMDF_QUEUE_COMMAND_TYPE_GPU_SDMA);
  if (family_ordinal == UINT32_MAX) {
    GTEST_SKIP() << "GPU endpoint exposes no kernel-mediated SDMA queue";
  }
  const amdf_status_t create_status = CreateQueue(family_ordinal);
  ASSERT_TRUE(amdf_status_is_ok(create_status))
      << "domain=" << amdf_status_domain(create_status)
      << " code=" << amdf_status_code(create_status);

  amdf_kernel_queue_info_t queue_info = {};
  queue_info.type = AMDF_STRUCTURE_TYPE_KERNEL_QUEUE_INFO;
  queue_info.structure_size = sizeof(queue_info);
  ASSERT_TRUE(
      amdf_status_is_ok(api_->kernel_queue_query_info(queue_, &queue_info)));
  EXPECT_EQ(queue_info.queue_family_ordinal, family_ordinal);
  EXPECT_EQ(queue_info.command_type, AMDF_QUEUE_COMMAND_TYPE_GPU_SDMA);

  const amdf_memory_info_t memory_info = CreateCommandMemory();
  ASSERT_NE(memory_, nullptr);
  const amdf_host_mapping_info_t mapping_info = MapCommandMemory();
  ASSERT_NE(mapping_, nullptr);
  ASSERT_NE(mapping_info.pointer, nullptr);

  constexpr uint32_t kSourceValue = 0x89ABCDEFu;
  constexpr uint32_t kTargetSentinel = 0xA5A5A5A5u;
  uint8_t* const bytes = static_cast<uint8_t*>(mapping_info.pointer);
  std::memcpy(bytes + kSourceByteOffset, &kSourceValue, sizeof(kSourceValue));
  std::memcpy(bytes + kTargetByteOffset, &kTargetSentinel,
              sizeof(kTargetSentinel));
  amdf_gpu_endpoint_info_t endpoint_info = {};
  ASSERT_TRUE(amdf_status_is_ok(QueryGpuEndpointInfo(&endpoint_info)));
  const std::array<uint32_t, kSdmaCopyDwordCount> command = MakeSdmaCopy32(
      endpoint_info, memory_info.device_address + kSourceByteOffset,
      memory_info.device_address + kTargetByteOffset);
  std::memcpy(bytes + kCommandByteOffset, command.data(), sizeof(command));
  ASSERT_TRUE(amdf_status_is_ok(api_->host_mapping_cache_control(
      mapping_, AMDF_HOST_CACHE_OPERATION_FLUSH, 0, kMemoryByteLength)));

  const amdf_gpu_kernel_command_t command_descriptor = {
      memory_,
      kCommandByteOffset,
      sizeof(command),
  };
  amdf_gpu_kernel_queue_submission_info_t submission_info = {};
  submission_info.type = AMDF_STRUCTURE_TYPE_GPU_KERNEL_QUEUE_SUBMISSION_INFO;
  submission_info.structure_size = sizeof(submission_info);
  submission_info.command_count = 1;
  submission_info.commands = &command_descriptor;
  uint64_t submission = 0;
  ASSERT_TRUE(amdf_status_is_ok(
      gpu_api_->kernel_queue_submit(queue_, &submission_info, &submission)));
  indirect_memory_may_be_in_use_ = true;
  ASSERT_TRUE(amdf_status_is_ok(api_->kernel_queue_wait(
      queue_, submission, UINT64_C(5000000000), UINT64_C(50000))));
  indirect_memory_may_be_in_use_ = false;

  ASSERT_TRUE(amdf_status_is_ok(api_->host_mapping_cache_control(
      mapping_, AMDF_HOST_CACHE_OPERATION_INVALIDATE, kTargetByteOffset,
      sizeof(uint32_t))));
  uint32_t target_value = 0;
  std::memcpy(&target_value, bytes + kTargetByteOffset, sizeof(target_value));
  EXPECT_EQ(target_value, kSourceValue);

  ASSERT_TRUE(amdf_status_is_ok(api_->host_mapping_destroy(mapping_)));
  mapping_ = nullptr;
  ASSERT_TRUE(amdf_status_is_ok(api_->kernel_queue_destroy(queue_)));
  queue_ = nullptr;
  ASSERT_TRUE(amdf_status_is_ok(api_->memory_destroy(memory_)));
  memory_ = nullptr;
}

TEST_F(GpuKernelQueueTest, CopiesThroughDeviceLocalExecutableMemory) {
  const uint32_t family_ordinal =
      FindKernelQueueFamily(AMDF_QUEUE_COMMAND_TYPE_GPU_PM4);
  if (family_ordinal == UINT32_MAX) {
    GTEST_SKIP() << "GPU endpoint exposes no kernel-mediated PM4 queue";
  }
  ASSERT_TRUE(amdf_status_is_ok(CreateQueue(family_ordinal)));

  const amdf_memory_info_t memory_info = CreateCommandMemory();
  ASSERT_NE(memory_, nullptr);
  const amdf_host_mapping_info_t mapping_info = MapCommandMemory();
  ASSERT_NE(mapping_, nullptr);
  ASSERT_NE(mapping_info.pointer, nullptr);
  ASSERT_GE(mapping_info.byte_length, kMemoryByteLength);

  const amdf_memory_info_t local_memory_info = CreateLocalExecutableMemory();
  ASSERT_NE(local_memory_, nullptr);
  ASSERT_NE(local_memory_info.device_address, 0u);

  constexpr uint32_t kSourceValue = 0x2468ACE0u;
  constexpr uint32_t kTargetSentinel = 0xA5A5A5A5u;
  uint8_t* const bytes = static_cast<uint8_t*>(mapping_info.pointer);
  std::memcpy(bytes + kSourceByteOffset, &kSourceValue, sizeof(kSourceValue));
  std::memcpy(bytes + kTargetByteOffset, &kTargetSentinel,
              sizeof(kTargetSentinel));
  const std::array<uint32_t, kCopyDataDwordCount> upload_command =
      MakeCopyData32(memory_info.device_address + kSourceByteOffset,
                     local_memory_info.device_address + kLocalByteOffset);
  const std::array<uint32_t, kCopyDataDwordCount> download_command =
      MakeCopyData32(local_memory_info.device_address + kLocalByteOffset,
                     memory_info.device_address + kTargetByteOffset);
  std::array<uint32_t, 2 * kCopyDataDwordCount> command = {};
  std::memcpy(command.data(), upload_command.data(), sizeof(upload_command));
  std::memcpy(command.data() + kCopyDataDwordCount, download_command.data(),
              sizeof(download_command));
  std::memcpy(bytes + kCommandByteOffset, command.data(), sizeof(command));
  ASSERT_TRUE(amdf_status_is_ok(api_->host_mapping_cache_control(
      mapping_, AMDF_HOST_CACHE_OPERATION_FLUSH, 0, kMemoryByteLength)));

  const amdf_gpu_kernel_command_t command_descriptor = {
      memory_,
      kCommandByteOffset,
      sizeof(command),
  };
  amdf_gpu_kernel_queue_submission_info_t submission_info = {};
  submission_info.type = AMDF_STRUCTURE_TYPE_GPU_KERNEL_QUEUE_SUBMISSION_INFO;
  submission_info.structure_size = sizeof(submission_info);
  submission_info.command_count = 1;
  submission_info.commands = &command_descriptor;
  uint64_t submission = 0;
  ASSERT_TRUE(amdf_status_is_ok(
      gpu_api_->kernel_queue_submit(queue_, &submission_info, &submission)));
  indirect_memory_may_be_in_use_ = true;

  ASSERT_TRUE(amdf_status_is_ok(api_->kernel_queue_wait(
      queue_, submission, UINT64_C(5000000000), UINT64_C(50000))));
  indirect_memory_may_be_in_use_ = false;
  ASSERT_TRUE(amdf_status_is_ok(api_->host_mapping_cache_control(
      mapping_, AMDF_HOST_CACHE_OPERATION_INVALIDATE, kTargetByteOffset,
      sizeof(uint32_t))));
  uint32_t target_value = 0;
  std::memcpy(&target_value, bytes + kTargetByteOffset, sizeof(target_value));
  EXPECT_EQ(target_value, kSourceValue);

  ASSERT_TRUE(amdf_status_is_ok(api_->host_mapping_destroy(mapping_)));
  mapping_ = nullptr;
  ASSERT_TRUE(amdf_status_is_ok(api_->kernel_queue_destroy(queue_)));
  queue_ = nullptr;
  ASSERT_TRUE(amdf_status_is_ok(api_->memory_destroy(local_memory_)));
  local_memory_ = nullptr;
  ASSERT_TRUE(amdf_status_is_ok(api_->memory_destroy(memory_)));
  memory_ = nullptr;
}

TEST_F(GpuKernelQueueTest, ExecutesDeviceLocalCommandStream) {
  const uint32_t family_ordinal =
      FindKernelQueueFamily(AMDF_QUEUE_COMMAND_TYPE_GPU_PM4);
  if (family_ordinal == UINT32_MAX) {
    GTEST_SKIP() << "GPU endpoint exposes no kernel-mediated PM4 queue";
  }
  ASSERT_TRUE(amdf_status_is_ok(CreateQueue(family_ordinal)));

  const amdf_memory_info_t memory_info = CreateCommandMemory();
  ASSERT_NE(memory_, nullptr);
  const amdf_host_mapping_info_t mapping_info = MapCommandMemory();
  ASSERT_NE(mapping_, nullptr);
  ASSERT_NE(mapping_info.pointer, nullptr);
  ASSERT_GE(mapping_info.byte_length, kMemoryByteLength);
  const amdf_memory_info_t local_memory_info = CreateLocalExecutableMemory();
  ASSERT_NE(local_memory_, nullptr);
  ASSERT_NE(local_memory_info.device_address, 0u);

  constexpr uint32_t kSourceValue = 0x10203040u;
  constexpr uint32_t kTargetSentinel = 0xA5A5A5A5u;
  uint8_t* const bytes = static_cast<uint8_t*>(mapping_info.pointer);
  std::memcpy(bytes + kSourceByteOffset, &kSourceValue, sizeof(kSourceValue));
  std::memcpy(bytes + kTargetByteOffset, &kTargetSentinel,
              sizeof(kTargetSentinel));

  const std::array<uint32_t, kCopyDataDwordCount> local_command =
      MakeCopyData32(memory_info.device_address + kSourceByteOffset,
                     memory_info.device_address + kTargetByteOffset);
  std::memcpy(bytes + kStagedCommandByteOffset, local_command.data(),
              sizeof(local_command));
  std::array<uint32_t, 2 * kCopyDataDwordCount * kCopyDataDwordCount>
      upload_command = {};
  for (uint32_t i = 0; i < kCopyDataDwordCount; ++i) {
    const std::array<uint32_t, kCopyDataDwordCount> upload_word =
        MakeCopyData32(memory_info.device_address + kStagedCommandByteOffset +
                           i * sizeof(uint32_t),
                       local_memory_info.device_address + kLocalByteOffset +
                           i * sizeof(uint32_t));
    const std::array<uint32_t, kCopyDataDwordCount> verify_word =
        MakeCopyData32(local_memory_info.device_address + kLocalByteOffset +
                           i * sizeof(uint32_t),
                       memory_info.device_address + kVerifiedCommandByteOffset +
                           i * sizeof(uint32_t));
    std::memcpy(upload_command.data() + 2 * i * kCopyDataDwordCount,
                upload_word.data(), sizeof(upload_word));
    std::memcpy(upload_command.data() + (2 * i + 1) * kCopyDataDwordCount,
                verify_word.data(), sizeof(verify_word));
  }
  std::memcpy(bytes + kCommandByteOffset, upload_command.data(),
              sizeof(upload_command));
  ASSERT_TRUE(amdf_status_is_ok(api_->host_mapping_cache_control(
      mapping_, AMDF_HOST_CACHE_OPERATION_FLUSH, 0, kMemoryByteLength)));

  const amdf_gpu_kernel_command_t upload_descriptor = {
      memory_,
      kCommandByteOffset,
      sizeof(upload_command),
  };
  amdf_gpu_kernel_queue_submission_info_t submission_info = {};
  submission_info.type = AMDF_STRUCTURE_TYPE_GPU_KERNEL_QUEUE_SUBMISSION_INFO;
  submission_info.structure_size = sizeof(submission_info);
  submission_info.command_count = 1;
  submission_info.commands = &upload_descriptor;
  uint64_t upload_submission = 0;
  ASSERT_TRUE(amdf_status_is_ok(gpu_api_->kernel_queue_submit(
      queue_, &submission_info, &upload_submission)));
  indirect_memory_may_be_in_use_ = true;
  ASSERT_TRUE(amdf_status_is_ok(api_->kernel_queue_wait(
      queue_, upload_submission, UINT64_C(5000000000), UINT64_C(50000))));
  indirect_memory_may_be_in_use_ = false;
  ASSERT_TRUE(amdf_status_is_ok(api_->host_mapping_cache_control(
      mapping_, AMDF_HOST_CACHE_OPERATION_INVALIDATE,
      kVerifiedCommandByteOffset, sizeof(local_command))));
  EXPECT_EQ(std::memcmp(bytes + kVerifiedCommandByteOffset,
                        local_command.data(), sizeof(local_command)),
            0);

  const amdf_gpu_kernel_command_t local_command_descriptor = {
      local_memory_,
      kLocalByteOffset,
      sizeof(local_command),
  };
  submission_info.commands = &local_command_descriptor;
  uint64_t local_submission = 0;
  ASSERT_TRUE(amdf_status_is_ok(gpu_api_->kernel_queue_submit(
      queue_, &submission_info, &local_submission)));
  indirect_memory_may_be_in_use_ = true;
  EXPECT_EQ(amdf_status_code(api_->memory_destroy(local_memory_)),
            AMDF_STATUS_CODE_BUSY);
  ASSERT_TRUE(amdf_status_is_ok(api_->kernel_queue_wait(
      queue_, local_submission, UINT64_C(5000000000), UINT64_C(50000))));
  indirect_memory_may_be_in_use_ = false;

  ASSERT_TRUE(amdf_status_is_ok(api_->host_mapping_cache_control(
      mapping_, AMDF_HOST_CACHE_OPERATION_INVALIDATE, kTargetByteOffset,
      sizeof(uint32_t))));
  uint32_t target_value = 0;
  std::memcpy(&target_value, bytes + kTargetByteOffset, sizeof(target_value));
  EXPECT_EQ(target_value, kSourceValue);

  ASSERT_TRUE(amdf_status_is_ok(api_->host_mapping_destroy(mapping_)));
  mapping_ = nullptr;
  ASSERT_TRUE(amdf_status_is_ok(api_->kernel_queue_destroy(queue_)));
  queue_ = nullptr;
  ASSERT_TRUE(amdf_status_is_ok(api_->memory_destroy(local_memory_)));
  local_memory_ = nullptr;
  ASSERT_TRUE(amdf_status_is_ok(api_->memory_destroy(memory_)));
  memory_ = nullptr;
}

}  // namespace
