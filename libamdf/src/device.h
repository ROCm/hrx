// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef AMDF_SRC_DEVICE_H_
#define AMDF_SRC_DEVICE_H_

#include "amdf/amdf.h"
#include "libamdf/src/child_tracker.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

typedef struct amdf_device_vtable_t {
  // Releases the exact native state owned by a device implementation.
  amdf_status_t (*destroy_native)(amdf_device_t* device);
} amdf_device_vtable_t;

struct amdf_device_t {
  // Implementation operations selected before the device is published.
  const amdf_device_vtable_t* vtable;
  // Endpoint borrowed for the lifetime of this device.
  amdf_endpoint_t* endpoint;
  // Exact engine family implementing this device.
  amdf_engine_kind_t engine_kind;
  // Number of live children borrowing this device.
  amdf_child_tracker_t children;
};

// Initializes an unpublished device base and borrows its endpoint.
amdf_status_t amdf_device_initialize(amdf_device_t* device,
                                     const amdf_device_vtable_t* vtable,
                                     amdf_endpoint_t* endpoint,
                                     amdf_engine_kind_t engine_kind);

// Releases the endpoint borrow held by an unpublished or torn-down device.
void amdf_device_deinitialize(amdf_device_t* device);

// Returns true when a device is implemented by `expected_engine_kind`.
bool amdf_device_is_engine(const amdf_device_t* device,
                           amdf_engine_kind_t expected_engine_kind);

// Destroys a device with no remaining children.
amdf_status_t AMDF_CALL amdf_device_destroy(amdf_device_t* device);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // AMDF_SRC_DEVICE_H_
