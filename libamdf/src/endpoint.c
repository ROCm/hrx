// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/endpoint.h"

#include <stddef.h>
#include <stdlib.h>

#include "libamdf/src/child_tracker.h"
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
  // Immutable endpoint-local native queue families.
  struct {
    // Records owned elsewhere and valid for the lifetime of this endpoint.
    const amdf_queue_family_info_t* values;
    // Number of records in `values`.
    uint32_t count;
  } queue_families;
  // Number of materialized devices borrowing this endpoint.
  amdf_child_tracker_t children;
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
  amdf_child_tracker_initialize(&endpoint->children);
  amdf_status_t status = amdf_instance_register_endpoint(instance);
  if (amdf_status_is_ok(status)) {
    endpoint->instance = instance;
    endpoint->info.type = AMDF_STRUCTURE_TYPE_ENDPOINT_INFO;
    endpoint->info.structure_size = sizeof(endpoint->info);
    status = amdf_platform_endpoint_open(amdf_instance_platform(instance), id,
                                         &endpoint->platform, &endpoint->info);
  }
  if (amdf_status_is_ok(status)) {
    endpoint->info.queue_family_count = endpoint->queue_families.count;
    *out_endpoint = endpoint;
  } else {
    if (endpoint->instance != NULL) {
      amdf_instance_unregister_endpoint(endpoint->instance);
    }
    free(endpoint);
  }
  return status;
}

const amdf_endpoint_info_t* amdf_endpoint_get_cached_info(
    const amdf_endpoint_t* endpoint) {
  return &endpoint->info;
}

amdf_platform_endpoint_t* amdf_endpoint_get_platform(
    amdf_endpoint_t* endpoint) {
  return endpoint->platform;
}

amdf_status_t amdf_endpoint_register_device(amdf_endpoint_t* endpoint) {
  return amdf_child_tracker_register(&endpoint->children);
}

void amdf_endpoint_unregister_device(amdf_endpoint_t* endpoint) {
  amdf_child_tracker_unregister(&endpoint->children);
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

amdf_status_t AMDF_CALL amdf_endpoint_query_queue_family_info(
    amdf_endpoint_t* endpoint, uint32_t queue_family_ordinal,
    amdf_queue_family_info_t* out_info) {
  if (endpoint == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  const amdf_status_t status = amdf_structure_validate_output(
      out_info, AMDF_STRUCTURE_TYPE_QUEUE_FAMILY_INFO,
      (uint32_t)sizeof(amdf_queue_family_info_t));
  if (!amdf_status_is_ok(status)) {
    return status;
  }
  if (queue_family_ordinal >= endpoint->queue_families.count) {
    return amdf_make_api_status(AMDF_STATUS_CODE_OUT_OF_RANGE);
  }
  if (endpoint->queue_families.values == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INTERNAL);
  }

  const amdf_queue_family_info_t* source_info =
      &endpoint->queue_families.values[queue_family_ordinal];
  const uint32_t structure_size = out_info->structure_size;
  void* const next = out_info->next;
  *out_info = *source_info;
  out_info->structure_size = structure_size;
  out_info->next = next;
  return AMDF_STATUS_OK;
}

amdf_status_t AMDF_CALL amdf_endpoint_close(amdf_endpoint_t* endpoint) {
  if (endpoint == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  if (amdf_child_tracker_count(&endpoint->children) != 0) {
    return amdf_make_api_status(AMDF_STATUS_CODE_BUSY);
  }
  const amdf_status_t status = amdf_platform_endpoint_close(endpoint->platform);
  if (amdf_status_is_ok(status)) {
    amdf_instance_unregister_endpoint(endpoint->instance);
    free(endpoint);
  }
  return status;
}
