// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/xdna/umd/drm/device.h"

#include <drm/amdxdna_accel.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <cstring>
#include <iostream>
#include <vector>

#include "gtest/gtest.h"
#include "libamdf/src/platform/linux/endpoint.h"
#include "libamdf/src/xdna/endpoint_profile.h"
#include "libamdf/src/xdna/target/npu5/bootstrap.h"
#include "libamdf/src/xdna/target/npu5/context.h"
#include "libamdf/src/xdna/umd/drm/memory.h"

namespace {

class LinuxXdnaDeviceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_EQ(amdf_platform_instance_create(&instance), AMDF_STATUS_OK);
    uint32_t count = 0;
    ASSERT_EQ(amdf_platform_endpoint_enumerate(instance, 0, nullptr, &count),
              AMDF_STATUS_OK);
    std::vector<amdf_endpoint_summary_t> summaries(count);
    ASSERT_EQ(amdf_platform_endpoint_enumerate(instance, count,
                                               summaries.data(), &count),
              AMDF_STATUS_OK);
    for (const auto& summary : summaries) {
      if (summary.engine_kind != AMDF_ENGINE_KIND_XDNA) continue;
      amdf_endpoint_info_t info;
      ASSERT_EQ(
          amdf_platform_endpoint_open(instance, &summary.id, &endpoint, &info),
          AMDF_STATUS_OK);
      profile = amdf_xdna_endpoint_profile_select(&info);
      if (profile != nullptr && profile->model == AMDF_PCI_XDNA_MODEL_NPU5)
        break;
      ASSERT_EQ(amdf_platform_endpoint_close(endpoint), AMDF_STATUS_OK);
      endpoint = nullptr;
    }
    if (endpoint == nullptr) GTEST_SKIP() << "No NPU5 endpoint";
  }

  void TearDown() override {
    std::cout << "Release host views and memory" << std::endl;
    for (auto* value : mappings) {
      if (value)
        EXPECT_EQ(amdf_xdna_umd_host_mapping_destroy(value), AMDF_STATUS_OK);
    }
    if (memory) EXPECT_EQ(amdf_xdna_umd_memory_destroy(memory), AMDF_STATUS_OK);
    for (auto* value : devices) {
      if (value) {
        std::cout << "Destroy native context" << std::endl;
        EXPECT_EQ(amdf_xdna_umd_device_destroy(value), AMDF_STATUS_OK);
      }
    }
    if (endpoint)
      EXPECT_EQ(amdf_platform_endpoint_close(endpoint), AMDF_STATUS_OK);
    if (instance)
      EXPECT_EQ(amdf_platform_instance_destroy(instance), AMDF_STATUS_OK);
  }

  // Native instance retained across early assertion exits.
  amdf_platform_instance_t* instance = nullptr;
  // Query endpoint owning any failed native constructions.
  amdf_platform_endpoint_t* endpoint = nullptr;
  // Static target profile selected from the opened endpoint.
  const amdf_xdna_endpoint_profile_t* profile = nullptr;
  // Independent native devices, each owning its own handle namespaces.
  amdf_xdna_umd_device_t* devices[2] = {};
  // Memory retained until every host view has been destroyed.
  amdf_xdna_umd_memory_t* memory = nullptr;
  // Independent host views into the same native attachment.
  amdf_xdna_umd_host_mapping_t* mappings[2] = {};
};

