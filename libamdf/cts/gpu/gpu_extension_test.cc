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
#include "amdf/gpu.h"
#include "gtest/gtest.h"
#include "util/provider.h"

namespace {

static_assert(offsetof(amdf_gpu_endpoint_info_t, gfx_ip) ==
              sizeof(amdf_output_structure_t));
static_assert(offsetof(amdf_gpu_endpoint_info_t, asic_revision) == 28);
static_assert(offsetof(amdf_gpu_endpoint_info_t, compute) == 32);
static_assert(offsetof(amdf_gpu_endpoint_info_t, topology) == 56);
static_assert(sizeof(amdf_gpu_endpoint_info_t) == 64);
static_assert(offsetof(amdf_gpu_api_t, endpoint_query_info) +
                  sizeof(amdf_gpu_api_t::endpoint_query_info) ==
              sizeof(amdf_gpu_api_t));

const amdf_api_t* QueryApi() {
  const amdf_api_t* api = nullptr;
  EXPECT_TRUE(amdf_status_is_ok(amdf_cts_provider_query_api()(
      AMDF_ABI_VERSION_1, AMDF_ABI_VERSION_LATEST, &api)));
  return api;
}

const amdf_gpu_api_t* QueryGpuApi(const amdf_api_t* api) {
  const void* extension_api = nullptr;
  EXPECT_TRUE(amdf_status_is_ok(
      api->query_extension(AMDF_EXTENSION_GPU, AMDF_GPU_EXTENSION_VERSION_1,
                           AMDF_GPU_EXTENSION_VERSION_LATEST, &extension_api)));
  return static_cast<const amdf_gpu_api_t*>(extension_api);
}

TEST(GpuExtensionTest, ReportsCompiledAvailabilityBeforeCreatingInstance) {
  const amdf_api_t* api = QueryApi();
  ASSERT_NE(api, nullptr);

  const amdf_gpu_api_t* gpu_api = QueryGpuApi(api);

  ASSERT_NE(gpu_api, nullptr);
  EXPECT_EQ(gpu_api->structure_size, sizeof(amdf_gpu_api_t));
  EXPECT_EQ(gpu_api->extension_version, AMDF_GPU_EXTENSION_VERSION_1);
  EXPECT_NE(gpu_api->endpoint_query_info, nullptr);
}

TEST(GpuExtensionTest, ReturnsStableImmutableTable) {
  const amdf_api_t* api = QueryApi();
  ASSERT_NE(api, nullptr);

  const amdf_gpu_api_t* first_api = QueryGpuApi(api);
  const amdf_gpu_api_t* second_api = QueryGpuApi(api);

  EXPECT_EQ(first_api, second_api);
}

TEST(GpuExtensionTest, RejectsUnsupportedVersionAndClearsOutput) {
  const amdf_api_t* api = QueryApi();
  ASSERT_NE(api, nullptr);
  const void* extension_api = reinterpret_cast<const void*>(uintptr_t{1});

  const amdf_status_t status = api->query_extension(
      AMDF_EXTENSION_GPU, AMDF_GPU_EXTENSION_VERSION_LATEST + 1,
      AMDF_GPU_EXTENSION_VERSION_LATEST + 1, &extension_api);

  EXPECT_EQ(amdf_status_domain(status), AMDF_STATUS_DOMAIN_API);
  EXPECT_EQ(amdf_status_code(status), AMDF_STATUS_CODE_VERSION_MISMATCH);
  EXPECT_EQ(extension_api, nullptr);
}

class GpuEndpointTest : public ::testing::Test {
 protected:
  void SetUp() override {
    api_ = QueryApi();
    ASSERT_NE(api_, nullptr);
    gpu_api_ = QueryGpuApi(api_);
    ASSERT_NE(gpu_api_, nullptr);

    amdf_instance_create_info_t create_info = {};
    create_info.type = AMDF_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.structure_size = sizeof(create_info);
    ASSERT_TRUE(
        amdf_status_is_ok(api_->instance_create(&create_info, &instance_)));
  }

  void TearDown() override {
    if (endpoint_ != nullptr) {
      EXPECT_TRUE(amdf_status_is_ok(api_->endpoint_close(endpoint_)));
    }
    if (instance_ != nullptr) {
      EXPECT_TRUE(amdf_status_is_ok(api_->instance_destroy(instance_)));
    }
  }

  amdf_status_t OpenEngine(amdf_engine_kind_t engine_kind,
                           bool* out_engine_found) {
    *out_engine_found = false;
    uint32_t endpoint_count = 0;
    amdf_status_t status =
        api_->endpoint_enumerate(instance_, 0, nullptr, &endpoint_count);
    if (!amdf_status_is_ok(status)) {
      return status;
    }
    std::vector<amdf_endpoint_summary_t> summaries(endpoint_count);
    if (endpoint_count != 0) {
      status = api_->endpoint_enumerate(instance_, endpoint_count,
                                        summaries.data(), &endpoint_count);
      if (!amdf_status_is_ok(status)) {
        return status;
      }
    }
    for (uint32_t endpoint_ordinal = 0; endpoint_ordinal < endpoint_count;
         ++endpoint_ordinal) {
      const amdf_endpoint_summary_t& summary = summaries[endpoint_ordinal];
      if (summary.engine_kind == engine_kind) {
        status = api_->endpoint_open(instance_, &summary.id, &endpoint_);
        if (amdf_status_is_ok(status) && endpoint_ == nullptr) {
          return amdf_make_api_status(AMDF_STATUS_CODE_INTERNAL);
        }
        *out_engine_found = amdf_status_is_ok(status);
        return status;
      }
    }
    return AMDF_STATUS_OK;
  }

