// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/endpoint.h"

#include <stddef.h>
#include <stdlib.h>

#include "libamdf/src/instance.h"
#include "libamdf/src/platform/endpoint.h"
#include "libamdf/src/structure.h"

struct amdf_endpoint_t {
  // Instance borrowed for the lifetime of this endpoint.
  amdf_instance_t* instance;
  // Platform query handle owned by this endpoint.
  amdf_platform_endpoint_t* platform;
  // Immutable properties cached while opening the platform endpoint.
  amdf_endpoint_info_t info;
};

amdf_status_t AMDF_CALL amdf_endpoint_open(amdf_instance_t* instance,
                                           const amdf_endpoint_id_t* id,
                                           amdf_endpoint_t** out_endpoint) {
  if (out_endpoint == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  *out_endpoint = NULL;
  if (instance == NULL || id == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }

  amdf_endpoint_t* endpoint = (amdf_endpoint_t*)calloc(1, sizeof(*endpoint));
  if (endpoint == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_RESOURCE_EXHAUSTED);
  }
  amdf_status_t status = amdf_instance_register_endpoint(instance);
  if (amdf_status_is_ok(status)) {
    endpoint->instance = instance;
    endpoint->info.type = AMDF_STRUCTURE_TYPE_ENDPOINT_INFO;
    endpoint->info.structure_size = sizeof(endpoint->info);
    status = amdf_platform_endpoint_open(amdf_instance_platform(instance), id,
                                         &endpoint->platform, &endpoint->info);
  }
  if (amdf_status_is_ok(status)) {
    *out_endpoint = endpoint;
  } else {
    if (endpoint->instance != NULL) {
      amdf_instance_unregister_endpoint(endpoint->instance);
    }
    free(endpoint);
  }
  return status;
}

amdf_status_t AMDF_CALL amdf_endpoint_query_info(
    amdf_endpoint_t* endpoint, amdf_endpoint_info_t* out_info) {
  if (endpoint == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  const amdf_status_t status = amdf_structure_validate_output(
      out_info, AMDF_STRUCTURE_TYPE_ENDPOINT_INFO,
      (uint32_t)sizeof(amdf_endpoint_info_t));
  if (!amdf_status_is_ok(status)) {
    return status;
  }

  const uint32_t structure_size = out_info->structure_size;
  void* const next = out_info->next;
  *out_info = endpoint->info;
  out_info->structure_size = structure_size;
  out_info->next = next;
  return AMDF_STATUS_OK;
}

amdf_status_t AMDF_CALL amdf_endpoint_close(amdf_endpoint_t* endpoint) {
  if (endpoint == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  const amdf_status_t status = amdf_platform_endpoint_close(endpoint->platform);
  if (amdf_status_is_ok(status)) {
    amdf_instance_unregister_endpoint(endpoint->instance);
    free(endpoint);
  }
  return status;
}
