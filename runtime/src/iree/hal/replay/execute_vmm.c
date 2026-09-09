// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/hal/replay/execute_vmm.h"

#include <inttypes.h>
#include <string.h>

static iree_status_t iree_hal_replay_executor_require_fixed_vmm_payload(
    const iree_hal_replay_file_record_t* record,
    iree_hal_replay_payload_type_t payload_type,
    iree_host_size_t payload_length) {
  IREE_RETURN_IF_ERROR(iree_hal_replay_executor_require_payload(
      record, payload_type, payload_length));
  if (IREE_UNLIKELY(record->payload.data_length != payload_length)) {
    return iree_make_status(IREE_STATUS_DATA_LOSS,
                            "replay VMM payload length mismatch");
  }
  return iree_ok_status();
}

static iree_status_t iree_hal_replay_executor_lookup_owned_reservation(
    iree_hal_replay_executor_t* executor, iree_hal_allocator_t* allocator,
    iree_hal_replay_object_id_t virtual_buffer_id,
    iree_hal_replay_object_entry_t** out_entry) {
  IREE_RETURN_IF_ERROR(iree_hal_replay_executor_lookup(
      executor, virtual_buffer_id, IREE_HAL_REPLAY_OBJECT_TYPE_BUFFER,
      out_entry));
  if (IREE_UNLIKELY(!(*out_entry)->owning_allocator ||
                    (*out_entry)->owning_allocator != allocator)) {
    return iree_make_status(
        IREE_STATUS_DATA_LOSS,
        "replay virtual memory reservation belongs to another allocator");
  }
  return iree_ok_status();
}

static iree_status_t iree_hal_replay_executor_lookup_owned_physical_memory(
    iree_hal_replay_executor_t* executor, iree_hal_allocator_t* allocator,
    iree_hal_replay_object_id_t physical_memory_id,
    iree_hal_replay_object_entry_t** out_entry) {
  IREE_RETURN_IF_ERROR(iree_hal_replay_executor_lookup(
      executor, physical_memory_id, IREE_HAL_REPLAY_OBJECT_TYPE_PHYSICAL_MEMORY,
      out_entry));
  if (IREE_UNLIKELY((*out_entry)->owning_allocator != allocator)) {
    return iree_make_status(
        IREE_STATUS_DATA_LOSS,
        "replay physical memory belongs to another allocator");
  }
  return iree_ok_status();
}

static iree_status_t iree_hal_replay_executor_virtual_memory_release(
    iree_hal_replay_executor_t* executor,
    const iree_hal_replay_file_record_t* record) {
  IREE_RETURN_IF_ERROR(iree_hal_replay_executor_require_fixed_vmm_payload(
      record, IREE_HAL_REPLAY_PAYLOAD_TYPE_ALLOCATOR_VIRTUAL_MEMORY_RELEASE,
      sizeof(iree_hal_replay_allocator_virtual_memory_release_payload_t)));
  iree_hal_replay_allocator_virtual_memory_release_payload_t payload;
  memcpy(&payload, record->payload.data, sizeof(payload));
  if (IREE_UNLIKELY(payload.virtual_buffer_id !=
                    record->header.related_object_id)) {
    return iree_make_status(
        IREE_STATUS_DATA_LOSS,
        "replay virtual memory release object ids do not match");
  }

  iree_hal_replay_object_entry_t* allocator_entry = NULL;
  IREE_RETURN_IF_ERROR(iree_hal_replay_executor_lookup(
      executor, record->header.object_id, IREE_HAL_REPLAY_OBJECT_TYPE_ALLOCATOR,
      &allocator_entry));
  iree_hal_replay_object_entry_t* buffer_entry = NULL;
  IREE_RETURN_IF_ERROR(iree_hal_replay_executor_lookup_owned_reservation(
      executor, allocator_entry->value.allocator, payload.virtual_buffer_id,
      &buffer_entry));
  if (IREE_UNLIKELY(iree_hal_replay_executor_has_virtual_memory_mapping(
          executor, payload.virtual_buffer_id))) {
    return iree_make_status(
        IREE_STATUS_FAILED_PRECONDITION,
        "replay virtual memory reservation is released while still mapped");
  }
  IREE_RETURN_IF_ERROR(iree_hal_allocator_virtual_memory_release(
      allocator_entry->value.allocator, buffer_entry->value.buffer));
  return iree_hal_replay_executor_forget(executor, payload.virtual_buffer_id,
                                         IREE_HAL_REPLAY_OBJECT_TYPE_BUFFER);
}

