// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef AMDF_CTS_API_XDNA_DEVICE_FIXTURE_H_
#define AMDF_CTS_API_XDNA_DEVICE_FIXTURE_H_

#include <vector>

#include "amdf/amdf.h"
#include "amdf/xdna.h"
#include "gtest/gtest.h"
#include "util/provider.h"

// Materializes the first qualified XDNA endpoint and one single-column device.
class XdnaDeviceFixture : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(amdf_status_is_ok(amdf_cts_provider_query_api()(
        AMDF_ABI_VERSION_1, AMDF_ABI_VERSION_LATEST, &api_)));
    ASSERT_NE(api_, nullptr);

    const void* extension_api = nullptr;
    ASSERT_TRUE(amdf_status_is_ok(api_->query_extension(
        AMDF_EXTENSION_XDNA, AMDF_XDNA_EXTENSION_VERSION_1,
        AMDF_XDNA_EXTENSION_VERSION_LATEST, &extension_api)));
    xdna_api_ = static_cast<const amdf_xdna_api_t*>(extension_api);
    ASSERT_NE(xdna_api_, nullptr);

    amdf_instance_create_info_t instance_create_info = {};
    instance_create_info.type = AMDF_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_create_info.structure_size = sizeof(instance_create_info);
    amdf_status_t status =
        api_->instance_create(&instance_create_info, &instance_);
    if (amdf_status_domain(status) == AMDF_STATUS_DOMAIN_API &&
        amdf_status_code(status) == AMDF_STATUS_CODE_UNSUPPORTED) {
      GTEST_SKIP() << "platform provider is not implemented";
    }
    ASSERT_TRUE(amdf_status_is_ok(status));

    uint32_t endpoint_count = 0;
    ASSERT_TRUE(amdf_status_is_ok(
        api_->endpoint_enumerate(instance_, 0, nullptr, &endpoint_count)));
    std::vector<amdf_endpoint_summary_t> summaries(endpoint_count);
    if (endpoint_count != 0) {
      ASSERT_TRUE(amdf_status_is_ok(api_->endpoint_enumerate(
          instance_, endpoint_count, summaries.data(), &endpoint_count)));
    }
    for (const amdf_endpoint_summary_t& summary : summaries) {
      if (summary.engine_kind == AMDF_ENGINE_KIND_XDNA) {
        ASSERT_TRUE(amdf_status_is_ok(
            api_->endpoint_open(instance_, &summary.id, &endpoint_)));
        break;
      }
    }
    if (endpoint_ == nullptr) {
      GTEST_SKIP() << "no qualified XDNA endpoint present";
    }

    amdf_xdna_device_create_info_t device_create_info = {};
    device_create_info.type = AMDF_STRUCTURE_TYPE_XDNA_DEVICE_CREATE_INFO;
    device_create_info.structure_size = sizeof(device_create_info);
    device_create_info.logical_column_count = 1;
    device_create_info.physical_column_origin =
        AMDF_XDNA_PHYSICAL_COLUMN_ORIGIN_ANY;
    device_create_info.acceptable_scheduling_modes =
        AMDF_XDNA_SCHEDULING_MODE_TIME_SLICED;
    status = xdna_api_->device_create(endpoint_, &device_create_info, &device_);
    if (amdf_status_domain(status) == AMDF_STATUS_DOMAIN_API &&
        amdf_status_code(status) == AMDF_STATUS_CODE_UNSUPPORTED) {
      GTEST_SKIP() << "XDNA device materialization is unavailable";
    }
    ASSERT_TRUE(amdf_status_is_ok(status))
        << "domain=" << amdf_status_domain(status)
        << " code=" << amdf_status_code(status);
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

  const amdf_api_t* api_ = nullptr;
  const amdf_xdna_api_t* xdna_api_ = nullptr;
  amdf_instance_t* instance_ = nullptr;
  amdf_endpoint_t* endpoint_ = nullptr;
  amdf_device_t* device_ = nullptr;
};

#endif  // AMDF_CTS_API_XDNA_DEVICE_FIXTURE_H_
