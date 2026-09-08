// Copyright 2026 The HRX Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "binding/hip/execution_resource.h"

#include <string.h>

#include "common/internal.h"

// "HRXR" identifies an HRX execution-resource token in the opaque HIP bytes.
#define IREE_HIP_EXECUTION_RESOURCE_TOKEN_MAGIC 0x52585248u
#define IREE_HIP_EXECUTION_RESOURCE_TOKEN_VERSION 1u

typedef enum iree_hip_execution_resource_token_kind_e {
  IREE_HIP_EXECUTION_RESOURCE_TOKEN_KIND_SM = 1,
} iree_hip_execution_resource_token_kind_t;

// Pointer-free token stored in hipDevResource::_internal_padding.
typedef struct iree_hip_execution_resource_token_t {
  // Format discriminator identifying an HRX execution-resource token.
  uint32_t magic;

  // Token format version.
  uint16_t version;

  // Resource payload kind from iree_hip_execution_resource_token_kind_t.
  uint16_t kind;

  // Streaming device ordinal that owned the resource when it was created.
  uint32_t device_ordinal;

  // Exact HIP SM resource flags copied into the public payload.
  uint32_t resource_flags;

  // Exact device-table incarnation that owns |set_id|.
  uint64_t table_generation;

  // Immutable execution-resource set identity within the device table.
  iree_hal_streaming_execution_resource_set_id_t set_id;
} iree_hip_execution_resource_token_t;

_Static_assert(
    sizeof(iree_hip_execution_resource_token_t) <=
        sizeof(((hipDevResource*)0)->_internal_padding),
    "execution-resource token must fit in the HIP opaque ABI storage");

static uint32_t iree_hip_execution_resource_gcd(uint32_t lhs, uint32_t rhs) {
  while (rhs != 0) {
    const uint32_t remainder = lhs % rhs;
    lhs = rhs;
    rhs = remainder;
  }
  return lhs;
}

// Derives the smallest valid raw-unit partition and common raw-unit alignment
// from one exact set. This remains generic over families whose selectable
// resources cover different numbers of raw execution units.
static bool iree_hip_execution_resource_measure_sm_constraints(
    const iree_hal_queue_family_spec_t* family_spec,
    const iree_hal_streaming_execution_resource_set_t* set,
    uint32_t* out_minimum_partition_size, uint32_t* out_alignment) {
  if (set->resources.count == 0 || !set->resources.ordinals) return false;

  uint32_t alignment = 0;
  for (iree_host_size_t i = 0; i < set->resources.count; ++i) {
    const iree_hal_queue_execution_resource_ordinal_t resource_ordinal =
        set->resources.ordinals[i];
    if (resource_ordinal >= family_spec->execution_resource_count) return false;
    const uint32_t execution_unit_count =
        family_spec->execution_resources[resource_ordinal].execution_unit_count;
    if (execution_unit_count == 0) return false;
    alignment = alignment == 0 ? execution_unit_count
                               : iree_hip_execution_resource_gcd(
                                     alignment, execution_unit_count);
  }

  uint64_t minimum_partition_size = 0;
  iree_host_size_t required_resource_count = 0;
  for (iree_host_size_t group_ordinal = 0;
       group_ordinal < family_spec->execution_resource_group_count;
       ++group_ordinal) {
    const uint32_t minimum_selected_resource_count =
        family_spec->execution_resource_groups[group_ordinal]
            .minimum_selected_resource_count;
    uint32_t previous_execution_unit_count = 0;
    iree_hal_queue_execution_resource_ordinal_t previous_resource_ordinal = 0;
    bool has_previous_resource = false;
    for (uint32_t selection_index = 0;
         selection_index < minimum_selected_resource_count; ++selection_index) {
      uint32_t selected_execution_unit_count = UINT32_MAX;
      iree_hal_queue_execution_resource_ordinal_t selected_resource_ordinal =
          UINT32_MAX;
      for (iree_host_size_t i = 0; i < set->resources.count; ++i) {
        const iree_hal_queue_execution_resource_ordinal_t resource_ordinal =
            set->resources.ordinals[i];
        const iree_hal_queue_execution_resource_spec_t* resource_spec =
            &family_spec->execution_resources[resource_ordinal];
        if (resource_spec->group_ordinal != group_ordinal) continue;
        if (has_previous_resource &&
            (resource_spec->execution_unit_count <
                 previous_execution_unit_count ||
             (resource_spec->execution_unit_count ==
                  previous_execution_unit_count &&
              resource_ordinal <= previous_resource_ordinal))) {
          continue;
        }
        if (resource_spec->execution_unit_count <
                selected_execution_unit_count ||
            (resource_spec->execution_unit_count ==
                 selected_execution_unit_count &&
             resource_ordinal < selected_resource_ordinal)) {
          selected_execution_unit_count = resource_spec->execution_unit_count;
          selected_resource_ordinal = resource_ordinal;
        }
      }
      if (selected_resource_ordinal == UINT32_MAX) return false;
      minimum_partition_size += selected_execution_unit_count;
      ++required_resource_count;
      previous_execution_unit_count = selected_execution_unit_count;
      previous_resource_ordinal = selected_resource_ordinal;
      has_previous_resource = true;
    }
  }

  if (required_resource_count == 0) {
    uint32_t smallest_execution_unit_count = UINT32_MAX;
    for (iree_host_size_t i = 0; i < set->resources.count; ++i) {
      const iree_hal_queue_execution_resource_ordinal_t resource_ordinal =
          set->resources.ordinals[i];
      smallest_execution_unit_count =
          iree_min(smallest_execution_unit_count,
                   family_spec->execution_resources[resource_ordinal]
                       .execution_unit_count);
    }
    minimum_partition_size = smallest_execution_unit_count;
  }
  if (minimum_partition_size == 0 || minimum_partition_size > UINT32_MAX ||
      alignment == 0) {
    return false;
  }

  *out_minimum_partition_size = (uint32_t)minimum_partition_size;
  *out_alignment = alignment;
  return true;
}