TEST_F(LinuxXdnaDeviceTest, IndependentContextsAndPersistentMappings) {
  amdf_xdna_device_create_info_t create_info = {};
  create_info.acceptable_scheduling_modes =
      AMDF_XDNA_SCHEDULING_MODE_TIME_SLICED;
  create_info.logical_column_count = 1;
  create_info.physical_column_origin = AMDF_XDNA_PHYSICAL_COLUMN_ORIGIN_ANY;
  amdf_xdna_umd_device_result_t results[2] = {};
  const void* pdi = nullptr;
  size_t pdi_byte_length = 0;
  amdf_xdna_npu5_bootstrap_query_pdi(&pdi, &pdi_byte_length);
  for (size_t i = 0; i < 2; ++i) {
    std::cout << "Create independent native context " << i << std::endl;
    ASSERT_EQ(amdf_xdna_umd_device_create(endpoint, profile, &create_info,
                                          &devices[i], &results[i]),
              AMDF_STATUS_OK);
    EXPECT_EQ(results[i].physical_column_origin, 0u);
    EXPECT_EQ(results[i].physical_column_count, 8u);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(devices[i]->heap.host_pointer) %
                  AMDF_XDNA_NPU5_HEAP_BYTE_LENGTH,
              0u);
    EXPECT_NE(fcntl(devices[i]->descriptor, F_GETFD) & FD_CLOEXEC, 0);
    EXPECT_EQ(
        std::memcmp(pdi, devices[i]->bootstrap.host_pointer, pdi_byte_length),
        0);
  }
  EXPECT_NE(devices[0]->descriptor, devices[1]->descriptor);
  EXPECT_NE(devices[0]->heap.host_pointer, devices[1]->heap.host_pointer);
  EXPECT_NE(results[0].id.words[0], results[1].id.words[0]);

  amdf_memory_create_info_t memory_create = {};
  memory_create.memory_class = AMDF_MEMORY_CLASS_SYSTEM;
  memory_create.required_flags =
      AMDF_MEMORY_FLAG_HOST_VISIBLE | AMDF_MEMORY_FLAG_DEVICE_ADDRESS;
  memory_create.byte_length = 4097;
  memory_create.minimum_alignment = 65536;
  amdf_xdna_umd_memory_result_t memory_result = {};
  std::cout << "Create aligned SHARE attachment" << std::endl;
  ASSERT_EQ(amdf_xdna_umd_memory_create(devices[0], &memory_create, &memory,
                                        &memory_result),
            AMDF_STATUS_OK);
  EXPECT_GE(memory_result.byte_length, memory_create.byte_length);
  EXPECT_EQ(memory_result.device_address % memory_create.minimum_alignment, 0u);
  amdf_memory_map_info_t map_info = {};
  map_info.byte_offset = 1;
  map_info.byte_length = 4096;
  map_info.flags = AMDF_MEMORY_MAP_FLAG_READ | AMDF_MEMORY_MAP_FLAG_WRITE;
  amdf_xdna_umd_host_mapping_result_t views[2] = {};
  for (size_t i = 0; i < 2; ++i) {
    ASSERT_EQ(
        amdf_xdna_umd_memory_map(memory, &map_info, &mappings[i], &views[i]),
        AMDF_STATUS_OK);
  }
  EXPECT_EQ(reinterpret_cast<uintptr_t>(views[0].pointer),
            memory_result.device_address + map_info.byte_offset);
  EXPECT_EQ(views[0].pointer, views[1].pointer);
  std::memset(views[0].pointer, 0xA5, map_info.byte_length);
  ASSERT_EQ(amdf_xdna_umd_host_mapping_cache_control(
                mappings[0], AMDF_HOST_CACHE_OPERATION_FLUSH, 0,
                map_info.byte_length),
            AMDF_STATUS_OK);
  ASSERT_EQ(amdf_xdna_umd_host_mapping_destroy(mappings[0]), AMDF_STATUS_OK);
  mappings[0] = nullptr;
  std::cout << "First view destroyed; attachment remains mapped" << std::endl;
  struct amdxdna_drm_get_bo_info native_info = {};
  native_info.handle = memory->buffer.handle;
  ASSERT_EQ(ioctl(devices[0]->descriptor, DRM_IOCTL_AMDXDNA_GET_BO_INFO,
                  &native_info),
            0);
  EXPECT_EQ(native_info.xdna_addr, memory_result.device_address);
  EXPECT_EQ(native_info.vaddr, memory_result.device_address);
  EXPECT_EQ(static_cast<uint8_t*>(views[1].pointer)[0], 0xA5);
  EXPECT_EQ(static_cast<uint8_t*>(views[1].pointer)[4095], 0xA5);
  ASSERT_EQ(amdf_xdna_umd_host_mapping_cache_control(
                mappings[1], AMDF_HOST_CACHE_OPERATION_INVALIDATE, 0,
                map_info.byte_length),
            AMDF_STATUS_OK);
  EXPECT_EQ(static_cast<uint8_t*>(views[1].pointer)[4095], 0xA5);

  std::cout << "Destroy sibling context while memory stays live" << std::endl;
  ASSERT_EQ(amdf_xdna_umd_device_destroy(devices[1]), AMDF_STATUS_OK);
  devices[1] = nullptr;
  EXPECT_EQ(static_cast<uint8_t*>(views[1].pointer)[4095], 0xA5);
}

}  // namespace
