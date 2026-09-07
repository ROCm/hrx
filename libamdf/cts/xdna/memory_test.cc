// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "amdf/amdf.h"
#include "amdf/xdna.h"
#include "gtest/gtest.h"
#include "xdna_device_fixture.h"

namespace {

static_assert(offsetof(amdf_memory_create_info_t, memory_class) ==
              sizeof(amdf_input_structure_t));
static_assert(offsetof(amdf_memory_create_info_t, required_flags) == 24);
static_assert(offsetof(amdf_memory_create_info_t, byte_length) == 32);
static_assert(sizeof(amdf_memory_create_info_t) == 56);
static_assert(offsetof(amdf_memory_info_t, memory_class) ==
              sizeof(amdf_output_structure_t));
static_assert(offsetof(amdf_memory_info_t, physical_backing_id) == 48);
static_assert(offsetof(amdf_memory_info_t, device_address) == 64);
static_assert(sizeof(amdf_memory_info_t) == 80);
static_assert(offsetof(amdf_memory_map_info_t, byte_offset) ==
              sizeof(amdf_input_structure_t));
static_assert(sizeof(amdf_memory_map_info_t) == 40);
static_assert(offsetof(amdf_host_mapping_info_t, flags) ==
              sizeof(amdf_output_structure_t));
static_assert(offsetof(amdf_host_mapping_info_t, pointer) == 24);
static_assert(offsetof(amdf_host_mapping_info_t, reset_epoch) == 48);
static_assert(sizeof(amdf_host_mapping_info_t) == 56);

class XdnaMemoryTest : public XdnaDeviceFixture {
 protected:
  void TearDown() override {
    if (mapping_ != nullptr) {
      EXPECT_TRUE(amdf_status_is_ok(api_->host_mapping_destroy(mapping_)));
    }
    if (memory_ != nullptr) {
      EXPECT_TRUE(amdf_status_is_ok(api_->memory_destroy(memory_)));
    }
    XdnaDeviceFixture::TearDown();
  }

  amdf_memory_create_info_t MakeMemoryCreateInfo() {
    amdf_memory_create_info_t create_info = {};
    create_info.type = AMDF_STRUCTURE_TYPE_MEMORY_CREATE_INFO;
    create_info.structure_size = sizeof(create_info);
    create_info.memory_class = AMDF_MEMORY_CLASS_SYSTEM;
    create_info.required_flags =
        AMDF_MEMORY_FLAG_HOST_VISIBLE | AMDF_MEMORY_FLAG_DEVICE_ADDRESS;
    create_info.byte_length = 4097;
    create_info.minimum_alignment = 4096;
    return create_info;
  }

