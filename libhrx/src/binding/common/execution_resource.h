// Copyright 2026 The HRX Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef LIBHRX_SRC_BINDING_COMMON_EXECUTION_RESOURCE_H_
#define LIBHRX_SRC_BINDING_COMMON_EXECUTION_RESOURCE_H_

#include "iree/base/api.h"
#include "iree/base/threading/mutex.h"
#include "iree/hal/api.h"

#ifdef __cplusplus
extern "C" {
#endif

// Stable identity of an immutable execution-resource set within one table.
// Zero never identifies a set.
typedef uint64_t iree_hal_streaming_execution_resource_set_id_t;
#define IREE_HAL_STREAMING_EXECUTION_RESOURCE_SET_ID_INVALID 0ull

// Canonical immutable execution-resource set interned for a device.
//
// The record and its ordinal storage remain valid until the containing table is
// deinitialized. It borrows the device specification through the table and
// contains no HAL object references.
typedef struct iree_hal_streaming_execution_resource_set_t {
  // Canonical queue-family ordinal within the owning device.
  iree_hal_queue_family_ordinal_t queue_family_ordinal;

  // Number of raw execution units covered by |resources|.
  uint32_t execution_unit_count;

  // Explicit sorted unique execution-resource ordinals. A full-family set is
  // expanded here instead of retaining the HAL empty-list shorthand.
  iree_hal_queue_execution_resource_list_t resources;
} iree_hal_streaming_execution_resource_set_t;

// Device-owned immutable execution-resource set table.
//
// Interning and resolution are thread-safe. Initialization and deinitialization
// are exclusive to the containing device. Resolved records remain valid without
// retaining the table because entries are never removed before device teardown.
typedef struct iree_hal_streaming_execution_resource_table_t {
  // Serializes entry interning and pointer-array access.
  iree_slim_mutex_t mutex;

  // Host allocator used for entries and pointer-array storage.
  iree_allocator_t host_allocator;

  // Borrowed HAL device whose immutable queue-family facts define all entries.
  iree_hal_device_t* device;

  // Process-unique nonzero device-table incarnation.
  uint64_t incarnation;

  // Number of immutable entries in |entries|.
  iree_host_size_t entry_count;

  // Number of allocated pointer slots in |entries|.
  iree_host_size_t entry_capacity;

  // Immutable entries indexed by set identity minus one.
  iree_hal_streaming_execution_resource_set_t** entries;
} iree_hal_streaming_execution_resource_table_t;

// Initializes an empty table borrowing |device| for its complete lifetime.
// The device must outlive the table.
iree_status_t iree_hal_streaming_execution_resource_table_initialize(
    iree_hal_device_t* device, iree_allocator_t host_allocator,
    iree_hal_streaming_execution_resource_table_t* out_table);

// Releases all entries and storage owned by |table|. A zero-initialized table
// is accepted to simplify containing-device error cleanup. No resolved entry
// may be used after this call.
void iree_hal_streaming_execution_resource_table_deinitialize(
    iree_hal_streaming_execution_resource_table_t* table);

// Returns the nonzero process-unique incarnation assigned to |table|.
uint64_t iree_hal_streaming_execution_resource_table_incarnation(
    const iree_hal_streaming_execution_resource_table_t* table);

// Interns |resources| for |queue_family| and returns its stable table identity.
//
// Empty resource lists are canonicalized to the complete explicit family list.
// Explicit lists must be sorted, unique, in range, and satisfy every family
// resource-group minimum. |out_set_id| is unchanged on failure.
iree_status_t iree_hal_streaming_execution_resource_table_intern(
    iree_hal_streaming_execution_resource_table_t* table,
    const iree_hal_queue_family_t* queue_family,
    iree_hal_queue_execution_resource_list_t resources,
    iree_hal_streaming_execution_resource_set_id_t* out_set_id);

// Resolves |set_id| to an immutable borrowed record or returns NULL when the
// identity is invalid for |table|.
const iree_hal_streaming_execution_resource_set_t*
iree_hal_streaming_execution_resource_table_resolve(
    iree_hal_streaming_execution_resource_table_t* table,
    iree_hal_streaming_execution_resource_set_id_t set_id);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // LIBHRX_SRC_BINDING_COMMON_EXECUTION_RESOURCE_H_
