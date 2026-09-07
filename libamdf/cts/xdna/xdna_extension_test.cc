// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "amdf/amdf.h"
#include "amdf/xdna.h"
#include "gtest/gtest.h"
#include "util/provider.h"

namespace {

static_assert(offsetof(amdf_xdna_device_create_info_t, logical_column_count) ==
              sizeof(amdf_input_structure_t));
static_assert(offsetof(amdf_xdna_device_create_info_t,
                       acceptable_scheduling_modes) == 24);
static_assert(sizeof(amdf_xdna_device_create_info_t) == 32);
static_assert(offsetof(amdf_xdna_device_info_t, id) ==
              sizeof(amdf_output_structure_t));
static_assert(offsetof(amdf_xdna_device_info_t, reset_epoch) == 32);
static_assert(offsetof(amdf_xdna_device_info_t, columns) == 48);
static_assert(offsetof(amdf_xdna_device_info_t, row_count) == 60);
static_assert(sizeof(amdf_xdna_device_info_t) == 64);
static_assert(offsetof(amdf_xdna_endpoint_info_t, program) == 72);
static_assert(offsetof(amdf_xdna_endpoint_info_t, command) == 136);
static_assert(offsetof(amdf_xdna_endpoint_info_t, target_id) == 168);
static_assert(sizeof(amdf_xdna_endpoint_info_t) == 232);
static_assert(sizeof(amdf_xdna_program_component_t) == 24);
static_assert(offsetof(amdf_xdna_program_create_info_t, required_flags) == 24);
static_assert(offsetof(amdf_xdna_program_create_info_t, footprint) == 40);
static_assert(sizeof(amdf_xdna_program_create_info_t) == 56);
static_assert(offsetof(amdf_xdna_program_info_t, total_byte_length) == 32);
static_assert(offsetof(amdf_xdna_program_info_t, footprint) == 48);
static_assert(sizeof(amdf_xdna_program_info_t) == 64);
static_assert(sizeof(amdf_xdna_command_binding_t) == 24);
static_assert(offsetof(amdf_xdna_command_create_info_t, control_bytes) == 24);
static_assert(offsetof(amdf_xdna_command_create_info_t, bindings) == 40);
static_assert(sizeof(amdf_xdna_command_create_info_t) == 48);
static_assert(offsetof(amdf_xdna_command_info_t, control_byte_length) == 24);
static_assert(sizeof(amdf_xdna_command_info_t) == 40);
static_assert(sizeof(amdf_xdna_kernel_queue_create_info_t) == 24);
static_assert(sizeof(amdf_xdna_kernel_queue_submission_info_t) == 32);
static_assert(offsetof(amdf_xdna_api_t, kernel_queue_submit) +
                  sizeof(amdf_xdna_api_t::kernel_queue_submit) ==
              sizeof(amdf_xdna_api_t));

const amdf_api_t* QueryApi() {
  const amdf_api_t* api = nullptr;
  EXPECT_TRUE(amdf_status_is_ok(amdf_cts_provider_query_api()(
      AMDF_ABI_VERSION_1, AMDF_ABI_VERSION_LATEST, &api)));
  return api;
}

const amdf_xdna_api_t* QueryXdnaApi(const amdf_api_t* api) {
  const void* extension_api = nullptr;
  EXPECT_TRUE(amdf_status_is_ok(api->query_extension(
      AMDF_EXTENSION_XDNA, AMDF_XDNA_EXTENSION_VERSION_1,
      AMDF_XDNA_EXTENSION_VERSION_LATEST, &extension_api)));
  return static_cast<const amdf_xdna_api_t*>(extension_api);
}

TEST(XdnaExtensionTest, ReportsCompiledAvailabilityBeforeCreatingInstance) {
  const amdf_api_t* api = QueryApi();
  ASSERT_NE(api, nullptr);
  const void* extension_api = reinterpret_cast<const void*>(uintptr_t{1});

  const amdf_status_t status =
      api->query_extension(AMDF_EXTENSION_XDNA, AMDF_XDNA_EXTENSION_VERSION_1,
                           AMDF_XDNA_EXTENSION_VERSION_LATEST, &extension_api);

  ASSERT_TRUE(amdf_status_is_ok(status));
  const auto* xdna_api = static_cast<const amdf_xdna_api_t*>(extension_api);
  ASSERT_NE(xdna_api, nullptr);
  EXPECT_EQ(xdna_api->structure_size, sizeof(amdf_xdna_api_t));
  EXPECT_EQ(xdna_api->extension_version, AMDF_XDNA_EXTENSION_VERSION_1);
  EXPECT_NE(xdna_api->endpoint_query_info, nullptr);
  EXPECT_NE(xdna_api->device_create, nullptr);
  EXPECT_NE(xdna_api->device_query_info, nullptr);
  EXPECT_NE(xdna_api->program_create, nullptr);
  EXPECT_NE(xdna_api->program_query_info, nullptr);
  EXPECT_NE(xdna_api->program_destroy, nullptr);
  EXPECT_NE(xdna_api->command_create, nullptr);
  EXPECT_NE(xdna_api->command_query_info, nullptr);
  EXPECT_NE(xdna_api->command_destroy, nullptr);
  EXPECT_NE(xdna_api->kernel_queue_create, nullptr);
  EXPECT_NE(xdna_api->kernel_queue_submit, nullptr);
}

TEST(XdnaExtensionTest, ReturnsStableImmutableTable) {
  const amdf_api_t* api = QueryApi();
  ASSERT_NE(api, nullptr);

  const amdf_xdna_api_t* first_api = QueryXdnaApi(api);
  const amdf_xdna_api_t* second_api = QueryXdnaApi(api);

  EXPECT_EQ(first_api, second_api);
}

TEST(XdnaExtensionTest, RejectsUnsupportedVersionAndClearsOutput) {
  const amdf_api_t* api = QueryApi();
  ASSERT_NE(api, nullptr);
  const void* extension_api = reinterpret_cast<const void*>(uintptr_t{1});

  const amdf_status_t status = api->query_extension(
      AMDF_EXTENSION_XDNA, AMDF_XDNA_EXTENSION_VERSION_LATEST + 1,
      AMDF_XDNA_EXTENSION_VERSION_LATEST + 1, &extension_api);

  EXPECT_EQ(amdf_status_domain(status), AMDF_STATUS_DOMAIN_API);
  EXPECT_EQ(amdf_status_code(status), AMDF_STATUS_CODE_VERSION_MISMATCH);
  EXPECT_EQ(extension_api, nullptr);
}

class XdnaEndpointTest : public ::testing::Test {
 protected:
  void SetUp() override {
    api_ = QueryApi();
    ASSERT_NE(api_, nullptr);
    xdna_api_ = QueryXdnaApi(api_);
    ASSERT_NE(xdna_api_, nullptr);

    amdf_instance_create_info_t create_info = {};
    create_info.type = AMDF_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.structure_size = sizeof(create_info);
    const amdf_status_t status =
        api_->instance_create(&create_info, &instance_);
    if (amdf_status_domain(status) == AMDF_STATUS_DOMAIN_API &&
        amdf_status_code(status) == AMDF_STATUS_CODE_UNSUPPORTED) {
      GTEST_SKIP() << "platform provider is not implemented";
    }
    ASSERT_TRUE(amdf_status_is_ok(status));
  }

