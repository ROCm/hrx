// Copyright 2026 The HRX Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "common/execution_resource.h"

#include <string.h>

// Storage for one public immutable record and its canonical ordinal list.
typedef struct iree_hal_streaming_execution_resource_set_storage_t {
  // Public record returned to table consumers.
  iree_hal_streaming_execution_resource_set_t set;

  // Canonical ordinals referenced by |set.resources|.
  iree_hal_queue_execution_resource_ordinal_t ordinals[];
} iree_hal_streaming_execution_resource_set_storage_t;

// Next table incarnation to issue. Zero permanently marks exhaustion after the
// final nonzero incarnation has been issued.
static iree_atomic_uint64_t
    iree_hal_streaming_next_execution_resource_table_incarnation =
        IREE_ATOMIC_VAR_INIT(1);

static iree_status_t
iree_hal_streaming_execution_resource_table_allocate_incarnation(
    uint64_t* out_incarnation) {
  uint64_t current = iree_atomic_load(
      &iree_hal_streaming_next_execution_resource_table_incarnation,
      iree_memory_order_relaxed);
  while (current != 0) {
    const uint64_t next = current + 1;
    if (iree_atomic_compare_exchange_weak(
            &iree_hal_streaming_next_execution_resource_table_incarnation,
            &current, next, iree_memory_order_relaxed,
            iree_memory_order_relaxed)) {
      *out_incarnation = current;
      return iree_ok_status();
    }
  }
  return iree_make_status(
      IREE_STATUS_RESOURCE_EXHAUSTED,
      "execution-resource table incarnation space is exhausted");
}

static iree_hal_queue_execution_resource_ordinal_t
iree_hal_streaming_execution_resource_canonical_ordinal(
    iree_hal_queue_execution_resource_list_t resources,
    iree_host_size_t index) {
  return resources.count ? resources.ordinals[index]
                         : (iree_hal_queue_execution_resource_ordinal_t)index;
}

static iree_status_t iree_hal_streaming_execution_resource_validate_and_measure(
    const iree_hal_queue_family_spec_t* family_spec,
    iree_hal_queue_execution_resource_list_t resources,
    iree_host_size_t* out_canonical_count, uint32_t* out_execution_unit_count) {
  if (IREE_UNLIKELY(resources.count && !resources.ordinals)) {
    return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                            "execution-resource list has count %" PRIhsz
                            " but NULL storage",
                            resources.count);
  }

  const iree_host_size_t canonical_count =
      resources.count ? resources.count : family_spec->execution_resource_count;
  uint64_t execution_unit_count = 0;
  for (iree_host_size_t i = 0; i < canonical_count; ++i) {
    const iree_hal_queue_execution_resource_ordinal_t resource_ordinal =
        iree_hal_streaming_execution_resource_canonical_ordinal(resources, i);
    if (IREE_UNLIKELY(
            i > 0 &&
            resource_ordinal <=
                iree_hal_streaming_execution_resource_canonical_ordinal(
                    resources, i - 1))) {
      return iree_make_status(
          IREE_STATUS_INVALID_ARGUMENT,
          "execution-resource ordinals must be sorted and unique");
    }
    if (IREE_UNLIKELY(resource_ordinal >=
                      family_spec->execution_resource_count)) {
      return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                              "execution-resource ordinal %u is outside family "
                              "resource count %" PRIhsz,
                              resource_ordinal,
                              family_spec->execution_resource_count);
    }
    execution_unit_count +=
        family_spec->execution_resources[resource_ordinal].execution_unit_count;
  }
  if (IREE_UNLIKELY(execution_unit_count > UINT32_MAX)) {
    return iree_make_status(IREE_STATUS_OUT_OF_RANGE,
                            "execution-resource set spans %" PRIu64
                            " raw units, exceeding the supported count",
                            execution_unit_count);
  }

  for (iree_host_size_t group_ordinal = 0;
       group_ordinal < family_spec->execution_resource_group_count;
       ++group_ordinal) {
    iree_host_size_t selected_resource_count = 0;
    for (iree_host_size_t i = 0; i < canonical_count; ++i) {
      const iree_hal_queue_execution_resource_ordinal_t resource_ordinal =
          iree_hal_streaming_execution_resource_canonical_ordinal(resources, i);
      selected_resource_count +=
          family_spec->execution_resources[resource_ordinal].group_ordinal ==
          group_ordinal;
    }
    const uint32_t minimum_selected_resource_count =
        family_spec->execution_resource_groups[group_ordinal]
            .minimum_selected_resource_count;
    if (IREE_UNLIKELY(selected_resource_count <
                      minimum_selected_resource_count)) {
      return iree_make_status(
          IREE_STATUS_INVALID_ARGUMENT,
          "execution-resource group %" PRIhsz
          " requires at least %u selections but the set has %" PRIhsz,
          group_ordinal, minimum_selected_resource_count,
          selected_resource_count);
    }
  }

  *out_canonical_count = canonical_count;
  *out_execution_unit_count = (uint32_t)execution_unit_count;
  return iree_ok_status();
}

