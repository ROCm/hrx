// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "experimental/xdna/prepared_command.h"

#include <array>
#include <cstdint>
#include <vector>

#include "experimental/xdna/executable.h"
#include "experimental/xdna/fake_provider.h"
#include "iree/hal/drivers/amd/xdna/image/aie2p/strix_halo.h"
#include "iree/hal/drivers/amd/xdna/image/testdata/mul_i32.h"
#include "iree/hal/drivers/amd/xdna/image/testing/aie2p_image_fixture.h"
#include "iree/testing/gtest.h"
#include "iree/testing/status_matchers.h"

namespace {

using iree::Status;
using iree::StatusCode;
using iree::hal::amd::xdna::testing::ByteSequencePtr;
using iree::hal::amd::xdna::testing::FakeCommand;
using iree::hal::amd::xdna::testing::FakeProvider;
using iree::hal::amd::xdna::testing::MakeOwnedByteSequence;
using testing::HasSubstr;

static constexpr iree_host_size_t kBindingCount = 3;
static constexpr iree_host_size_t kBufferStorageByteLength = 4096;
static constexpr std::array<iree_device_size_t, kBindingCount>
    kBindingByteLengths = {64, 64, 64};

static ByteSequencePtr LoadMulI32Image() {
  EXPECT_EQ(iree_hal_amd_xdna_test_mul_i32_size(), 1u);
  if (iree_hal_amd_xdna_test_mul_i32_size() != 1u) return {};
  const iree_file_toc_t* file = iree_hal_amd_xdna_test_mul_i32_create();
  const auto* begin = reinterpret_cast<const uint8_t*>(file->data);
  return MakeOwnedByteSequence(std::vector<uint8_t>(begin, begin + file->size));
}

static void ReleaseBufferStorage(void* user_data, iree_hal_buffer_t* buffer) {
  (void)buffer;
  ++*static_cast<uint32_t*>(user_data);
}

class XdnaPreparedCommandTest : public ::testing::Test {
 protected:
  void SetUp() override {
    static_assert(kBufferStorageByteLength % IREE_HAL_HEAP_BUFFER_ALIGNMENT ==
                  0);
    iree_hal_queue_family_initialize(/*ordinal=*/7, &queue_family_);

    ByteSequencePtr sequence = LoadMulI32Image();
    iree_hal_amd_xdna_aie2p_target_t target;
    IREE_CHECK_OK(iree_hal_amd_xdna_aie2p_strix_halo_target_initialize(
        /*context_column_count=*/1, &target));
    IREE_CHECK_OK(iree_hal_amd_xdna_executable_create(
        provider_.xdna_api(), provider_.device(), &queue_family_,
        sequence.get(), &target, /*required_program_flags=*/0,
        iree_allocator_system(), &executable_));

    for (iree_host_size_t i = 0; i < kBindingCount; ++i) {
      WrapBuffer(i,
                 IREE_HAL_MEMORY_TYPE_HOST_LOCAL |
                     IREE_HAL_MEMORY_TYPE_HOST_COHERENT |
                     IREE_HAL_MEMORY_TYPE_DEVICE_VISIBLE,
                 IREE_HAL_MEMORY_ACCESS_READ | IREE_HAL_MEMORY_ACCESS_WRITE,
                 IREE_HAL_BUFFER_USAGE_STORAGE);
    }
  }

  void TearDown() override {
    provider_.command_destroy_status = AMDF_STATUS_OK;
    if (prepared_command_ != nullptr) {
      IREE_CHECK_OK(
          iree_hal_amd_xdna_prepared_command_destroy(prepared_command_));
      prepared_command_ = nullptr;
    }
    for (iree_hal_buffer_t*& buffer : buffers_) {
      iree_hal_buffer_release(buffer);
      buffer = nullptr;
    }
    iree_hal_executable_release(executable_);
    executable_ = nullptr;
    EXPECT_EQ(provider_.live_command_count, 0u);
    EXPECT_EQ(provider_.live_program_count, 0u);
  }