  void TearDown() override {
    if (device_ != nullptr) {
      EXPECT_TRUE(amdf_status_is_ok(api_->device_destroy(device_)));
    }
    if (endpoint_ != nullptr) {
      EXPECT_TRUE(amdf_status_is_ok(api_->endpoint_close(endpoint_)));
    }
    if (instance_ != nullptr) {
      EXPECT_TRUE(amdf_status_is_ok(api_->instance_destroy(instance_)));
    }
  }

  bool OpenEngine(amdf_engine_kind_t engine_kind) {
    uint32_t endpoint_count = 0;
    if (!amdf_status_is_ok(
            api_->endpoint_enumerate(instance_, 0, nullptr, &endpoint_count))) {
      return false;
    }
    std::vector<amdf_endpoint_summary_t> summaries(endpoint_count);
    if (endpoint_count != 0 &&
        !amdf_status_is_ok(api_->endpoint_enumerate(
            instance_, endpoint_count, summaries.data(), &endpoint_count))) {
      return false;
    }
    for (const amdf_endpoint_summary_t& summary : summaries) {
      if (summary.engine_kind == engine_kind) {
        return amdf_status_is_ok(
            api_->endpoint_open(instance_, &summary.id, &endpoint_));
      }
    }
    return false;
  }

  amdf_xdna_device_create_info_t MakeDeviceCreateInfo(
      uint32_t logical_column_count = 1) {
    amdf_xdna_device_create_info_t create_info = {};
    create_info.type = AMDF_STRUCTURE_TYPE_XDNA_DEVICE_CREATE_INFO;
    create_info.structure_size = sizeof(create_info);
    create_info.logical_column_count = logical_column_count;
    create_info.physical_column_origin = AMDF_XDNA_PHYSICAL_COLUMN_ORIGIN_ANY;
    create_info.acceptable_scheduling_modes =
        AMDF_XDNA_SCHEDULING_MODE_TIME_SLICED;
    return create_info;
  }

