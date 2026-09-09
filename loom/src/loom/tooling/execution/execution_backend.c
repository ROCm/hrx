// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/tooling/execution/execution_backend.h"

void loom_run_execution_backend_registry_initialize_from_entries(
    const loom_run_execution_backend_t* const* backends,
    iree_host_size_t backend_count,
    loom_run_execution_backend_registry_t* out_registry) {
  *out_registry = (loom_run_execution_backend_registry_t){
      .backends = backends,
      .backend_count = backend_count,
  };
}

const loom_run_execution_backend_t*
loom_run_execution_backend_registry_lookup_device_driver(
    const loom_run_execution_backend_registry_t* registry,
    iree_string_view_t device_driver_name) {
  if (registry == NULL) {
    return NULL;
  }
  for (iree_host_size_t i = 0; i < registry->backend_count; ++i) {
    const loom_run_execution_backend_t* backend = registry->backends[i];
    if (backend && iree_string_view_equal(backend->device_driver_name,
                                          device_driver_name)) {
      return backend;
    }
  }
  return NULL;
}

iree_status_t loom_run_execution_select_device_driver(
    iree_string_view_list_t device_uris, iree_string_view_t* out_device_uri,
    iree_string_view_t* out_device_driver_name) {
  *out_device_uri = iree_string_view_empty();
  *out_device_driver_name = iree_string_view_empty();
  if (device_uris.count != 1) {
    return iree_make_status(
        IREE_STATUS_INVALID_ARGUMENT,
        "Loom HAL execution requires exactly one --device= URI; got %" PRIhsz,
        device_uris.count);
  }

  const iree_string_view_t device_uri = device_uris.values[0];
  iree_string_view_t device_driver_name = iree_string_view_empty();
  iree_string_view_split(device_uri, ':', &device_driver_name, NULL);
  if (iree_string_view_is_empty(device_driver_name)) {
    return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                            "--device=%.*s has no HAL driver name",
                            (int)device_uri.size, device_uri.data);
  }

  *out_device_uri = device_uri;
  *out_device_driver_name = device_driver_name;
  return iree_ok_status();
}