static hipError_t iree_hip_execution_resource_decode_sm_token(
    const hipDevResource* resource,
    iree_hip_execution_resource_token_t* out_token) {
  if (!resource) return hipErrorInvalidValue;
  if (resource->type != hipDevResourceTypeSm) {
    return hipErrorInvalidResourceType;
  }

  iree_hip_execution_resource_token_t token = {0};
  memcpy(&token, resource->_internal_padding, sizeof(token));
  if (token.magic != IREE_HIP_EXECUTION_RESOURCE_TOKEN_MAGIC ||
      token.version != IREE_HIP_EXECUTION_RESOURCE_TOKEN_VERSION ||
      token.kind != IREE_HIP_EXECUTION_RESOURCE_TOKEN_KIND_SM ||
      (token.resource_flags & ~hipDevSmResourceGroupBackfill) ||
      token.set_id == IREE_HAL_STREAMING_EXECUTION_RESOURCE_SET_ID_INVALID) {
    return hipErrorInvalidResourceConfiguration;
  }
  *out_token = token;
  return hipSuccess;
}

iree_status_t iree_hip_execution_resource_create_sm(
    iree_hal_streaming_device_t* device,
    const iree_hal_queue_family_t* queue_family,
    iree_hal_queue_execution_resource_list_t resources, unsigned int flags,
    hipDevResource* out_resource) {
  IREE_ASSERT_ARGUMENT(device);
  IREE_ASSERT_ARGUMENT(queue_family);
  IREE_ASSERT_ARGUMENT(out_resource);

  if (IREE_UNLIKELY(flags & ~hipDevSmResourceGroupBackfill)) {
    return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                            "unsupported HIP SM resource flags 0x%08X", flags);
  }
  if (IREE_UNLIKELY(device->ordinal > UINT32_MAX)) {
    return iree_make_status(IREE_STATUS_OUT_OF_RANGE,
                            "HIP device ordinal exceeds token capacity");
  }

  const iree_hal_queue_family_spec_t* family_spec =
      iree_hal_queue_family_spec(queue_family);
  if (IREE_UNLIKELY(family_spec->execution_resource_count == 0)) {
    return iree_make_status(
        IREE_STATUS_UNIMPLEMENTED,
        "queue family exposes no selectable HIP execution resources");
  }

  iree_hal_streaming_execution_resource_set_id_t set_id =
      IREE_HAL_STREAMING_EXECUTION_RESOURCE_SET_ID_INVALID;
  IREE_RETURN_IF_ERROR(iree_hal_streaming_execution_resource_table_intern(
      &device->execution_resource_table, queue_family, resources, &set_id));
  const iree_hal_streaming_execution_resource_set_t* set =
      iree_hal_streaming_execution_resource_table_resolve(
          &device->execution_resource_table, set_id);
  if (IREE_UNLIKELY(!set)) {
    return iree_make_status(
        IREE_STATUS_INTERNAL,
        "newly interned HIP execution-resource set cannot be resolved");
  }

  uint32_t minimum_partition_size = 0;
  uint32_t alignment = 0;
  if (IREE_UNLIKELY(!iree_hip_execution_resource_measure_sm_constraints(
          family_spec, set, &minimum_partition_size, &alignment))) {
    return iree_make_status(
        IREE_STATUS_INTERNAL,
        "interned execution-resource set has inconsistent family constraints");
  }

  const iree_hip_execution_resource_token_t token = {
      .magic = IREE_HIP_EXECUTION_RESOURCE_TOKEN_MAGIC,
      .version = IREE_HIP_EXECUTION_RESOURCE_TOKEN_VERSION,
      .kind = IREE_HIP_EXECUTION_RESOURCE_TOKEN_KIND_SM,
      .device_ordinal = (uint32_t)device->ordinal,
      .resource_flags = flags,
      .table_generation =
          iree_hal_streaming_execution_resource_table_generation(
              &device->execution_resource_table),
      .set_id = set_id,
  };
  hipDevResource resource = {0};
  resource.type = hipDevResourceTypeSm;
  memcpy(resource._internal_padding, &token, sizeof(token));
  resource.sm.smCount = set->execution_unit_count;
  resource.sm.minSmPartitionSize = minimum_partition_size;
  resource.sm.smCoscheduledAlignment = alignment;
  resource.sm.flags = flags;
  resource.nextResource = NULL;
  *out_resource = resource;
  return iree_ok_status();
}

