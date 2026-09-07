// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/xdna/memory.h"

#include <stddef.h>
#include <stdlib.h>

#include "libamdf/src/host_mapping.h"
#include "libamdf/src/memory.h"
#include "libamdf/src/xdna/device.h"
#include "libamdf/src/xdna/umd/memory.h"

typedef struct amdf_xdna_memory_t {
  // Generic memory state shared by every engine implementation.
  amdf_memory_t base;
  // Exact native physical backing and XDNA attachment.
  amdf_xdna_umd_memory_t* umd;
} amdf_xdna_memory_t;

typedef struct amdf_xdna_host_mapping_t {
  // Generic host mapping state shared by every engine implementation.
  amdf_host_mapping_t base;
  // Exact native host mapping state.
  amdf_xdna_umd_host_mapping_t* umd;
} amdf_xdna_host_mapping_t;

_Static_assert(offsetof(amdf_xdna_memory_t, base) == 0,
               "XDNA memory base must be the first field");
_Static_assert(offsetof(amdf_xdna_host_mapping_t, base) == 0,
               "XDNA mapping base must be the first field");

static amdf_status_t amdf_xdna_host_mapping_cache_control(
    amdf_host_mapping_t* base_mapping, amdf_host_cache_operation_t operation,
    uint64_t byte_offset, uint64_t byte_length) {
  amdf_xdna_host_mapping_t* mapping = (amdf_xdna_host_mapping_t*)base_mapping;
  return amdf_xdna_umd_host_mapping_cache_control(mapping->umd, operation,
                                                  byte_offset, byte_length);
}

static amdf_status_t amdf_xdna_host_mapping_destroy_native(
    amdf_host_mapping_t* base_mapping) {
  amdf_xdna_host_mapping_t* mapping = (amdf_xdna_host_mapping_t*)base_mapping;
  const amdf_status_t status = amdf_xdna_umd_host_mapping_destroy(mapping->umd);
  if (amdf_status_is_ok(status)) {
    mapping->umd = NULL;
  }
  return status;
}

static const amdf_host_mapping_vtable_t amdf_xdna_host_mapping_vtable = {
    .cache_control = amdf_xdna_host_mapping_cache_control,
    .destroy_native = amdf_xdna_host_mapping_destroy_native,
};

static amdf_status_t amdf_xdna_memory_map(
    amdf_memory_t* base_memory, const amdf_memory_map_info_t* map_info,
    amdf_host_mapping_t** out_mapping) {
  amdf_xdna_memory_t* memory = (amdf_xdna_memory_t*)base_memory;
  amdf_xdna_host_mapping_t* mapping =
      (amdf_xdna_host_mapping_t*)calloc(1, sizeof(*mapping));
  if (mapping == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_RESOURCE_EXHAUSTED);
  }
  amdf_status_t status = amdf_host_mapping_initialize(
      &mapping->base, &amdf_xdna_host_mapping_vtable, base_memory);

  amdf_xdna_umd_host_mapping_result_t result = {0};
  if (amdf_status_is_ok(status)) {
    status =
        amdf_xdna_umd_memory_map(memory->umd, map_info, &mapping->umd, &result);
  }
  if (amdf_status_is_ok(status)) {
    mapping->base.info.type = AMDF_STRUCTURE_TYPE_HOST_MAPPING_INFO;
    mapping->base.info.structure_size = sizeof(mapping->base.info);
    mapping->base.info.flags = result.flags;
    mapping->base.info.cacheability = result.cacheability;
    mapping->base.info.pointer = result.pointer;
    mapping->base.info.byte_length = result.byte_length;
    mapping->base.info.cache_line_size = result.cache_line_size;
    mapping->base.info.reset_epoch = base_memory->info.reset_epoch;
    *out_mapping = &mapping->base;
  } else {
    if (mapping->base.memory != NULL) {
      amdf_host_mapping_deinitialize(&mapping->base);
    }
    free(mapping);
  }
  return status;
}

static amdf_status_t amdf_xdna_memory_destroy_native(
    amdf_memory_t* base_memory) {
  amdf_xdna_memory_t* memory = (amdf_xdna_memory_t*)base_memory;
  const amdf_status_t status = amdf_xdna_umd_memory_destroy(memory->umd);
  if (amdf_status_is_ok(status)) {
    memory->umd = NULL;
  }
  return status;
}

static const amdf_memory_vtable_t amdf_xdna_memory_vtable = {
    .map = amdf_xdna_memory_map,
    .destroy_native = amdf_xdna_memory_destroy_native,
};

amdf_status_t amdf_xdna_memory_create(
    amdf_device_t* device, const amdf_memory_create_info_t* create_info,
    amdf_memory_t** out_memory) {
  amdf_xdna_memory_t* memory = (amdf_xdna_memory_t*)calloc(1, sizeof(*memory));
  if (memory == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_RESOURCE_EXHAUSTED);
  }
  amdf_status_t status =
      amdf_memory_initialize(&memory->base, &amdf_xdna_memory_vtable, device);

  amdf_xdna_umd_memory_result_t result = {0};
  if (amdf_status_is_ok(status)) {
    status = amdf_xdna_umd_memory_create(amdf_xdna_device_get_umd(device),
                                         create_info, &memory->umd, &result);
  }
  if (amdf_status_is_ok(status)) {
    memory->base.info.type = AMDF_STRUCTURE_TYPE_MEMORY_INFO;
    memory->base.info.structure_size = sizeof(memory->base.info);
    memory->base.info.memory_class = result.memory_class;
    memory->base.info.flags = result.flags;
    memory->base.info.byte_length = result.byte_length;
    memory->base.info.alignment = result.alignment;
    memory->base.info.physical_backing_id = result.physical_backing_id;
    memory->base.info.device_address = result.device_address;
    memory->base.info.reset_epoch = amdf_xdna_device_query_reset_epoch(device);
    *out_memory = &memory->base;
  } else {
    if (memory->base.device != NULL) {
      amdf_memory_deinitialize(&memory->base);
    }
    free(memory);
  }
  return status;
}

amdf_xdna_umd_memory_t* amdf_xdna_memory_get_umd(amdf_memory_t* base_memory) {
  amdf_xdna_memory_t* memory = (amdf_xdna_memory_t*)base_memory;
  return memory->umd;
}