  void WrapBuffer(iree_host_size_t ordinal, iree_hal_memory_type_t memory_type,
                  iree_hal_memory_access_t allowed_access,
                  iree_hal_buffer_usage_t allowed_usage) {
    iree_hal_buffer_release(buffers_[ordinal]);
    buffers_[ordinal] = nullptr;
    IREE_CHECK_OK(iree_hal_heap_buffer_wrap(
        iree_hal_buffer_placement_undefined(), memory_type, allowed_access,
        allowed_usage, kBufferStorageByteLength,
        iree_make_byte_span(buffer_storage_[ordinal].data(),
                            buffer_storage_[ordinal].size()),
        (iree_hal_buffer_release_callback_t){
            .fn = ReleaseBufferStorage,
            .user_data = &release_counts_[ordinal],
        },
        iree_allocator_system(), &buffers_[ordinal]));
    bindings_[ordinal] = (iree_hal_amd_xdna_prepared_command_binding_t){
        .buffer_ref = iree_hal_make_buffer_ref(buffers_[ordinal], /*offset=*/0,
                                               kBindingByteLengths[ordinal]),
        .memory = reinterpret_cast<amdf_memory_t*>(uintptr_t{0x1000} +
                                                   ordinal * uintptr_t{0x100}),
        .memory_byte_offset = 64 + ordinal * 512,
        .device_address = 0x100000 + ordinal * 0x1000,
    };
  }

  iree_status_t CreatePrepared(
      iree_host_size_t binding_count,
      const iree_hal_amd_xdna_prepared_command_binding_t* bindings) {
    return iree_hal_amd_xdna_prepared_command_create(
        executable_, iree_hal_executable_function_from_index(0), binding_count,
        bindings, iree_allocator_system(), &prepared_command_);
  }

  iree_status_t CreatePrepared() {
    return CreatePrepared(bindings_.size(), bindings_.data());
  }

  iree_status_t DestroyPrepared() {
    iree_status_t status =
        iree_hal_amd_xdna_prepared_command_destroy(prepared_command_);
    if (iree_status_is_ok(status)) prepared_command_ = nullptr;
    return status;
  }