hipError_t iree_hip_execution_resource_resolve_sm_for_device(
    const hipDevResource* resource, iree_hal_streaming_device_t* device,
    const iree_hal_streaming_execution_resource_set_t** out_set) {
  IREE_ASSERT_ARGUMENT(device);
  IREE_ASSERT_ARGUMENT(out_set);

  iree_hip_execution_resource_token_t token = {0};
  hipError_t result =
      iree_hip_execution_resource_decode_sm_token(resource, &token);
  if (result != hipSuccess) return result;
  if (token.device_ordinal != device->ordinal ||
      token.table_generation !=
          iree_hal_streaming_execution_resource_table_generation(
              &device->execution_resource_table)) {
    return hipErrorInvalidResourceConfiguration;
  }

  const iree_hal_streaming_execution_resource_set_t* set =
      iree_hal_streaming_execution_resource_table_resolve(
          &device->execution_resource_table, token.set_id);
  if (!set) return hipErrorInvalidResourceConfiguration;
  const iree_hal_queue_family_t* queue_family = iree_hal_device_queue_family(
      device->hal_device, set->queue_family_ordinal);
  if (!queue_family) return hipErrorInvalidResourceConfiguration;

  uint32_t minimum_partition_size = 0;
  uint32_t alignment = 0;
  if (!iree_hip_execution_resource_measure_sm_constraints(
          iree_hal_queue_family_spec(queue_family), set,
          &minimum_partition_size, &alignment) ||
      resource->sm.smCount != set->execution_unit_count ||
      resource->sm.minSmPartitionSize != minimum_partition_size ||
      resource->sm.smCoscheduledAlignment != alignment ||
      resource->sm.flags != token.resource_flags) {
    return hipErrorInvalidResourceConfiguration;
  }

  *out_set = set;
  return hipSuccess;
}

hipError_t iree_hip_execution_resource_resolve_sm(
    const hipDevResource* resource, iree_hal_streaming_device_t** out_device,
    const iree_hal_streaming_execution_resource_set_t** out_set) {
  IREE_ASSERT_ARGUMENT(out_device);
  IREE_ASSERT_ARGUMENT(out_set);

  iree_hip_execution_resource_token_t token = {0};
  hipError_t result =
      iree_hip_execution_resource_decode_sm_token(resource, &token);
  if (result != hipSuccess) return result;
  iree_hal_streaming_device_t* device =
      iree_hal_streaming_device_entry(token.device_ordinal);
  if (!device) return hipErrorInvalidResourceConfiguration;

  const iree_hal_streaming_execution_resource_set_t* set = NULL;
  result =
      iree_hip_execution_resource_resolve_sm_for_device(resource, device, &set);
  if (result != hipSuccess) return result;
  *out_device = device;
  *out_set = set;
  return hipSuccess;
}