  const amdf_api_t* api_ = nullptr;
  const amdf_gpu_api_t* gpu_api_ = nullptr;
  amdf_instance_t* instance_ = nullptr;
  amdf_endpoint_t* endpoint_ = nullptr;
};

TEST_F(GpuEndpointTest, ReturnsQualifiedCachedProfile) {
  bool engine_found = false;
  const amdf_status_t open_status =
      OpenEngine(AMDF_ENGINE_KIND_GPU, &engine_found);
  ASSERT_TRUE(amdf_status_is_ok(open_status))
      << "domain=" << amdf_status_domain(open_status)
      << " code=" << amdf_status_code(open_status);
  if (!engine_found) {
    GTEST_SKIP() << "no GPU endpoint present";
  }

  amdf_gpu_endpoint_info_t info = {};
  info.type = AMDF_STRUCTURE_TYPE_GPU_ENDPOINT_INFO;
  info.structure_size = sizeof(info);
  const amdf_status_t status = gpu_api_->endpoint_query_info(endpoint_, &info);
  if (amdf_status_domain(status) == AMDF_STATUS_DOMAIN_API &&
      amdf_status_code(status) == AMDF_STATUS_CODE_UNSUPPORTED) {
    GTEST_SKIP() << "GPU endpoint is not qualified by this provider";
  }
  ASSERT_TRUE(amdf_status_is_ok(status))
      << "domain=" << amdf_status_domain(status)
      << " code=" << amdf_status_code(status);

  EXPECT_GT(info.gfx_ip.major, 0u);
  EXPECT_TRUE(info.compute.wavefront_size == 32u ||
              info.compute.wavefront_size == 64u);
  EXPECT_GT(info.compute.compute_unit_count, 0u);
  EXPECT_GT(info.compute.maximum_wave_count_per_compute_unit, 0u);
  EXPECT_GT(info.compute.maximum_scratch_wave_count_per_compute_unit, 0u);
  EXPECT_LE(info.compute.maximum_scratch_wave_count_per_compute_unit,
            info.compute.maximum_wave_count_per_compute_unit);
  EXPECT_GT(info.compute.local_data_share_byte_length, 0u);
  EXPECT_GT(info.topology.xcc_count, 0u);
  EXPECT_GT(info.topology.shader_engine_count_per_xcc, 0u);

  amdf_gpu_endpoint_info_t second_info = {};
  second_info.type = AMDF_STRUCTURE_TYPE_GPU_ENDPOINT_INFO;
  second_info.structure_size = sizeof(second_info);
  ASSERT_TRUE(amdf_status_is_ok(
      gpu_api_->endpoint_query_info(endpoint_, &second_info)));
  EXPECT_EQ(std::memcmp(&info, &second_info, sizeof(info)), 0);
}

TEST_F(GpuEndpointTest, RejectsMalformedOutputWithoutMutation) {
  bool engine_found = false;
  const amdf_status_t open_status =
      OpenEngine(AMDF_ENGINE_KIND_GPU, &engine_found);
  ASSERT_TRUE(amdf_status_is_ok(open_status));
  if (!engine_found) {
    GTEST_SKIP() << "no GPU endpoint present";
  }

  EXPECT_EQ(amdf_status_code(gpu_api_->endpoint_query_info(endpoint_, nullptr)),
            AMDF_STATUS_CODE_INVALID_ARGUMENT);

  amdf_gpu_endpoint_info_t info = {};
  info.structure_size = sizeof(info);
  info.gfx_ip.major = UINT32_MAX;
  EXPECT_EQ(amdf_status_code(gpu_api_->endpoint_query_info(endpoint_, &info)),
            AMDF_STATUS_CODE_INVALID_ARGUMENT);
  EXPECT_EQ(info.gfx_ip.major, UINT32_MAX);

  info.type = AMDF_STRUCTURE_TYPE_GPU_ENDPOINT_INFO;
  info.next = &info;
  EXPECT_EQ(amdf_status_code(gpu_api_->endpoint_query_info(endpoint_, &info)),
            AMDF_STATUS_CODE_UNSUPPORTED);
  EXPECT_EQ(info.gfx_ip.major, UINT32_MAX);
}

TEST_F(GpuEndpointTest, RejectsXdnaEndpointWithoutMutation) {
  bool engine_found = false;
  const amdf_status_t open_status =
      OpenEngine(AMDF_ENGINE_KIND_XDNA, &engine_found);
  ASSERT_TRUE(amdf_status_is_ok(open_status));
  if (!engine_found) {
    GTEST_SKIP() << "no XDNA endpoint present";
  }

  amdf_gpu_endpoint_info_t info = {};
  info.type = AMDF_STRUCTURE_TYPE_GPU_ENDPOINT_INFO;
  info.structure_size = sizeof(info);
  info.gfx_ip.major = UINT32_MAX;
  const amdf_status_t status = gpu_api_->endpoint_query_info(endpoint_, &info);

  EXPECT_EQ(amdf_status_domain(status), AMDF_STATUS_DOMAIN_API);
  EXPECT_EQ(amdf_status_code(status), AMDF_STATUS_CODE_UNSUPPORTED);
  EXPECT_EQ(info.gfx_ip.major, UINT32_MAX);
}

}  // namespace