static bool iree_hal_streaming_execution_resource_set_matches(
    const iree_hal_streaming_execution_resource_set_t* set,
    iree_hal_queue_family_ordinal_t queue_family_ordinal,
    iree_hal_queue_execution_resource_list_t resources,
    iree_host_size_t canonical_count) {
  if (set->queue_family_ordinal != queue_family_ordinal ||
      set->resources.count != canonical_count) {
    return false;
  }
  for (iree_host_size_t i = 0; i < canonical_count; ++i) {
    if (set->resources.ordinals[i] !=
        iree_hal_streaming_execution_resource_canonical_ordinal(resources, i)) {
      return false;
    }
  }
  return true;
}

iree_status_t iree_hal_streaming_execution_resource_table_initialize(
    iree_hal_device_t* device, iree_allocator_t host_allocator,
    iree_hal_streaming_execution_resource_table_t* out_table) {
  IREE_ASSERT_ARGUMENT(device);
  IREE_ASSERT_ARGUMENT(out_table);

  uint64_t incarnation = 0;
  IREE_RETURN_IF_ERROR(
      iree_hal_streaming_execution_resource_table_allocate_incarnation(
          &incarnation));

  memset(out_table, 0, sizeof(*out_table));
  iree_slim_mutex_initialize(&out_table->mutex);
  out_table->host_allocator = host_allocator;
  out_table->device = device;
  out_table->incarnation = incarnation;
  return iree_ok_status();
}

void iree_hal_streaming_execution_resource_table_deinitialize(
    iree_hal_streaming_execution_resource_table_t* table) {
  if (!table || table->incarnation == 0) return;
  for (iree_host_size_t i = 0; i < table->entry_count; ++i) {
    iree_allocator_free(table->host_allocator, table->entries[i]);
  }
  iree_allocator_free(table->host_allocator, table->entries);
  iree_slim_mutex_deinitialize(&table->mutex);
  memset(table, 0, sizeof(*table));
}

uint64_t iree_hal_streaming_execution_resource_table_incarnation(
    const iree_hal_streaming_execution_resource_table_t* table) {
  return table->incarnation;
}