static iree_status_t iree_hal_replay_executor_physical_memory_free(
    iree_hal_replay_executor_t* executor,
    const iree_hal_replay_file_record_t* record) {
  IREE_RETURN_IF_ERROR(iree_hal_replay_executor_require_fixed_vmm_payload(
      record, IREE_HAL_REPLAY_PAYLOAD_TYPE_ALLOCATOR_PHYSICAL_MEMORY_FREE,
      sizeof(iree_hal_replay_allocator_physical_memory_free_payload_t)));
  iree_hal_replay_allocator_physical_memory_free_payload_t payload;
  memcpy(&payload, record->payload.data, sizeof(payload));
  if (IREE_UNLIKELY(payload.physical_memory_id !=
                    record->header.related_object_id)) {
    return iree_make_status(
        IREE_STATUS_DATA_LOSS,
        "replay physical memory free object ids do not match");
  }

  iree_hal_replay_object_entry_t* allocator_entry = NULL;
  IREE_RETURN_IF_ERROR(iree_hal_replay_executor_lookup(
      executor, record->header.object_id, IREE_HAL_REPLAY_OBJECT_TYPE_ALLOCATOR,
      &allocator_entry));
  iree_hal_replay_object_entry_t* physical_memory_entry = NULL;
  IREE_RETURN_IF_ERROR(iree_hal_replay_executor_lookup_owned_physical_memory(
      executor, allocator_entry->value.allocator, payload.physical_memory_id,
      &physical_memory_entry));
  if (IREE_UNLIKELY(iree_hal_replay_executor_has_physical_memory_mapping(
          executor, payload.physical_memory_id))) {
    return iree_make_status(
        IREE_STATUS_FAILED_PRECONDITION,
        "replay physical memory is freed while still mapped");
  }
  IREE_RETURN_IF_ERROR(iree_hal_allocator_physical_memory_free(
      allocator_entry->value.allocator,
      physical_memory_entry->value.physical_memory.handle));
  return iree_hal_replay_executor_forget(
      executor, payload.physical_memory_id,
      IREE_HAL_REPLAY_OBJECT_TYPE_PHYSICAL_MEMORY);
}

static iree_status_t iree_hal_replay_executor_virtual_memory_map(
    iree_hal_replay_executor_t* executor,
    const iree_hal_replay_file_record_t* record) {
  IREE_RETURN_IF_ERROR(iree_hal_replay_executor_require_fixed_vmm_payload(
      record, IREE_HAL_REPLAY_PAYLOAD_TYPE_ALLOCATOR_VIRTUAL_MEMORY_MAP,
      sizeof(iree_hal_replay_allocator_virtual_memory_map_payload_t)));
  iree_hal_replay_allocator_virtual_memory_map_payload_t payload;
  memcpy(&payload, record->payload.data, sizeof(payload));
  if (IREE_UNLIKELY(payload.virtual_buffer_id !=
                    record->header.related_object_id)) {
    return iree_make_status(
        IREE_STATUS_DATA_LOSS,
        "replay virtual memory map object ids do not match");
  }

  iree_hal_replay_object_entry_t* allocator_entry = NULL;
  IREE_RETURN_IF_ERROR(iree_hal_replay_executor_lookup(
      executor, record->header.object_id, IREE_HAL_REPLAY_OBJECT_TYPE_ALLOCATOR,
      &allocator_entry));
  return iree_hal_replay_executor_map_virtual_memory(
      executor, allocator_entry->value.allocator, payload.virtual_buffer_id,
      payload.physical_memory_id, payload.virtual_offset,
      payload.physical_offset, payload.size);
}

static iree_status_t iree_hal_replay_executor_virtual_memory_unmap(
    iree_hal_replay_executor_t* executor,
    const iree_hal_replay_file_record_t* record) {
  IREE_RETURN_IF_ERROR(iree_hal_replay_executor_require_fixed_vmm_payload(
      record, IREE_HAL_REPLAY_PAYLOAD_TYPE_ALLOCATOR_VIRTUAL_MEMORY_UNMAP,
      sizeof(iree_hal_replay_allocator_virtual_memory_unmap_payload_t)));
  iree_hal_replay_allocator_virtual_memory_unmap_payload_t payload;
  memcpy(&payload, record->payload.data, sizeof(payload));
  if (IREE_UNLIKELY(payload.virtual_buffer_id !=
                    record->header.related_object_id)) {
    return iree_make_status(
        IREE_STATUS_DATA_LOSS,
        "replay virtual memory unmap object ids do not match");
  }

  iree_hal_replay_object_entry_t* allocator_entry = NULL;
  IREE_RETURN_IF_ERROR(iree_hal_replay_executor_lookup(
      executor, record->header.object_id, IREE_HAL_REPLAY_OBJECT_TYPE_ALLOCATOR,
      &allocator_entry));
  return iree_hal_replay_executor_unmap_virtual_memory(
      executor, allocator_entry->value.allocator, payload.virtual_buffer_id,
      payload.virtual_offset, payload.size);
}

