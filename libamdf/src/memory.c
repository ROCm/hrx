// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/memory.h"

#include <stddef.h>
#include <stdlib.h>

#include "libamdf/src/device.h"
#include "libamdf/src/structure.h"

static amdf_status_t amdf_memory_validate_create_info(
    const amdf_memory_create_info_t* create_info) {
  const amdf_status_t status = amdf_structure_validate_input(
      create_info, AMDF_STRUCTURE_TYPE_MEMORY_CREATE_INFO,
      (uint32_t)sizeof(amdf_memory_create_info_t));
  if (!amdf_status_is_ok(status)) {
    return status;
  }
  if (create_info->memory_class < AMDF_MEMORY_CLASS_SYSTEM ||
      create_info->memory_class > AMDF_MEMORY_CLASS_REGISTERED_HOST) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  const amdf_memory_flags_t known_flags =
      AMDF_MEMORY_FLAG_HOST_VISIBLE | AMDF_MEMORY_FLAG_DEVICE_LOCAL |
      AMDF_MEMORY_FLAG_SHAREABLE | AMDF_MEMORY_FLAG_EXECUTABLE |
      AMDF_MEMORY_FLAG_QUEUE_STORAGE | AMDF_MEMORY_FLAG_HOST_COHERENT |
      AMDF_MEMORY_FLAG_DEVICE_ADDRESS;
  if ((create_info->required_flags & ~known_flags) != 0 ||
      create_info->byte_length == 0) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  if (create_info->minimum_alignment != 0 &&
      (create_info->minimum_alignment & (create_info->minimum_alignment - 1)) !=
          0) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  if ((create_info->memory_class == AMDF_MEMORY_CLASS_REGISTERED_HOST) !=
      (create_info->registered_host_pointer != NULL)) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  return AMDF_STATUS_OK;
}

amdf_status_t amdf_memory_initialize(amdf_memory_t* memory,
                                     const amdf_memory_vtable_t* vtable,
                                     amdf_device_t* device) {
  const amdf_status_t status = amdf_device_register_child(device);
  if (!amdf_status_is_ok(status)) {
    return status;
  }
  memory->vtable = vtable;
  memory->device = device;
  amdf_child_tracker_initialize(&memory->children);
  return AMDF_STATUS_OK;
}

void amdf_memory_deinitialize(amdf_memory_t* memory) {
  amdf_device_unregister_child(memory->device);
  memory->device = NULL;
}

amdf_status_t amdf_memory_register_child(amdf_memory_t* memory) {
  return amdf_child_tracker_register(&memory->children);
}

void amdf_memory_unregister_child(amdf_memory_t* memory) {
  amdf_child_tracker_unregister(&memory->children);
}

amdf_status_t AMDF_CALL amdf_memory_create(
    amdf_device_t* device, const amdf_memory_create_info_t* create_info,
    amdf_memory_t** out_memory) {
  if (out_memory == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  *out_memory = NULL;
  if (device == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  const amdf_status_t status = amdf_memory_validate_create_info(create_info);
  if (!amdf_status_is_ok(status)) {
    return status;
  }
  if (device->vtable->memory_create == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_UNSUPPORTED);
  }
  return device->vtable->memory_create(device, create_info, out_memory);
}

amdf_status_t AMDF_CALL amdf_memory_query_info(amdf_memory_t* memory,
                                               amdf_memory_info_t* out_info) {
  if (memory == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  const amdf_status_t status =
      amdf_structure_validate_output(out_info, AMDF_STRUCTURE_TYPE_MEMORY_INFO,
                                     (uint32_t)sizeof(amdf_memory_info_t));
  if (!amdf_status_is_ok(status)) {
    return status;
  }
  const uint32_t structure_size = out_info->structure_size;
  void* const next = out_info->next;
  *out_info = memory->info;
  out_info->structure_size = structure_size;
  out_info->next = next;
  return AMDF_STATUS_OK;
}

amdf_status_t AMDF_CALL amdf_memory_map(amdf_memory_t* memory,
                                        const amdf_memory_map_info_t* map_info,
                                        amdf_host_mapping_t** out_mapping) {
  if (out_mapping == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  *out_mapping = NULL;
  if (memory == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  const amdf_status_t status = amdf_structure_validate_input(
      map_info, AMDF_STRUCTURE_TYPE_MEMORY_MAP_INFO,
      (uint32_t)sizeof(amdf_memory_map_info_t));
  if (!amdf_status_is_ok(status)) {
    return status;
  }
  const amdf_memory_map_flags_t known_flags =
      AMDF_MEMORY_MAP_FLAG_READ | AMDF_MEMORY_MAP_FLAG_WRITE;
  if (map_info->flags == 0 || (map_info->flags & ~known_flags) != 0 ||
      map_info->byte_length == 0 ||
      map_info->byte_offset > memory->info.byte_length ||
      map_info->byte_length >
          memory->info.byte_length - map_info->byte_offset) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  if ((memory->info.flags & AMDF_MEMORY_FLAG_HOST_VISIBLE) == 0) {
    return amdf_make_api_status(AMDF_STATUS_CODE_UNSUPPORTED);
  }
  return memory->vtable->map(memory, map_info, out_mapping);
}

amdf_status_t AMDF_CALL amdf_memory_destroy(amdf_memory_t* memory) {
  if (memory == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  if (amdf_child_tracker_count(&memory->children) != 0) {
    return amdf_make_api_status(AMDF_STATUS_CODE_BUSY);
  }
  const amdf_status_t status = memory->vtable->destroy_native(memory);
  if (amdf_status_is_ok(status)) {
    amdf_memory_deinitialize(memory);
    free(memory);
  }
  return status;
}
