// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <algorithm>
#include <vector>

#include "iree/hal/cts/util/test_base.h"

namespace iree::hal::cts {

class QueueTest : public CtsTestBase<> {};

static void ExpectQueuePropertiesSupportedByFamily(
    const iree_hal_queue_t* queue,
    const iree_hal_queue_family_spec_t* family_spec) {
  bool priority_supported = false;
  for (iree_host_size_t i = 0; i < family_spec->priority_count; ++i) {
    priority_supported |=
        family_spec->priorities[i] == iree_hal_queue_priority(queue);
  }
  EXPECT_TRUE(priority_supported);
  EXPECT_EQ(0u, iree_hal_queue_features(queue) &
                    ~family_spec->supported_queue_features);

  const iree_hal_queue_execution_resource_list_t resources =
      iree_hal_queue_execution_resources(queue);
  if (!resources.count) return;
  ASSERT_NE(nullptr, resources.ordinals);
  std::vector<iree_host_size_t> group_resource_counts(
      family_spec->execution_resource_group_count, 0);
  for (iree_host_size_t i = 0; i < resources.count; ++i) {
    if (i > 0) EXPECT_LT(resources.ordinals[i - 1], resources.ordinals[i]);
    ASSERT_LT(resources.ordinals[i], family_spec->execution_resource_count);
    const iree_hal_queue_execution_resource_group_ordinal_t group_ordinal =
        family_spec->execution_resources[resources.ordinals[i]].group_ordinal;
    ASSERT_LT(group_ordinal, group_resource_counts.size());
    ++group_resource_counts[group_ordinal];
  }
  for (iree_host_size_t i = 0; i < group_resource_counts.size(); ++i) {
    EXPECT_GE(group_resource_counts[i],
              family_spec->execution_resource_groups[i]
                  .minimum_selected_resource_count);
  }
}

TEST_P(QueueTest, ProvisionedInventoryMatchesDeviceSpec) {
  const iree_hal_device_queue_spec_t* queue_spec =
      iree_hal_device_spec_queues(iree_hal_device_spec(device_));
  const iree_host_size_t family_count =
      queue_spec ? queue_spec->family_count : 0;

  std::vector<const iree_hal_queue_family_t*> observed_families;
  std::vector<iree_hal_queue_t*> observed_queues;
  for (iree_host_size_t i = 0; i < family_count; ++i) {
    const iree_hal_queue_family_ordinal_t family_ordinal =
        (iree_hal_queue_family_ordinal_t)i;
    const iree_hal_queue_family_t* queue_family =
        iree_hal_device_queue_family(device_, family_ordinal);
    ASSERT_NE(nullptr, queue_family)
        << "device did not expose advertised queue family " << i;
    EXPECT_EQ(family_ordinal, iree_hal_queue_family_ordinal(queue_family));
    const iree_hal_queue_family_spec_t* family_spec = &queue_spec->families[i];
    EXPECT_EQ(family_spec, iree_hal_queue_family_spec(queue_family));
    EXPECT_EQ(queue_family,
              iree_hal_device_queue_family(device_, family_ordinal));
    EXPECT_EQ(observed_families.end(),
              std::find(observed_families.begin(), observed_families.end(),
                        queue_family))
        << "queue family " << i << " aliases an earlier family identity";
    observed_families.push_back(queue_family);

    const uint32_t queue_count =
        queue_spec->families[i].provisioned_queue_count;
    for (uint32_t j = 0; j < queue_count; ++j) {
      const iree_hal_queue_ordinal_t queue_ordinal =
          (iree_hal_queue_ordinal_t)j;
      iree_hal_queue_t* queue =
          iree_hal_device_queue(device_, family_ordinal, queue_ordinal);
      ASSERT_NE(nullptr, queue)
          << "device did not expose advertised queue " << i << ":" << j;
      EXPECT_EQ(queue,
                iree_hal_device_queue(device_, family_ordinal, queue_ordinal));
      EXPECT_EQ(queue_family, iree_hal_queue_family(queue));
      ExpectQueuePropertiesSupportedByFamily(queue, family_spec);
      EXPECT_EQ(observed_queues.end(), std::find(observed_queues.begin(),
                                                 observed_queues.end(), queue))
          << "queue " << i << ":" << j
          << " aliases an earlier provisioned queue";
      observed_queues.push_back(queue);
    }
    EXPECT_EQ(nullptr,
              iree_hal_device_queue(device_, family_ordinal, queue_count));
  }

  const iree_hal_queue_family_ordinal_t invalid_family_ordinal =
      (iree_hal_queue_family_ordinal_t)family_count;
  EXPECT_EQ(nullptr,
            iree_hal_device_queue_family(device_, invalid_family_ordinal));
  EXPECT_EQ(nullptr, iree_hal_device_queue(device_, invalid_family_ordinal, 0));
}

CTS_REGISTER_TEST_SUITE(QueueTest);

}  // namespace iree::hal::cts