  const amdf_api_t* api_ = nullptr;
  const amdf_xdna_api_t* xdna_api_ = nullptr;
  amdf_instance_t* instance_ = nullptr;
  amdf_endpoint_t* endpoint_ = nullptr;
  amdf_device_t* device_ = nullptr;
};

TEST_F(XdnaEndpointTest, ReturnsQualifiedCachedProfile) {
  if (!OpenEngine(AMDF_ENGINE_KIND_XDNA)) {
    GTEST_SKIP() << "no qualified XDNA endpoint present";
  }

  amdf_xdna_endpoint_info_t info = {};
  info.type = AMDF_STRUCTURE_TYPE_XDNA_ENDPOINT_INFO;
  info.structure_size = sizeof(info);
  ASSERT_TRUE(
      amdf_status_is_ok(xdna_api_->endpoint_query_info(endpoint_, &info)));

  EXPECT_EQ(info.architecture, AMDF_XDNA_ARCHITECTURE_AIE2P);
  EXPECT_EQ(info.array.column_origin, 0u);
  EXPECT_EQ(info.array.column_count, 8u);
  EXPECT_EQ(info.array.row_count, 6u);
  EXPECT_EQ(info.array.column_stride, UINT64_C(1) << 25);
  EXPECT_EQ(info.context.scheduling_modes,
            AMDF_XDNA_SCHEDULING_MODE_TIME_SLICED);
  EXPECT_EQ(info.context.minimum_column_count, 1u);
  EXPECT_EQ(info.context.maximum_column_count, 8u);
  EXPECT_EQ(info.context.column_count_granularity, 1u);
  EXPECT_EQ(info.context.maximum_live_context_count, 32u);
  EXPECT_EQ(info.context.maximum_hardware_context_count, 16u);
  EXPECT_EQ(info.program.maximum_component_count, 1u);
  EXPECT_EQ(info.program.maximum_component_byte_length, 32u * 1024u);
  EXPECT_EQ(info.program.maximum_total_byte_length, 32u * 1024u);
  EXPECT_EQ(info.program.component_formats.array_configuration.format,
            AMDF_XDNA_BINARY_FORMAT_TRANSACTION);
  EXPECT_EQ(info.program.component_formats.array_configuration.version,
            AMDF_XDNA_TRANSACTION_FORMAT_VERSION_0_1);
  EXPECT_EQ(info.command.maximum_binding_count, 5u);
  EXPECT_EQ(info.command.maximum_control_byte_length, 32u * 1024u);
  EXPECT_EQ(info.command.maximum_native_byte_length, 32u * 1024u);
  EXPECT_EQ(info.command.control_format.format,
            AMDF_XDNA_BINARY_FORMAT_TRANSACTION);
  EXPECT_EQ(info.command.control_format.version,
            AMDF_XDNA_TRANSACTION_FORMAT_VERSION_0_1);
  EXPECT_STREQ(info.target_id, "amd.xdna.strix_halo.17f0_11");

  amdf_xdna_endpoint_info_t second_info = {};
  second_info.type = AMDF_STRUCTURE_TYPE_XDNA_ENDPOINT_INFO;
  second_info.structure_size = sizeof(second_info);
  ASSERT_TRUE(amdf_status_is_ok(
      xdna_api_->endpoint_query_info(endpoint_, &second_info)));
  EXPECT_EQ(std::memcmp(&info, &second_info, sizeof(info)), 0);
}

TEST_F(XdnaEndpointTest, RejectsMalformedOutputWithoutMutation) {
  if (!OpenEngine(AMDF_ENGINE_KIND_XDNA)) {
    GTEST_SKIP() << "no qualified XDNA endpoint present";
  }

  EXPECT_EQ(
      amdf_status_code(xdna_api_->endpoint_query_info(endpoint_, nullptr)),
      AMDF_STATUS_CODE_INVALID_ARGUMENT);

  amdf_xdna_endpoint_info_t info = {};
  info.structure_size = sizeof(info);
  info.architecture = UINT32_MAX;
  EXPECT_EQ(amdf_status_code(xdna_api_->endpoint_query_info(endpoint_, &info)),
            AMDF_STATUS_CODE_INVALID_ARGUMENT);
  EXPECT_EQ(info.architecture, UINT32_MAX);

  info.type = AMDF_STRUCTURE_TYPE_XDNA_ENDPOINT_INFO;
  info.next = &info;
  EXPECT_EQ(amdf_status_code(xdna_api_->endpoint_query_info(endpoint_, &info)),
            AMDF_STATUS_CODE_UNSUPPORTED);
  EXPECT_EQ(info.architecture, UINT32_MAX);
}

TEST_F(XdnaEndpointTest, RejectsGpuEndpointWithoutMutation) {
  if (!OpenEngine(AMDF_ENGINE_KIND_GPU)) {
    GTEST_SKIP() << "no GPU endpoint present";
  }

  amdf_xdna_endpoint_info_t info = {};
  info.type = AMDF_STRUCTURE_TYPE_XDNA_ENDPOINT_INFO;
  info.structure_size = sizeof(info);
  info.architecture = UINT32_MAX;
  const amdf_status_t status = xdna_api_->endpoint_query_info(endpoint_, &info);

  EXPECT_EQ(amdf_status_domain(status), AMDF_STATUS_DOMAIN_API);
  EXPECT_EQ(amdf_status_code(status), AMDF_STATUS_CODE_UNSUPPORTED);
  EXPECT_EQ(info.architecture, UINT32_MAX);
}

TEST_F(XdnaEndpointTest, ValidatesDeviceCreationArgumentsWithoutNativeWork) {
  if (!OpenEngine(AMDF_ENGINE_KIND_XDNA)) {
    GTEST_SKIP() << "no qualified XDNA endpoint present";
  }

  amdf_xdna_device_create_info_t create_info = MakeDeviceCreateInfo();
  amdf_device_t* output = reinterpret_cast<amdf_device_t*>(uintptr_t{1});
  EXPECT_EQ(amdf_status_code(
                xdna_api_->device_create(nullptr, &create_info, &output)),
            AMDF_STATUS_CODE_INVALID_ARGUMENT);
  EXPECT_EQ(output, nullptr);
  EXPECT_EQ(
      amdf_status_code(xdna_api_->device_create(endpoint_, nullptr, &output)),
      AMDF_STATUS_CODE_INVALID_ARGUMENT);
  EXPECT_EQ(output, nullptr);
  EXPECT_EQ(amdf_status_code(
                xdna_api_->device_create(endpoint_, &create_info, nullptr)),
            AMDF_STATUS_CODE_INVALID_ARGUMENT);

  create_info.type = AMDF_STRUCTURE_TYPE_NONE;
  EXPECT_EQ(amdf_status_code(
                xdna_api_->device_create(endpoint_, &create_info, &output)),
            AMDF_STATUS_CODE_INVALID_ARGUMENT);
  EXPECT_EQ(output, nullptr);

  create_info = MakeDeviceCreateInfo();
  create_info.acceptable_scheduling_modes = 0;
  EXPECT_EQ(amdf_status_code(
                xdna_api_->device_create(endpoint_, &create_info, &output)),
            AMDF_STATUS_CODE_INVALID_ARGUMENT);
  EXPECT_EQ(output, nullptr);

  create_info = MakeDeviceCreateInfo(0);
  EXPECT_EQ(amdf_status_code(
                xdna_api_->device_create(endpoint_, &create_info, &output)),
            AMDF_STATUS_CODE_OUT_OF_RANGE);
  EXPECT_EQ(output, nullptr);

  create_info = MakeDeviceCreateInfo();
  create_info.acceptable_scheduling_modes = UINT32_MAX;
  EXPECT_EQ(amdf_status_code(
                xdna_api_->device_create(endpoint_, &create_info, &output)),
            AMDF_STATUS_CODE_INVALID_ARGUMENT);
  EXPECT_EQ(output, nullptr);
}

TEST_F(XdnaEndpointTest, MaterializesProgramIndependentDevice) {
  if (!OpenEngine(AMDF_ENGINE_KIND_XDNA)) {
    GTEST_SKIP() << "no qualified XDNA endpoint present";
  }
  const amdf_xdna_device_create_info_t create_info = MakeDeviceCreateInfo();
  const amdf_status_t create_status =
      xdna_api_->device_create(endpoint_, &create_info, &device_);
  if (amdf_status_domain(create_status) == AMDF_STATUS_DOMAIN_API &&
      amdf_status_code(create_status) == AMDF_STATUS_CODE_UNSUPPORTED) {
    GTEST_SKIP() << "XDNA device materialization is unavailable";
  }
  ASSERT_TRUE(amdf_status_is_ok(create_status))
      << "domain=" << amdf_status_domain(create_status)
      << " code=" << amdf_status_code(create_status);
  ASSERT_NE(device_, nullptr);

  amdf_xdna_device_info_t info = {};
  info.type = AMDF_STRUCTURE_TYPE_XDNA_DEVICE_INFO;
  info.structure_size = sizeof(info);
  ASSERT_TRUE(amdf_status_is_ok(xdna_api_->device_query_info(device_, &info)));
  EXPECT_NE(info.id.words[0] | info.id.words[1], 0u);
  EXPECT_EQ(info.reset_epoch, 1u);
  EXPECT_EQ(info.scheduling_mode, AMDF_XDNA_SCHEDULING_MODE_TIME_SLICED);
  EXPECT_EQ(info.placement_generation, 1u);
  EXPECT_EQ(info.columns.logical_count, create_info.logical_column_count);
  EXPECT_EQ(info.columns.physical_origin, 0u);
  EXPECT_EQ(info.columns.physical_count, 8u);
  EXPECT_EQ(info.row_count, 6u);

  amdf_xdna_device_info_t second_info = {};
  second_info.type = AMDF_STRUCTURE_TYPE_XDNA_DEVICE_INFO;
  second_info.structure_size = sizeof(second_info);
  ASSERT_TRUE(
      amdf_status_is_ok(xdna_api_->device_query_info(device_, &second_info)));
  EXPECT_EQ(std::memcmp(&info, &second_info, sizeof(info)), 0);

  EXPECT_EQ(amdf_status_code(api_->endpoint_close(endpoint_)),
            AMDF_STATUS_CODE_BUSY);
}

TEST_F(XdnaEndpointTest, RejectsMalformedDeviceInfoWithoutMutation) {
  if (!OpenEngine(AMDF_ENGINE_KIND_XDNA)) {
    GTEST_SKIP() << "no qualified XDNA endpoint present";
  }
  const amdf_xdna_device_create_info_t create_info = MakeDeviceCreateInfo();
  const amdf_status_t create_status =
      xdna_api_->device_create(endpoint_, &create_info, &device_);
  if (amdf_status_domain(create_status) == AMDF_STATUS_DOMAIN_API &&
      amdf_status_code(create_status) == AMDF_STATUS_CODE_UNSUPPORTED) {
    GTEST_SKIP() << "XDNA device materialization is unavailable";
  }
  ASSERT_TRUE(amdf_status_is_ok(create_status))
      << "domain=" << amdf_status_domain(create_status)
      << " code=" << amdf_status_code(create_status);

  EXPECT_EQ(amdf_status_code(xdna_api_->device_query_info(device_, nullptr)),
            AMDF_STATUS_CODE_INVALID_ARGUMENT);
  EXPECT_EQ(amdf_status_code(xdna_api_->device_query_info(nullptr, nullptr)),
            AMDF_STATUS_CODE_INVALID_ARGUMENT);

  amdf_xdna_device_info_t info = {};
  info.structure_size = sizeof(info);
  info.reset_epoch = UINT64_MAX;
  EXPECT_EQ(amdf_status_code(xdna_api_->device_query_info(device_, &info)),
            AMDF_STATUS_CODE_INVALID_ARGUMENT);
  EXPECT_EQ(info.reset_epoch, UINT64_MAX);

  info.type = AMDF_STRUCTURE_TYPE_XDNA_DEVICE_INFO;
  info.next = &info;
  EXPECT_EQ(amdf_status_code(xdna_api_->device_query_info(device_, &info)),
            AMDF_STATUS_CODE_UNSUPPORTED);
  EXPECT_EQ(info.reset_epoch, UINT64_MAX);
}

TEST_F(XdnaEndpointTest, ValidatesDeviceDestructionArguments) {
  EXPECT_EQ(amdf_status_code(api_->device_destroy(nullptr)),
            AMDF_STATUS_CODE_INVALID_ARGUMENT);
}

}  // namespace
