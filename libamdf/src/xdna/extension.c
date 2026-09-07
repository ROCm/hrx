// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/xdna/extension.h"

#include <stddef.h>

#include "amdf/xdna.h"
#include "libamdf/src/endpoint.h"
#include "libamdf/src/structure.h"
#include "libamdf/src/xdna/device.h"
#include "libamdf/src/xdna/endpoint_profile.h"

static amdf_status_t AMDF_CALL amdf_xdna_endpoint_query_info(
    amdf_endpoint_t* endpoint, amdf_xdna_endpoint_info_t* out_info) {
  if (endpoint == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  const amdf_status_t status = amdf_structure_validate_output(
      out_info, AMDF_STRUCTURE_TYPE_XDNA_ENDPOINT_INFO,
      (uint32_t)sizeof(amdf_xdna_endpoint_info_t));
  if (!amdf_status_is_ok(status)) {
    return status;
  }

  const amdf_xdna_endpoint_profile_t* profile =
      amdf_xdna_endpoint_profile_select(
          amdf_endpoint_get_cached_info(endpoint));
  if (profile == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_UNSUPPORTED);
  }

  const uint32_t structure_size = out_info->structure_size;
  void* const next = out_info->next;
  *out_info = *amdf_xdna_endpoint_profile_get_info(profile);
  out_info->structure_size = structure_size;
  out_info->next = next;
  return AMDF_STATUS_OK;
}

static const amdf_xdna_api_t amdf_xdna_api_v1 = {
    .structure_size = sizeof(amdf_xdna_api_t),
    .extension_version = AMDF_XDNA_EXTENSION_VERSION_1,
    .endpoint_query_info = amdf_xdna_endpoint_query_info,
    .device_create = amdf_xdna_device_create,
    .device_query_info = amdf_xdna_device_query_info,
};

amdf_status_t amdf_xdna_extension_query(uint32_t minimum_version,
                                        uint32_t maximum_version,
                                        const void** out_extension_api) {
  if (minimum_version > AMDF_XDNA_EXTENSION_VERSION_1 ||
      maximum_version < AMDF_XDNA_EXTENSION_VERSION_1) {
    return amdf_make_api_status(AMDF_STATUS_CODE_VERSION_MISMATCH);
  }
  *out_extension_api = &amdf_xdna_api_v1;
  return AMDF_STATUS_OK;
}
