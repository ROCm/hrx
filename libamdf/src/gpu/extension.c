// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/gpu/extension.h"

#include <stddef.h>

#include "amdf/gpu.h"
#include "libamdf/src/gpu/device.h"
#include "libamdf/src/gpu/endpoint_profile.h"
#include "libamdf/src/gpu/kernel_queue.h"
#include "libamdf/src/gpu/umd/endpoint_profile.h"
#include "libamdf/src/structure.h"

static amdf_status_t AMDF_CALL amdf_gpu_endpoint_query_info(
    amdf_endpoint_t* endpoint, amdf_gpu_endpoint_info_t* out_info) {
  if (endpoint == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  const amdf_status_t validation_status = amdf_structure_validate_output(
      out_info, AMDF_STRUCTURE_TYPE_GPU_ENDPOINT_INFO,
      (uint32_t)sizeof(amdf_gpu_endpoint_info_t));
  if (!amdf_status_is_ok(validation_status)) {
    return validation_status;
  }

  const void* untyped_profile = NULL;
  const amdf_status_t profile_status = amdf_endpoint_query_engine_profile(
      endpoint, AMDF_ENGINE_KIND_GPU, &untyped_profile);
  if (!amdf_status_is_ok(profile_status)) {
    return profile_status;
  }
  const amdf_gpu_endpoint_profile_t* profile =
      (const amdf_gpu_endpoint_profile_t*)untyped_profile;

  const uint32_t structure_size = out_info->structure_size;
  void* const next = out_info->next;
  *out_info = *amdf_gpu_endpoint_profile_get_info(profile);
  out_info->structure_size = structure_size;
  out_info->next = next;
  return AMDF_STATUS_OK;
}

static const amdf_gpu_api_t amdf_gpu_api_v1 = {
    .structure_size = sizeof(amdf_gpu_api_t),
    .extension_version = AMDF_GPU_EXTENSION_VERSION_1,
    .endpoint_query_info = amdf_gpu_endpoint_query_info,
    .device_create = amdf_gpu_device_create,
    .device_query_info = amdf_gpu_device_query_info,
    .kernel_queue_create = amdf_gpu_kernel_queue_create,
    .kernel_queue_submit = amdf_gpu_kernel_queue_submit,
};

void amdf_gpu_extension_initialize_endpoint(amdf_endpoint_t* endpoint) {
  const amdf_endpoint_info_t* endpoint_info =
      amdf_endpoint_get_cached_info(endpoint);
  if (endpoint_info->engine_kind != AMDF_ENGINE_KIND_GPU) {
    return;
  }

  amdf_gpu_endpoint_profile_t profile = {0};
  bool profile_available = false;
  const amdf_status_t status = amdf_gpu_umd_query_endpoint_profile(
      amdf_endpoint_get_platform(endpoint), &profile, &profile_available);
  if (!amdf_status_is_ok(status)) {
    amdf_endpoint_store_engine_profile_error(endpoint, status);
  } else if (!profile_available) {
    amdf_endpoint_store_engine_profile_error(
        endpoint, amdf_make_api_status(AMDF_STATUS_CODE_UNSUPPORTED));
  } else {
    amdf_endpoint_store_engine_profile(endpoint, &profile, sizeof(profile));
  }
}

uint32_t amdf_gpu_extension_query_endpoint_queue_families(
    const amdf_gpu_endpoint_profile_t* profile,
    const amdf_platform_endpoint_t* platform_endpoint, uint32_t capacity,
    amdf_queue_family_info_t* out_families) {
  if (profile == NULL || capacity == 0) {
    return 0;
  }

  struct {
    // Native command representation accepted by this candidate family.
    amdf_queue_command_type_t command_type;
    // Whether the qualified endpoint can construct this native queue.
    bool supported;
  } candidates[] = {
      {AMDF_QUEUE_COMMAND_TYPE_GPU_PM4, profile->supports_pm4_kernel_queue},
      {AMDF_QUEUE_COMMAND_TYPE_GPU_SDMA, profile->supports_sdma_kernel_queue},
  };
  uint32_t family_count = 0;
  for (uint32_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]) &&
                       family_count < capacity;
       ++i) {
    if (!candidates[i].supported) {
      continue;
    }
    const amdf_queue_publication_modes_t publication_modes =
        amdf_platform_endpoint_query_queue_publication_modes(
            platform_endpoint, candidates[i].command_type);
    if (publication_modes == 0) {
      continue;
    }
    amdf_queue_family_info_t* family = &out_families[family_count];
    *family = (amdf_queue_family_info_t){0};
    family->type = AMDF_STRUCTURE_TYPE_QUEUE_FAMILY_INFO;
    family->structure_size = sizeof(*family);
    family->ordinal = family_count;
    family->command_type = candidates[i].command_type;
    family->publication_modes = publication_modes;
    ++family_count;
  }
  return family_count;
}

amdf_status_t amdf_gpu_extension_query(uint32_t minimum_version,
                                       uint32_t maximum_version,
                                       const void** out_extension_api) {
  if (minimum_version > AMDF_GPU_EXTENSION_VERSION_1 ||
      maximum_version < AMDF_GPU_EXTENSION_VERSION_1) {
    return amdf_make_api_status(AMDF_STATUS_CODE_VERSION_MISMATCH);
  }
  *out_extension_api = &amdf_gpu_api_v1;
  return AMDF_STATUS_OK;
}
