// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/platform/endpoint.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "libamdf/src/platform/windows/endpoint_properties.h"
#include "libamdf/src/platform/windows/instance.h"

struct amdf_platform_endpoint_t {
  // Platform instance borrowed by the retained adapter handle.
  amdf_platform_instance_t* instance;
  // Query-only KMT adapter handle owned by this endpoint.
  D3DKMT_HANDLE adapter;
};

static amdf_status_t amdf_windows_close_endpoint_adapter(
    amdf_platform_endpoint_t* endpoint) {
  D3DKMT_CLOSEADAPTER close_adapter = {0};
  close_adapter.hAdapter = endpoint->adapter;
  return amdf_kmt_make_status(
      endpoint->instance->kmt.close_adapter(&close_adapter));
}

amdf_status_t amdf_platform_endpoint_open(
    amdf_platform_instance_t* instance, const amdf_endpoint_id_t* id,
    amdf_platform_endpoint_t** out_endpoint, amdf_endpoint_info_t* out_info) {
  *out_endpoint = NULL;
  amdf_platform_endpoint_t* endpoint =
      (amdf_platform_endpoint_t*)calloc(1, sizeof(*endpoint));
  if (endpoint == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_RESOURCE_EXHAUSTED);
  }
  endpoint->instance = instance;

  LUID adapter_luid;
  uint32_t physical_adapter_index = 0;
  amdf_windows_endpoint_id_decode(id, &adapter_luid, &physical_adapter_index);
  D3DKMT_OPENADAPTERFROMLUID open_adapter = {0};
  open_adapter.AdapterLuid = adapter_luid;
  amdf_status_t status =
      amdf_kmt_make_status(instance->kmt.open_adapter_from_luid(&open_adapter));
  endpoint->adapter = open_adapter.hAdapter;
  if (amdf_status_is_ok(status) && endpoint->adapter == 0) {
    status = amdf_make_api_status(AMDF_STATUS_CODE_INTERNAL);
  }

  uint32_t physical_adapter_count = 0;
  if (amdf_status_is_ok(status)) {
    status = amdf_windows_query_physical_adapter_count(
        &instance->kmt, endpoint->adapter, &physical_adapter_count);
  }
  if (amdf_status_is_ok(status) &&
      physical_adapter_index >= physical_adapter_count) {
    status = amdf_make_api_status(AMDF_STATUS_CODE_NOT_FOUND);
  }

  amdf_endpoint_info_t endpoint_info = {0};
  if (amdf_status_is_ok(status)) {
    status = amdf_windows_query_endpoint_info(
        &instance->kmt, endpoint->adapter, adapter_luid, physical_adapter_index,
        &endpoint_info);
  }
  if (amdf_status_is_ok(status) &&
      (!amdf_windows_endpoint_info_is_amd(&endpoint_info) ||
       !amdf_endpoint_id_is_equal(id, &endpoint_info.id))) {
    status = amdf_make_api_status(AMDF_STATUS_CODE_NOT_FOUND);
  }

  if (amdf_status_is_ok(status)) {
    *out_info = endpoint_info;
    *out_endpoint = endpoint;
  } else {
    if (endpoint->adapter != 0) {
      const amdf_status_t close_status =
          amdf_windows_close_endpoint_adapter(endpoint);
      if (!amdf_status_is_ok(close_status)) {
        status = close_status;
      }
    }
    free(endpoint);
  }
  return status;
}

amdf_status_t amdf_platform_endpoint_close(amdf_platform_endpoint_t* endpoint) {
  const amdf_status_t status = amdf_windows_close_endpoint_adapter(endpoint);
  if (amdf_status_is_ok(status)) {
    free(endpoint);
  }
  return status;
}