iree_status_t iree_hal_streaming_execution_resource_table_intern(
    iree_hal_streaming_execution_resource_table_t* table,
    const iree_hal_queue_family_t* queue_family,
    iree_hal_queue_execution_resource_list_t resources,
    iree_hal_streaming_execution_resource_set_id_t* out_set_id) {
  IREE_ASSERT_ARGUMENT(table);
  IREE_ASSERT_ARGUMENT(queue_family);
  IREE_ASSERT_ARGUMENT(out_set_id);

  const iree_hal_queue_family_ordinal_t queue_family_ordinal =
      iree_hal_queue_family_ordinal(queue_family);
  if (IREE_UNLIKELY(iree_hal_device_queue_family(
                        table->device, queue_family_ordinal) != queue_family)) {
    return iree_make_status(
        IREE_STATUS_INVALID_ARGUMENT,
        "execution-resource queue family does not belong to the table device");
  }
  const iree_hal_queue_family_spec_t* family_spec =
      iree_hal_queue_family_spec(queue_family);
  iree_host_size_t canonical_count = 0;
  uint32_t execution_unit_count = 0;
  IREE_RETURN_IF_ERROR(
      iree_hal_streaming_execution_resource_validate_and_measure(
          family_spec, resources, &canonical_count, &execution_unit_count));

  iree_hal_streaming_execution_resource_set_id_t set_id =
      IREE_HAL_STREAMING_EXECUTION_RESOURCE_SET_ID_INVALID;
  iree_hal_streaming_execution_resource_set_storage_t* new_storage = NULL;
  iree_status_t status = iree_ok_status();
  iree_slim_mutex_lock(&table->mutex);
  for (iree_host_size_t i = 0; i < table->entry_count; ++i) {
    if (iree_hal_streaming_execution_resource_set_matches(
            table->entries[i], queue_family_ordinal, resources,
            canonical_count)) {
      set_id = (iree_hal_streaming_execution_resource_set_id_t)i + 1;
      break;
    }
  }
  if (set_id == IREE_HAL_STREAMING_EXECUTION_RESOURCE_SET_ID_INVALID) {
    status = iree_allocator_malloc_struct_array(
        table->host_allocator, sizeof(*new_storage), canonical_count,
        sizeof(*new_storage->ordinals), (void**)&new_storage);
  }
  if (iree_status_is_ok(status) && new_storage) {
    new_storage->set.queue_family_ordinal = queue_family_ordinal;
    new_storage->set.execution_unit_count = execution_unit_count;
    new_storage->set.resources.count = canonical_count;
    new_storage->set.resources.ordinals =
        canonical_count ? new_storage->ordinals : NULL;
    for (iree_host_size_t i = 0; i < canonical_count; ++i) {
      new_storage->ordinals[i] =
          iree_hal_streaming_execution_resource_canonical_ordinal(resources, i);
    }
    if (IREE_UNLIKELY(table->entry_count == IREE_HOST_SIZE_MAX)) {
      status = iree_make_status(IREE_STATUS_RESOURCE_EXHAUSTED,
                                "execution-resource set table is full");
    }
  }
  if (iree_status_is_ok(status) && new_storage &&
      table->entry_count == table->entry_capacity) {
    status = iree_allocator_grow_array(
        table->host_allocator, table->entry_count + 1, sizeof(*table->entries),
        &table->entry_capacity, (void**)&table->entries);
  }
  if (iree_status_is_ok(status) && new_storage) {
    table->entries[table->entry_count] = &new_storage->set;
    ++table->entry_count;
    set_id = (iree_hal_streaming_execution_resource_set_id_t)table->entry_count;
    new_storage = NULL;
  }
  iree_slim_mutex_unlock(&table->mutex);

  iree_allocator_free(table->host_allocator, new_storage);
  if (iree_status_is_ok(status)) {
    *out_set_id = set_id;
  }
  return status;
}

const iree_hal_streaming_execution_resource_set_t*
iree_hal_streaming_execution_resource_table_resolve(
    iree_hal_streaming_execution_resource_table_t* table,
    iree_hal_streaming_execution_resource_set_id_t set_id) {
  IREE_ASSERT_ARGUMENT(table);
  if (set_id == IREE_HAL_STREAMING_EXECUTION_RESOURCE_SET_ID_INVALID) {
    return NULL;
  }

  const iree_hal_streaming_execution_resource_set_t* set = NULL;
  iree_slim_mutex_lock(&table->mutex);
  if (set_id <= (uint64_t)table->entry_count) {
    set = table->entries[(iree_host_size_t)set_id - 1];
  }
  iree_slim_mutex_unlock(&table->mutex);
  return set;
}