static iree_status_t iree_hal_replay_executor_virtual_memory_protect(
    iree_hal_replay_executor_t* executor,
    const iree_hal_replay_file_record_t* record) {
  IREE_RETURN_IF_ERROR(iree_hal_replay_executor_require_fixed_vmm_payload(
      record, IREE_HAL_REPLAY_PAYLOAD_TYPE_ALLOCATOR_VIRTUAL_MEMORY_PROTECT,
      sizeof(iree_hal_replay_allocator_virtual_memory_protect_payload_t)));
  iree_hal_replay_allocator_virtual_memory_protect_payload_t payload;
  memcpy(&payload, record->payload.data, sizeof(payload));
  if (IREE_UNLIKELY(payload.virtual_buffer_id !=
                        record->header.related_object_id ||
                    payload.reserved0 != 0)) {
    return iree_make_status(
        IREE_STATUS_DATA_LOSS,
        "replay virtual memory protect metadata is invalid");
  }

  iree_hal_replay_object_entry_t* allocator_entry = NULL;
  IREE_RETURN_IF_ERROR(iree_hal_replay_executor_lookup(
      executor, record->header.object_id, IREE_HAL_REPLAY_OBJECT_TYPE_ALLOCATOR,
      &allocator_entry));
  iree_hal_replay_object_entry_t* buffer_entry = NULL;
  IREE_RETURN_IF_ERROR(iree_hal_replay_executor_lookup_owned_reservation(
      executor, allocator_entry->value.allocator, payload.virtual_buffer_id,
      &buffer_entry));
  IREE_RETURN_IF_ERROR(
      iree_hal_replay_executor_drain_queue_completions(executor));
  return iree_hal_allocator_virtual_memory_protect(
      allocator_entry->value.allocator, buffer_entry->value.buffer,
      payload.virtual_offset, payload.size, payload.queue_family_affinity,
      payload.access_scope, payload.protection);
}

static iree_status_t iree_hal_replay_executor_virtual_memory_advise(
    iree_hal_replay_executor_t* executor,
    const iree_hal_replay_file_record_t* record) {
  IREE_RETURN_IF_ERROR(iree_hal_replay_executor_require_fixed_vmm_payload(
      record, IREE_HAL_REPLAY_PAYLOAD_TYPE_ALLOCATOR_VIRTUAL_MEMORY_ADVISE,
      sizeof(iree_hal_replay_allocator_virtual_memory_advise_payload_t)));
  iree_hal_replay_allocator_virtual_memory_advise_payload_t payload;
  memcpy(&payload, record->payload.data, sizeof(payload));
  if (IREE_UNLIKELY(payload.virtual_buffer_id !=
                    record->header.related_object_id)) {
    return iree_make_status(
        IREE_STATUS_DATA_LOSS,
        "replay virtual memory advise object ids do not match");
  }

  iree_hal_replay_object_entry_t* allocator_entry = NULL;
  IREE_RETURN_IF_ERROR(iree_hal_replay_executor_lookup(
      executor, record->header.object_id, IREE_HAL_REPLAY_OBJECT_TYPE_ALLOCATOR,
      &allocator_entry));
  iree_hal_replay_object_entry_t* buffer_entry = NULL;
  IREE_RETURN_IF_ERROR(iree_hal_replay_executor_lookup_owned_reservation(
      executor, allocator_entry->value.allocator, payload.virtual_buffer_id,
      &buffer_entry));
  return iree_hal_allocator_virtual_memory_advise(
      allocator_entry->value.allocator, buffer_entry->value.buffer,
      payload.virtual_offset, payload.size, payload.queue_family_affinity,
      payload.advice);
}

iree_status_t iree_hal_replay_executor_replay_vmm_operation(
    iree_hal_replay_executor_t* executor,
    const iree_hal_replay_file_record_t* record) {
  switch (record->header.operation_code) {
    case IREE_HAL_REPLAY_OPERATION_CODE_ALLOCATOR_VIRTUAL_MEMORY_RELEASE:
      return iree_hal_replay_executor_virtual_memory_release(executor, record);
    case IREE_HAL_REPLAY_OPERATION_CODE_ALLOCATOR_PHYSICAL_MEMORY_FREE:
      return iree_hal_replay_executor_physical_memory_free(executor, record);
    case IREE_HAL_REPLAY_OPERATION_CODE_ALLOCATOR_VIRTUAL_MEMORY_MAP:
      return iree_hal_replay_executor_virtual_memory_map(executor, record);
    case IREE_HAL_REPLAY_OPERATION_CODE_ALLOCATOR_VIRTUAL_MEMORY_UNMAP:
      return iree_hal_replay_executor_virtual_memory_unmap(executor, record);
    case IREE_HAL_REPLAY_OPERATION_CODE_ALLOCATOR_VIRTUAL_MEMORY_PROTECT:
      return iree_hal_replay_executor_virtual_memory_protect(executor, record);
    case IREE_HAL_REPLAY_OPERATION_CODE_ALLOCATOR_VIRTUAL_MEMORY_ADVISE:
      return iree_hal_replay_executor_virtual_memory_advise(executor, record);
    default:
      return iree_make_status(
          IREE_STATUS_UNIMPLEMENTED,
          "replay VMM operation %s is not implemented",
          iree_hal_replay_operation_code_string(record->header.operation_code));
  }
}
