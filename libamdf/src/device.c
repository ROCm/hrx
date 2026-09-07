// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/device.h"

#include <stddef.h>
#include <stdlib.h>

#include "libamdf/src/endpoint.h"

amdf_status_t amdf_device_initialize(amdf_device_t* device,
                                     const amdf_device_vtable_t* vtable,
                                     amdf_endpoint_t* endpoint,
                                     amdf_engine_kind_t engine_kind) {
  const amdf_status_t status = amdf_endpoint_register_device(endpoint);
  if (!amdf_status_is_ok(status)) {
    return status;
  }
  device->vtable = vtable;
  device->endpoint = endpoint;
  device->engine_kind = engine_kind;
  amdf_child_tracker_initialize(&device->children);
  return AMDF_STATUS_OK;
}

void amdf_device_deinitialize(amdf_device_t* device) {
  amdf_endpoint_unregister_device(device->endpoint);
  device->endpoint = NULL;
}

bool amdf_device_is_engine(const amdf_device_t* device,
                           amdf_engine_kind_t expected_engine_kind) {
  return device != NULL && device->engine_kind == expected_engine_kind;
}

amdf_status_t AMDF_CALL amdf_device_destroy(amdf_device_t* device) {
  if (device == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  if (amdf_child_tracker_count(&device->children) != 0) {
    return amdf_make_api_status(AMDF_STATUS_CODE_BUSY);
  }
  const amdf_status_t status = device->vtable->destroy_native(device);
  if (amdf_status_is_ok(status)) {
    amdf_device_deinitialize(device);
    free(device);
  }
  return status;
}