  amdf_memory_t* memory_ = nullptr;
  amdf_host_mapping_t* mapping_ = nullptr;
};

TEST_F(XdnaMemoryTest, ValidatesCreationArgumentsWithoutNativeAllocation) {
  amdf_memory_create_info_t create_info = MakeMemoryCreateInfo();
  amdf_memory_t* output = reinterpret_cast<amdf_memory_t*>(uintptr_t{1});
  EXPECT_EQ(
      amdf_status_code(api_->memory_create(nullptr, &create_info, &output)),
      AMDF_STATUS_CODE_INVALID_ARGUMENT);
  EXPECT_EQ(output, nullptr);
  EXPECT_EQ(amdf_status_code(api_->memory_create(device_, nullptr, &output)),
            AMDF_STATUS_CODE_INVALID_ARGUMENT);
  EXPECT_EQ(output, nullptr);
  EXPECT_EQ(
      amdf_status_code(api_->memory_create(device_, &create_info, nullptr)),
      AMDF_STATUS_CODE_INVALID_ARGUMENT);

  create_info.type = AMDF_STRUCTURE_TYPE_NONE;
  EXPECT_EQ(
      amdf_status_code(api_->memory_create(device_, &create_info, &output)),
      AMDF_STATUS_CODE_INVALID_ARGUMENT);
  EXPECT_EQ(output, nullptr);

  create_info = MakeMemoryCreateInfo();
  create_info.byte_length = 0;
  EXPECT_EQ(
      amdf_status_code(api_->memory_create(device_, &create_info, &output)),
      AMDF_STATUS_CODE_INVALID_ARGUMENT);
  EXPECT_EQ(output, nullptr);

  create_info = MakeMemoryCreateInfo();
  create_info.minimum_alignment = 3;
  EXPECT_EQ(
      amdf_status_code(api_->memory_create(device_, &create_info, &output)),
      AMDF_STATUS_CODE_INVALID_ARGUMENT);
  EXPECT_EQ(output, nullptr);

  create_info = MakeMemoryCreateInfo();
  create_info.required_flags = UINT64_C(1) << 63;
  EXPECT_EQ(
      amdf_status_code(api_->memory_create(device_, &create_info, &output)),
      AMDF_STATUS_CODE_INVALID_ARGUMENT);
  EXPECT_EQ(output, nullptr);

  create_info = MakeMemoryCreateInfo();
  create_info.registered_host_pointer = &create_info;
  EXPECT_EQ(
      amdf_status_code(api_->memory_create(device_, &create_info, &output)),
      AMDF_STATUS_CODE_INVALID_ARGUMENT);
  EXPECT_EQ(output, nullptr);
}

TEST_F(XdnaMemoryTest, OwnsStableAddressAndExplicitHostMapping) {
  const amdf_memory_create_info_t create_info = MakeMemoryCreateInfo();
  const amdf_status_t create_status =
      api_->memory_create(device_, &create_info, &memory_);
  if (amdf_status_domain(create_status) == AMDF_STATUS_DOMAIN_API &&
      amdf_status_code(create_status) == AMDF_STATUS_CODE_UNSUPPORTED) {
    GTEST_SKIP() << "XDNA host-visible device memory is unavailable";
  }
  ASSERT_TRUE(amdf_status_is_ok(create_status))
      << "domain=" << amdf_status_domain(create_status)
      << " code=" << amdf_status_code(create_status);
  ASSERT_NE(memory_, nullptr);

  amdf_memory_info_t memory_info = {};
  memory_info.type = AMDF_STRUCTURE_TYPE_MEMORY_INFO;
  memory_info.structure_size = sizeof(memory_info);
  ASSERT_TRUE(
      amdf_status_is_ok(api_->memory_query_info(memory_, &memory_info)));
  EXPECT_EQ(memory_info.memory_class, AMDF_MEMORY_CLASS_SYSTEM);
  EXPECT_EQ(memory_info.flags & create_info.required_flags,
            create_info.required_flags);
  EXPECT_GE(memory_info.byte_length, create_info.byte_length);
  ASSERT_GE(memory_info.alignment, create_info.minimum_alignment);
  EXPECT_EQ(memory_info.alignment & (memory_info.alignment - 1), 0u);
  EXPECT_NE(memory_info.device_address, 0u);
  EXPECT_EQ(memory_info.device_address & (memory_info.alignment - 1), 0u);

  amdf_xdna_device_info_t device_info = {};
  device_info.type = AMDF_STRUCTURE_TYPE_XDNA_DEVICE_INFO;
  device_info.structure_size = sizeof(device_info);
  ASSERT_TRUE(
      amdf_status_is_ok(xdna_api_->device_query_info(device_, &device_info)));
  EXPECT_EQ(memory_info.reset_epoch, device_info.reset_epoch);

  amdf_memory_info_t second_memory_info = {};
  second_memory_info.type = AMDF_STRUCTURE_TYPE_MEMORY_INFO;
  second_memory_info.structure_size = sizeof(second_memory_info);
  ASSERT_TRUE(
      amdf_status_is_ok(api_->memory_query_info(memory_, &second_memory_info)));
  EXPECT_EQ(std::memcmp(&memory_info, &second_memory_info, sizeof(memory_info)),
            0);

  amdf_memory_info_t invalid_memory_info = memory_info;
  invalid_memory_info.type = AMDF_STRUCTURE_TYPE_NONE;
  const amdf_memory_info_t original_invalid_memory_info = invalid_memory_info;
  EXPECT_EQ(
      amdf_status_code(api_->memory_query_info(memory_, &invalid_memory_info)),
      AMDF_STATUS_CODE_INVALID_ARGUMENT);
  EXPECT_EQ(std::memcmp(&invalid_memory_info, &original_invalid_memory_info,
                        sizeof(invalid_memory_info)),
            0);

  amdf_memory_map_info_t map_info = {};
  map_info.type = AMDF_STRUCTURE_TYPE_MEMORY_MAP_INFO;
  map_info.structure_size = sizeof(map_info);
  map_info.byte_offset = 32;
  map_info.byte_length = 4096;
  map_info.flags = AMDF_MEMORY_MAP_FLAG_READ | AMDF_MEMORY_MAP_FLAG_WRITE;
  ASSERT_TRUE(
      amdf_status_is_ok(api_->memory_map(memory_, &map_info, &mapping_)));
  ASSERT_NE(mapping_, nullptr);

  amdf_host_mapping_info_t mapping_info = {};
  mapping_info.type = AMDF_STRUCTURE_TYPE_HOST_MAPPING_INFO;
  mapping_info.structure_size = sizeof(mapping_info);
  ASSERT_TRUE(amdf_status_is_ok(
      api_->host_mapping_query_info(mapping_, &mapping_info)));
  EXPECT_EQ(mapping_info.flags & map_info.flags, map_info.flags);
  EXPECT_NE(mapping_info.cacheability, AMDF_HOST_CACHEABILITY_UNKNOWN);
  ASSERT_NE(mapping_info.pointer, nullptr);
  EXPECT_EQ(mapping_info.byte_length, map_info.byte_length);
  EXPECT_EQ(mapping_info.reset_epoch, memory_info.reset_epoch);

  amdf_host_mapping_info_t invalid_mapping_info = mapping_info;
  invalid_mapping_info.type = AMDF_STRUCTURE_TYPE_NONE;
  const amdf_host_mapping_info_t original_invalid_mapping_info =
      invalid_mapping_info;
  EXPECT_EQ(amdf_status_code(
                api_->host_mapping_query_info(mapping_, &invalid_mapping_info)),
            AMDF_STATUS_CODE_INVALID_ARGUMENT);
  EXPECT_EQ(std::memcmp(&invalid_mapping_info, &original_invalid_mapping_info,
                        sizeof(invalid_mapping_info)),
            0);

  std::memset(mapping_info.pointer, 0xA5,
              static_cast<size_t>(mapping_info.byte_length));
  EXPECT_TRUE(amdf_status_is_ok(api_->host_mapping_cache_control(
      mapping_, AMDF_HOST_CACHE_OPERATION_FLUSH, 0, mapping_info.byte_length)));
  EXPECT_TRUE(amdf_status_is_ok(api_->host_mapping_cache_control(
      mapping_, AMDF_HOST_CACHE_OPERATION_INVALIDATE, 0,
      mapping_info.byte_length)));
  EXPECT_TRUE(amdf_status_is_ok(api_->host_mapping_cache_control(
      mapping_, AMDF_HOST_CACHE_OPERATION_FLUSH, mapping_info.byte_length, 0)));
  EXPECT_EQ(amdf_status_code(api_->host_mapping_cache_control(
                mapping_, AMDF_HOST_CACHE_OPERATION_FLUSH,
                mapping_info.byte_length, 1)),
            AMDF_STATUS_CODE_INVALID_ARGUMENT);
  EXPECT_EQ(amdf_status_code(api_->host_mapping_cache_control(
                mapping_, 0, 0, mapping_info.byte_length)),
            AMDF_STATUS_CODE_INVALID_ARGUMENT);

  amdf_memory_map_info_t read_only_map_info = map_info;
  read_only_map_info.flags = AMDF_MEMORY_MAP_FLAG_READ;
  amdf_host_mapping_t* read_only_mapping = nullptr;
  ASSERT_TRUE(amdf_status_is_ok(
      api_->memory_map(memory_, &read_only_map_info, &read_only_mapping)));
  ASSERT_NE(read_only_mapping, nullptr);
  amdf_host_mapping_info_t read_only_mapping_info = {};
  read_only_mapping_info.type = AMDF_STRUCTURE_TYPE_HOST_MAPPING_INFO;
  read_only_mapping_info.structure_size = sizeof(read_only_mapping_info);
  ASSERT_TRUE(amdf_status_is_ok(api_->host_mapping_query_info(
      read_only_mapping, &read_only_mapping_info)));
  EXPECT_TRUE(amdf_status_is_ok(api_->host_mapping_cache_control(
      read_only_mapping, AMDF_HOST_CACHE_OPERATION_FLUSH, 0, 0)));
  const amdf_status_t read_only_flush_status = api_->host_mapping_cache_control(
      read_only_mapping, AMDF_HOST_CACHE_OPERATION_FLUSH, 0, 1);
  if ((read_only_mapping_info.flags & AMDF_MEMORY_MAP_FLAG_WRITE) != 0) {
    EXPECT_TRUE(amdf_status_is_ok(read_only_flush_status));
  } else {
    EXPECT_EQ(amdf_status_code(read_only_flush_status),
              AMDF_STATUS_CODE_FAILED_PRECONDITION);
  }
  ASSERT_TRUE(amdf_status_is_ok(api_->host_mapping_destroy(read_only_mapping)));

  EXPECT_EQ(amdf_status_code(api_->memory_destroy(memory_)),
            AMDF_STATUS_CODE_BUSY);
  EXPECT_EQ(amdf_status_code(api_->device_destroy(device_)),
            AMDF_STATUS_CODE_BUSY);

  ASSERT_TRUE(amdf_status_is_ok(api_->host_mapping_destroy(mapping_)));
  mapping_ = nullptr;
  ASSERT_TRUE(amdf_status_is_ok(api_->memory_destroy(memory_)));
  memory_ = nullptr;
}

TEST_F(XdnaMemoryTest, RejectsInvalidMappingRequestsBeforeNativeMapping) {
  const amdf_memory_create_info_t create_info = MakeMemoryCreateInfo();
  const amdf_status_t create_status =
      api_->memory_create(device_, &create_info, &memory_);
  if (amdf_status_domain(create_status) == AMDF_STATUS_DOMAIN_API &&
      amdf_status_code(create_status) == AMDF_STATUS_CODE_UNSUPPORTED) {
    GTEST_SKIP() << "XDNA host-visible device memory is unavailable";
  }
  ASSERT_TRUE(amdf_status_is_ok(create_status));

  amdf_memory_map_info_t map_info = {};
  map_info.type = AMDF_STRUCTURE_TYPE_MEMORY_MAP_INFO;
  map_info.structure_size = sizeof(map_info);
  map_info.byte_length = 1;
  map_info.flags = AMDF_MEMORY_MAP_FLAG_READ;
  amdf_host_mapping_t* output =
      reinterpret_cast<amdf_host_mapping_t*>(uintptr_t{1});
  EXPECT_EQ(amdf_status_code(api_->memory_map(nullptr, &map_info, &output)),
            AMDF_STATUS_CODE_INVALID_ARGUMENT);
  EXPECT_EQ(output, nullptr);
  EXPECT_EQ(amdf_status_code(api_->memory_map(memory_, nullptr, &output)),
            AMDF_STATUS_CODE_INVALID_ARGUMENT);
  EXPECT_EQ(output, nullptr);

  map_info.flags = 0;
  EXPECT_EQ(amdf_status_code(api_->memory_map(memory_, &map_info, &output)),
            AMDF_STATUS_CODE_INVALID_ARGUMENT);
  EXPECT_EQ(output, nullptr);

  map_info.flags = UINT32_C(1) << 31;
  EXPECT_EQ(amdf_status_code(api_->memory_map(memory_, &map_info, &output)),
            AMDF_STATUS_CODE_INVALID_ARGUMENT);
  EXPECT_EQ(output, nullptr);

  map_info.flags = AMDF_MEMORY_MAP_FLAG_READ;
  map_info.byte_offset = UINT64_C(65536);
  map_info.byte_length = 1;
  EXPECT_EQ(amdf_status_code(api_->memory_map(memory_, &map_info, &output)),
            AMDF_STATUS_CODE_INVALID_ARGUMENT);
  EXPECT_EQ(output, nullptr);
}

}  // namespace
