// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef AMDF_SRC_MEMORY_H_
#define AMDF_SRC_MEMORY_H_

#include "amdf/amdf.h"
#include "libamdf/src/child_tracker.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

typedef struct amdf_memory_vtable_t {
  // Creates one explicit host mapping.
  amdf_status_t (*map)(amdf_memory_t* memory,
                       const amdf_memory_map_info_t* map_info,
                       amdf_host_mapping_t** out_mapping);
  // Releases the exact native state owned by a memory implementation.
  amdf_status_t (*destroy_native)(amdf_memory_t* memory);
} amdf_memory_vtable_t;

struct amdf_memory_t {
  // Implementation operations selected before the memory is published.
  const amdf_memory_vtable_t* vtable;
  // Device borrowed for the lifetime of this memory attachment.
  amdf_device_t* device;
  // Immutable properties established before publication.
  amdf_memory_info_t info;
  // Number of live mappings and commands borrowing this memory.
  amdf_child_tracker_t children;
};

// Initializes an unpublished memory base and borrows its device.
amdf_status_t amdf_memory_initialize(amdf_memory_t* memory,
                                     const amdf_memory_vtable_t* vtable,
                                     amdf_device_t* device);

// Releases the device borrow held by unpublished or torn-down memory.
void amdf_memory_deinitialize(amdf_memory_t* memory);

// Registers one child that borrows `memory`.
amdf_status_t amdf_memory_register_child(amdf_memory_t* memory);

// Releases one child borrow.
void amdf_memory_unregister_child(amdf_memory_t* memory);

// Creates memory attached to `device`.
amdf_status_t AMDF_CALL amdf_memory_create(
    amdf_device_t* device, const amdf_memory_create_info_t* create_info,
    amdf_memory_t** out_memory);

// Copies immutable memory properties.
amdf_status_t AMDF_CALL amdf_memory_query_info(amdf_memory_t* memory,
                                               amdf_memory_info_t* out_info);

// Creates an explicit host mapping of one memory range.
amdf_status_t AMDF_CALL amdf_memory_map(amdf_memory_t* memory,
                                        const amdf_memory_map_info_t* map_info,
                                        amdf_host_mapping_t** out_mapping);

// Destroys memory with no remaining children.
amdf_status_t AMDF_CALL amdf_memory_destroy(amdf_memory_t* memory);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // AMDF_SRC_MEMORY_H_
