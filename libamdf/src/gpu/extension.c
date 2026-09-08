// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/gpu/extension.h"

#include <stddef.h>

#include "amdf/gpu.h"
#include "libamdf/src/gpu/endpoint_profile.h"
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
    const amdf_endpoint_info_t* endpoint_info, uint32_t capacity,
    amdf_queue_family_info_t* out_families) {
  (void)endpoint_info;
  (void)capacity;
  (void)out_families;
  return 0;
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
