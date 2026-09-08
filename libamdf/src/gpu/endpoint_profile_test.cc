// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/gpu/endpoint_profile.h"

#include <cstring>

#include "gtest/gtest.h"

namespace {

amdf_gpu_endpoint_properties_t MakeProperties(
    uint32_t major, uint32_t minor, uint32_t stepping, uint32_t asic_revision,
    uint32_t wavefront_size, uint32_t compute_unit_count,
    uint32_t maximum_wave_count_per_compute_unit,
    uint32_t maximum_scratch_wave_count_per_compute_unit,
    uint64_t local_data_share_byte_length, uint32_t xcc_count,
    uint32_t shader_engine_count_per_xcc) {
  amdf_gpu_endpoint_properties_t properties = {};
  properties.gfx_ip.major = major;
  properties.gfx_ip.minor = minor;
  properties.gfx_ip.stepping = stepping;
  properties.asic_revision = asic_revision;
  properties.compute.wavefront_size = wavefront_size;
  properties.compute.compute_unit_count = compute_unit_count;
  properties.compute.maximum_wave_count_per_compute_unit =
      maximum_wave_count_per_compute_unit;
  properties.compute.maximum_scratch_wave_count_per_compute_unit =
      maximum_scratch_wave_count_per_compute_unit;
  properties.compute.local_data_share_byte_length =
      local_data_share_byte_length;
  properties.topology.xcc_count = xcc_count;
  properties.topology.shader_engine_count_per_xcc = shader_engine_count_per_xcc;
  properties.supports_pm4_kernel_queue = true;
  return properties;
}

TEST(GpuEndpointProfileTest, QualifiesRdnaCdnaAndMultiXccProfiles) {
  struct TestCase {
    const char* name;
    amdf_gpu_endpoint_properties_t properties;
  };
  const TestCase test_cases[] = {
      {"gfx1151",
       MakeProperties(11, 5, 1, 1, 32, 40, 32, 32, 64u * 1024u, 1, 2)},
      {"gfx1100",
       MakeProperties(11, 0, 0, 0, 32, 96, 32, 32, 64u * 1024u, 1, 6)},
      {"gfx90a",
       MakeProperties(9, 0, 10, 1, 64, 104, 32, 32, 64u * 1024u, 1, 8)},
      {"gfx1201",
       MakeProperties(12, 0, 1, 0, 32, 64, 32, 32, 64u * 1024u, 1, 4)},
      {"gfx1250-a0",
       MakeProperties(12, 5, 0, 0, 32, 256, 64, 32, 320u * 1024u, 8, 2)},
      {"gfx1250-b0",
       MakeProperties(12, 5, 0, 1, 32, 256, 64, 32, 320u * 1024u, 8, 2)},
  };

  for (const TestCase& test_case : test_cases) {
    SCOPED_TRACE(test_case.name);
    const amdf_gpu_endpoint_properties_t& properties = test_case.properties;
    amdf_gpu_endpoint_profile_t profile = {};
    ASSERT_TRUE(amdf_gpu_endpoint_profile_initialize(&properties, &profile));
    const amdf_gpu_endpoint_info_t* info =
        amdf_gpu_endpoint_profile_get_info(&profile);
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->type, AMDF_STRUCTURE_TYPE_GPU_ENDPOINT_INFO);
    EXPECT_EQ(info->structure_size, sizeof(*info));
    EXPECT_EQ(info->next, nullptr);
    EXPECT_EQ(info->gfx_ip.major, properties.gfx_ip.major);
    EXPECT_EQ(info->gfx_ip.minor, properties.gfx_ip.minor);
    EXPECT_EQ(info->gfx_ip.stepping, properties.gfx_ip.stepping);
    EXPECT_EQ(info->asic_revision, properties.asic_revision);
    EXPECT_EQ(info->compute.wavefront_size, properties.compute.wavefront_size);
    EXPECT_EQ(info->compute.compute_unit_count,
              properties.compute.compute_unit_count);
    EXPECT_EQ(info->compute.maximum_wave_count_per_compute_unit,
              properties.compute.maximum_wave_count_per_compute_unit);
    EXPECT_EQ(info->compute.maximum_scratch_wave_count_per_compute_unit,
              properties.compute.maximum_scratch_wave_count_per_compute_unit);
    EXPECT_EQ(info->compute.local_data_share_byte_length,
              properties.compute.local_data_share_byte_length);
    EXPECT_EQ(info->topology.xcc_count, properties.topology.xcc_count);
    EXPECT_EQ(info->topology.shader_engine_count_per_xcc,
              properties.topology.shader_engine_count_per_xcc);
    EXPECT_EQ(profile.supports_pm4_kernel_queue,
              properties.supports_pm4_kernel_queue);
  }
}

TEST(GpuEndpointProfileTest, RejectsIncompleteOrInconsistentProperties) {
  const amdf_gpu_endpoint_properties_t valid =
      MakeProperties(12, 5, 0, 1, 32, 256, 64, 32, 320u * 1024u, 8, 2);
  EXPECT_FALSE(amdf_gpu_endpoint_profile_initialize(nullptr, nullptr));
  EXPECT_FALSE(amdf_gpu_endpoint_profile_initialize(&valid, nullptr));

  amdf_gpu_endpoint_profile_t profile;
  std::memset(&profile, 0xA5, sizeof(profile));
  const amdf_gpu_endpoint_profile_t original = profile;
  amdf_gpu_endpoint_properties_t properties = valid;
  properties.gfx_ip.major = 0;
  EXPECT_FALSE(amdf_gpu_endpoint_profile_initialize(&properties, &profile));
  EXPECT_EQ(std::memcmp(&profile, &original, sizeof(profile)), 0);

  properties = valid;
  properties.compute.wavefront_size = 16;
  EXPECT_FALSE(amdf_gpu_endpoint_profile_initialize(&properties, &profile));
  properties = valid;
  properties.compute.compute_unit_count = 0;
  EXPECT_FALSE(amdf_gpu_endpoint_profile_initialize(&properties, &profile));
  properties = valid;
  properties.compute.maximum_wave_count_per_compute_unit = 0;
  EXPECT_FALSE(amdf_gpu_endpoint_profile_initialize(&properties, &profile));
  properties = valid;
  properties.compute.maximum_scratch_wave_count_per_compute_unit = 0;
  EXPECT_FALSE(amdf_gpu_endpoint_profile_initialize(&properties, &profile));
  properties = valid;
  properties.compute.maximum_scratch_wave_count_per_compute_unit = 65;
  EXPECT_FALSE(amdf_gpu_endpoint_profile_initialize(&properties, &profile));
  properties = valid;
  properties.compute.local_data_share_byte_length = 0;
  EXPECT_FALSE(amdf_gpu_endpoint_profile_initialize(&properties, &profile));
  properties = valid;
  properties.topology.xcc_count = 0;
  EXPECT_FALSE(amdf_gpu_endpoint_profile_initialize(&properties, &profile));
  properties = valid;
  properties.topology.shader_engine_count_per_xcc = 0;
  EXPECT_FALSE(amdf_gpu_endpoint_profile_initialize(&properties, &profile));
}

}  // namespace
