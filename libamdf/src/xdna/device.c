// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/xdna/device.h"

#include <stddef.h>
#include <stdlib.h>

#include "libamdf/src/device.h"
#include "libamdf/src/endpoint.h"
#include "libamdf/src/structure.h"
#include "libamdf/src/xdna/endpoint_profile.h"
#include "libamdf/src/xdna/umd/device.h"

typedef struct amdf_xdna_device_t {
  // Generic device state shared by every engine implementation.
  amdf_device_t base;
  // Exact native context and address-domain state.
  amdf_xdna_umd_device_t* umd;
  // Immutable identity and achieved placement returned by the provider.
  amdf_xdna_device_info_t info;
} amdf_xdna_device_t;

_Static_assert(offsetof(amdf_xdna_device_t, base) == 0,
               "XDNA device base must be the first field");

static amdf_status_t amdf_xdna_device_validate_create_info(
    const amdf_xdna_endpoint_profile_t* profile,
    const amdf_xdna_device_create_info_t* create_info) {
  const amdf_status_t status = amdf_structure_validate_input(
      create_info, AMDF_STRUCTURE_TYPE_XDNA_DEVICE_CREATE_INFO,
      (uint32_t)sizeof(amdf_xdna_device_create_info_t));
  if (!amdf_status_is_ok(status)) {
    return status;
  }

  const amdf_xdna_endpoint_info_t* endpoint_info =
      amdf_xdna_endpoint_profile_get_info(profile);
  const amdf_xdna_scheduling_modes_t known_scheduling_modes =
      AMDF_XDNA_SCHEDULING_MODE_EXCLUSIVE | AMDF_XDNA_SCHEDULING_MODE_SPATIAL |
      AMDF_XDNA_SCHEDULING_MODE_TIME_SLICED;
  if (create_info->acceptable_scheduling_modes == 0 ||
      (create_info->acceptable_scheduling_modes & ~known_scheduling_modes) !=
          0) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  if ((create_info->acceptable_scheduling_modes &
       endpoint_info->context.scheduling_modes) == 0) {
    return amdf_make_api_status(AMDF_STATUS_CODE_UNSUPPORTED);
  }
  if (create_info->logical_column_count <
          endpoint_info->context.minimum_column_count ||
      create_info->logical_column_count >
          endpoint_info->context.maximum_column_count) {
    return amdf_make_api_status(AMDF_STATUS_CODE_OUT_OF_RANGE);
  }
  const uint32_t count_offset = create_info->logical_column_count -
                                endpoint_info->context.minimum_column_count;
  if (count_offset % endpoint_info->context.column_count_granularity != 0) {
    return amdf_make_api_status(AMDF_STATUS_CODE_OUT_OF_RANGE);
  }
  return AMDF_STATUS_OK;
}

static amdf_status_t amdf_xdna_device_destroy_native(
    amdf_device_t* base_device) {
  amdf_xdna_device_t* device = (amdf_xdna_device_t*)base_device;
  const amdf_status_t status = amdf_xdna_umd_device_destroy(device->umd);
  if (amdf_status_is_ok(status)) {
    device->umd = NULL;
  }
  return status;
}

static const amdf_device_vtable_t amdf_xdna_device_vtable = {
    .destroy_native = amdf_xdna_device_destroy_native,
};

amdf_status_t AMDF_CALL
amdf_xdna_device_create(amdf_endpoint_t* endpoint,
                        const amdf_xdna_device_create_info_t* create_info,
                        amdf_device_t** out_device) {
  if (out_device == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  *out_device = NULL;
  if (endpoint == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }

  const amdf_xdna_endpoint_profile_t* profile =
      amdf_xdna_endpoint_profile_select(
          amdf_endpoint_get_cached_info(endpoint));
  if (profile == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_UNSUPPORTED);
  }
  amdf_status_t status =
      amdf_xdna_device_validate_create_info(profile, create_info);
  if (!amdf_status_is_ok(status)) {
    return status;
  }

  amdf_xdna_device_t* device = (amdf_xdna_device_t*)calloc(1, sizeof(*device));
  if (device == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_RESOURCE_EXHAUSTED);
  }
  status = amdf_device_initialize(&device->base, &amdf_xdna_device_vtable,
                                  endpoint, AMDF_ENGINE_KIND_XDNA);

  amdf_xdna_umd_device_result_t result = {0};
  if (amdf_status_is_ok(status)) {
    status = amdf_xdna_umd_device_create(amdf_endpoint_get_platform(endpoint),
                                         profile, create_info, &device->umd,
                                         &result);
  }
  if (amdf_status_is_ok(status)) {
    const amdf_xdna_endpoint_info_t* endpoint_info =
        amdf_xdna_endpoint_profile_get_info(profile);
    device->info.type = AMDF_STRUCTURE_TYPE_XDNA_DEVICE_INFO;
    device->info.structure_size = sizeof(device->info);
    device->info.id = result.id;
    device->info.reset_epoch = result.reset_epoch;
    device->info.scheduling_mode = result.scheduling_mode;
    device->info.placement_generation = result.placement_generation;
    device->info.columns.logical_count = create_info->logical_column_count;
    device->info.columns.physical_origin = result.physical_column_origin;
    device->info.columns.physical_count = result.physical_column_count;
    device->info.row_count = endpoint_info->array.row_count;
    *out_device = &device->base;
  } else {
    if (device->base.endpoint != NULL) {
      amdf_device_deinitialize(&device->base);
    }
    free(device);
  }
  return status;
}

amdf_status_t AMDF_CALL amdf_xdna_device_query_info(
    amdf_device_t* device, amdf_xdna_device_info_t* out_info) {
  if (!amdf_device_is_engine(device, AMDF_ENGINE_KIND_XDNA)) {
    return device == NULL
               ? amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT)
               : amdf_make_api_status(AMDF_STATUS_CODE_UNSUPPORTED);
  }
  const amdf_status_t status = amdf_structure_validate_output(
      out_info, AMDF_STRUCTURE_TYPE_XDNA_DEVICE_INFO,
      (uint32_t)sizeof(amdf_xdna_device_info_t));
  if (!amdf_status_is_ok(status)) {
    return status;
  }

  const amdf_xdna_device_t* xdna_device = (const amdf_xdna_device_t*)device;
  const uint32_t structure_size = out_info->structure_size;
  void* const next = out_info->next;
  *out_info = xdna_device->info;
  out_info->structure_size = structure_size;
  out_info->next = next;
  return AMDF_STATUS_OK;
}
