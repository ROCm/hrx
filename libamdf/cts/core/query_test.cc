// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <cstdint>

#include "amdf/amdf.h"
#include "gtest/gtest.h"
#include "util/provider.h"

namespace {

TEST(QueryApiTest, NegotiatesSupportedVersion) {
  const amdf_api_t* api = nullptr;
  const amdf_status_t status = amdf_cts_provider_query_api()(
      AMDF_ABI_VERSION_1, AMDF_ABI_VERSION_LATEST, &api);

  EXPECT_TRUE(amdf_status_is_ok(status));
  ASSERT_NE(api, nullptr);
  EXPECT_EQ(api->structure_size, sizeof(amdf_api_t));
  EXPECT_EQ(api->abi_version, AMDF_ABI_VERSION_1);
  EXPECT_NE(api->instance_create, nullptr);
  EXPECT_NE(api->instance_destroy, nullptr);
  EXPECT_NE(api->endpoint_enumerate, nullptr);
  EXPECT_NE(api->endpoint_open, nullptr);
  EXPECT_NE(api->endpoint_query_info, nullptr);
  EXPECT_NE(api->endpoint_close, nullptr);
}

TEST(QueryApiTest, ReturnsStableImmutableTable) {
  const amdf_api_t* first_api = nullptr;
  const amdf_api_t* second_api = nullptr;

  ASSERT_TRUE(amdf_status_is_ok(amdf_cts_provider_query_api()(
      AMDF_ABI_VERSION_1, AMDF_ABI_VERSION_1, &first_api)));
  ASSERT_TRUE(amdf_status_is_ok(amdf_cts_provider_query_api()(
      AMDF_ABI_VERSION_1, AMDF_ABI_VERSION_1, &second_api)));
  EXPECT_EQ(first_api, second_api);
}

TEST(QueryApiTest, RejectsNullOutput) {
  const amdf_status_t status = amdf_cts_provider_query_api()(
      AMDF_ABI_VERSION_1, AMDF_ABI_VERSION_1, nullptr);

  EXPECT_EQ(amdf_status_domain(status), AMDF_STATUS_DOMAIN_API);
  EXPECT_EQ(amdf_status_code(status), AMDF_STATUS_CODE_INVALID_ARGUMENT);
}

TEST(QueryApiTest, RejectsReversedVersionRangeAndClearsOutput) {
  const amdf_api_t* api = reinterpret_cast<const amdf_api_t*>(uintptr_t{1});
  const amdf_status_t status = amdf_cts_provider_query_api()(
      AMDF_ABI_VERSION_1 + 1, AMDF_ABI_VERSION_1, &api);

  EXPECT_EQ(amdf_status_domain(status), AMDF_STATUS_DOMAIN_API);
  EXPECT_EQ(amdf_status_code(status), AMDF_STATUS_CODE_INVALID_ARGUMENT);
  EXPECT_EQ(api, nullptr);
}

TEST(QueryApiTest, RejectsUnsupportedVersionAndClearsOutput) {
  const amdf_api_t* api = reinterpret_cast<const amdf_api_t*>(uintptr_t{1});
  const amdf_status_t status = amdf_cts_provider_query_api()(
      AMDF_ABI_VERSION_LATEST + 1, AMDF_ABI_VERSION_LATEST + 1, &api);

  EXPECT_EQ(amdf_status_domain(status), AMDF_STATUS_DOMAIN_API);
  EXPECT_EQ(amdf_status_code(status), AMDF_STATUS_CODE_VERSION_MISMATCH);
  EXPECT_EQ(api, nullptr);
}

TEST(StatusTest, PreservesDomainAndCode) {
  const amdf_status_t status =
      amdf_make_status(AMDF_STATUS_DOMAIN_FIRMWARE, 0xFEDCBA98u);

  EXPECT_FALSE(amdf_status_is_ok(status));
  EXPECT_EQ(amdf_status_domain(status), AMDF_STATUS_DOMAIN_FIRMWARE);
  EXPECT_EQ(amdf_status_code(status), 0xFEDCBA98u);
  EXPECT_TRUE(amdf_status_is_ok(AMDF_STATUS_OK));
  EXPECT_TRUE(amdf_status_is_ok(amdf_make_status(AMDF_STATUS_DOMAIN_ERRNO, 0)));
}

}  // namespace