  FakeProvider provider_;
  iree_hal_queue_family_t queue_family_ = {};
  iree_hal_executable_t* executable_ = nullptr;
  alignas(IREE_HAL_HEAP_BUFFER_ALIGNMENT)
      std::array<std::array<uint8_t, kBufferStorageByteLength>,
                 kBindingCount> buffer_storage_ = {};
  std::array<uint32_t, kBindingCount> release_counts_ = {};
  std::array<iree_hal_buffer_t*, kBindingCount> buffers_ = {};
  std::array<iree_hal_amd_xdna_prepared_command_binding_t, kBindingCount>
      bindings_ = {};
  iree_hal_amd_xdna_prepared_command_t* prepared_command_ = nullptr;
};

TEST_F(XdnaPreparedCommandTest, PreparesCanonicalFixedBindings) {
  IREE_ASSERT_OK(CreatePrepared());

  ASSERT_EQ(provider_.command_create_count, 1u);
  ASSERT_EQ(provider_.live_command_count, 1u);
  ASSERT_NE(provider_.last_command, nullptr);
  const FakeCommand& command = *provider_.last_command;
  EXPECT_EQ(command.create_info_type,
            AMDF_STRUCTURE_TYPE_XDNA_COMMAND_CREATE_INFO);
  EXPECT_EQ(command.create_info_structure_size,
            sizeof(amdf_xdna_command_create_info_t));
  EXPECT_EQ(command.create_info_next, nullptr);
  EXPECT_EQ(command.reserved, 0u);
  EXPECT_EQ(command.program, provider_.last_program);

  iree_hal_amd_xdna_executable_entry_t entry;
  IREE_ASSERT_OK(iree_hal_amd_xdna_executable_query_entry(
      executable_, iree_hal_executable_function_from_index(0), &entry));
  ASSERT_EQ(command.control_bytes.size(), entry.control.data_length);
  EXPECT_EQ(
      command.control_bytes,
      std::vector<uint8_t>(entry.control.data,
                           entry.control.data + entry.control.data_length));
  ASSERT_EQ(command.bindings.size(), bindings_.size());
  for (iree_host_size_t i = 0; i < bindings_.size(); ++i) {
    EXPECT_EQ(command.bindings[i].memory, bindings_[i].memory);
    EXPECT_EQ(command.bindings[i].byte_offset, bindings_[i].memory_byte_offset);
    EXPECT_EQ(command.bindings[i].byte_length, kBindingByteLengths[i]);
  }
  EXPECT_EQ(
      iree_hal_amd_xdna_prepared_command_get_native_command(prepared_command_),
      reinterpret_cast<amdf_xdna_command_t*>(provider_.last_command));
}

TEST_F(XdnaPreparedCommandTest, RetainsEveryBorrowedResource) {
  IREE_ASSERT_OK(CreatePrepared());

  iree_hal_executable_release(executable_);
  executable_ = nullptr;
  for (iree_host_size_t i = 0; i < buffers_.size(); ++i) {
    iree_hal_buffer_release(buffers_[i]);
    buffers_[i] = nullptr;
    EXPECT_EQ(release_counts_[i], 0u);
  }
  EXPECT_EQ(provider_.live_program_count, 1u);

  IREE_ASSERT_OK(DestroyPrepared());
  EXPECT_EQ(provider_.live_command_count, 0u);
  EXPECT_EQ(provider_.live_program_count, 0u);
  for (uint32_t release_count : release_counts_) {
    EXPECT_EQ(release_count, 1u);
  }
}

TEST_F(XdnaPreparedCommandTest, RejectsMalformedBindingRanges) {
  auto invalid_bindings = bindings_;
  invalid_bindings[0].buffer_ref.length = kBindingByteLengths[0] - 1;
  IREE_EXPECT_STATUS_IS(
      StatusCode::kOutOfRange,
      CreatePrepared(invalid_bindings.size(), invalid_bindings.data()));

  invalid_bindings = bindings_;
  invalid_bindings[0].buffer_ref.offset = 4;
  invalid_bindings[0].buffer_ref.length = kBindingByteLengths[0];
  IREE_EXPECT_STATUS_IS(
      StatusCode::kOutOfRange,
      CreatePrepared(invalid_bindings.size(), invalid_bindings.data()));

  invalid_bindings = bindings_;
  ++invalid_bindings[0].device_address;
  IREE_EXPECT_STATUS_IS(
      StatusCode::kOutOfRange,
      CreatePrepared(invalid_bindings.size(), invalid_bindings.data()));

  invalid_bindings = bindings_;
  invalid_bindings[0].memory = nullptr;
  IREE_EXPECT_STATUS_IS(
      StatusCode::kInvalidArgument,
      CreatePrepared(invalid_bindings.size(), invalid_bindings.data()));
  EXPECT_EQ(provider_.command_create_count, 0u);
}

TEST_F(XdnaPreparedCommandTest, RejectsBindingCountMismatch) {
  IREE_EXPECT_STATUS_IS(StatusCode::kInvalidArgument,
                        CreatePrepared(bindings_.size() - 1, bindings_.data()));
  EXPECT_EQ(provider_.command_create_count, 0u);
}

TEST_F(XdnaPreparedCommandTest, EnforcesBindingAccess) {
  WrapBuffer(/*ordinal=*/2,
             IREE_HAL_MEMORY_TYPE_HOST_LOCAL |
                 IREE_HAL_MEMORY_TYPE_HOST_COHERENT |
                 IREE_HAL_MEMORY_TYPE_DEVICE_VISIBLE,
             IREE_HAL_MEMORY_ACCESS_READ, IREE_HAL_BUFFER_USAGE_STORAGE);
  IREE_EXPECT_STATUS_IS(StatusCode::kPermissionDenied, CreatePrepared());
  EXPECT_EQ(provider_.command_create_count, 0u);
}

TEST_F(XdnaPreparedCommandTest, EnforcesBindingUsage) {
  WrapBuffer(/*ordinal=*/2,
             IREE_HAL_MEMORY_TYPE_HOST_LOCAL |
                 IREE_HAL_MEMORY_TYPE_HOST_COHERENT |
                 IREE_HAL_MEMORY_TYPE_DEVICE_VISIBLE,
             IREE_HAL_MEMORY_ACCESS_READ | IREE_HAL_MEMORY_ACCESS_WRITE,
             IREE_HAL_BUFFER_USAGE_STORAGE_READ);
  IREE_EXPECT_STATUS_IS(StatusCode::kPermissionDenied, CreatePrepared());
  EXPECT_EQ(provider_.command_create_count, 0u);
}

TEST_F(XdnaPreparedCommandTest, EnforcesDeviceVisibility) {
  WrapBuffer(
      /*ordinal=*/0,
      IREE_HAL_MEMORY_TYPE_HOST_LOCAL | IREE_HAL_MEMORY_TYPE_HOST_COHERENT,
      IREE_HAL_MEMORY_ACCESS_READ | IREE_HAL_MEMORY_ACCESS_WRITE,
      IREE_HAL_BUFFER_USAGE_STORAGE);
  IREE_EXPECT_STATUS_IS(StatusCode::kPermissionDenied, CreatePrepared());
  EXPECT_EQ(provider_.command_create_count, 0u);
}

TEST_F(XdnaPreparedCommandTest, PropagatesProviderCreationFailure) {
  provider_.command_create_status =
      amdf_make_api_status(AMDF_STATUS_CODE_RESOURCE_EXHAUSTED);
  Status status(CreatePrepared());
  EXPECT_EQ(status.code(), StatusCode::kResourceExhausted);
  EXPECT_THAT(status.ToString(), HasSubstr("xdna.command_create"));
  EXPECT_EQ(prepared_command_, nullptr);
  EXPECT_EQ(provider_.command_create_count, 1u);
  EXPECT_EQ(provider_.live_command_count, 0u);
}

TEST_F(XdnaPreparedCommandTest, RejectsProviderSuccessWithoutCommand) {
  provider_.return_null_command = true;
  Status status(CreatePrepared());
  EXPECT_EQ(status.code(), StatusCode::kFailedPrecondition);
  EXPECT_THAT(status.ToString(), HasSubstr("without a command"));
  EXPECT_EQ(prepared_command_, nullptr);
  EXPECT_EQ(provider_.command_create_count, 1u);
  EXPECT_EQ(provider_.live_command_count, 0u);
}

TEST_F(XdnaPreparedCommandTest, PreservesResourcesForTeardownRetry) {
  IREE_ASSERT_OK(CreatePrepared());
  amdf_xdna_command_t* native_command =
      iree_hal_amd_xdna_prepared_command_get_native_command(prepared_command_);

  provider_.command_destroy_status =
      amdf_make_api_status(AMDF_STATUS_CODE_BUSY);
  Status status(DestroyPrepared());
  EXPECT_EQ(status.code(), StatusCode::kAborted);
  EXPECT_THAT(status.ToString(), HasSubstr("xdna.command_destroy"));
  EXPECT_NE(prepared_command_, nullptr);
  EXPECT_EQ(
      iree_hal_amd_xdna_prepared_command_get_native_command(prepared_command_),
      native_command);
  EXPECT_EQ(provider_.live_command_count, 1u);
  EXPECT_EQ(provider_.live_program_count, 1u);
  for (uint32_t release_count : release_counts_) {
    EXPECT_EQ(release_count, 0u);
  }

  provider_.command_destroy_status = AMDF_STATUS_OK;
  IREE_ASSERT_OK(DestroyPrepared());
  EXPECT_EQ(provider_.command_destroy_count, 2u);
  EXPECT_EQ(provider_.live_command_count, 0u);
}

}  // namespace
