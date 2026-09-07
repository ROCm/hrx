// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/xdna/program.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "libamdf/src/child_tracker.h"
#include "libamdf/src/device.h"
#include "libamdf/src/structure.h"
#include "libamdf/src/xdna/device.h"
#include "libamdf/src/xdna/endpoint_profile.h"

struct amdf_xdna_program_t {
  // Device borrowed for the lifetime of this program.
  amdf_device_t* device;
  // Number of live commands borrowing this program.
  amdf_child_tracker_t commands;
  // Immutable properties established before publication.
  amdf_xdna_program_info_t info;
  // Copied component records at the beginning of trailing storage.
  amdf_xdna_program_component_t* components;
};

static const amdf_xdna_binary_format_info_t*
amdf_xdna_program_component_query_format(
    const amdf_xdna_endpoint_info_t* endpoint_info,
    amdf_xdna_program_component_kind_t kind) {
  switch (kind) {
    case AMDF_XDNA_PROGRAM_COMPONENT_PDI:
      return &endpoint_info->program.component_formats.pdi;
    case AMDF_XDNA_PROGRAM_COMPONENT_ARRAY_CONFIGURATION:
      return &endpoint_info->program.component_formats.array_configuration;
    case AMDF_XDNA_PROGRAM_COMPONENT_SAVE:
      return &endpoint_info->program.component_formats.save;
    case AMDF_XDNA_PROGRAM_COMPONENT_RESTORE:
      return &endpoint_info->program.component_formats.restore;
    default:
      return NULL;
  }
}

static amdf_status_t amdf_xdna_program_validate_footprint(
    const amdf_xdna_device_info_t* device_info,
    const amdf_xdna_program_footprint_t* footprint) {
  if (footprint->column_count == 0 || footprint->row_count == 0 ||
      footprint->row_count > device_info->row_count) {
    return amdf_make_api_status(AMDF_STATUS_CODE_OUT_OF_RANGE);
  }
  switch (footprint->coordinate_mode) {
    case AMDF_XDNA_COORDINATE_MODE_CONTEXT_RELATIVE:
      if (footprint->column_origin != 0 ||
          footprint->column_count > device_info->columns.logical_count) {
        return amdf_make_api_status(AMDF_STATUS_CODE_OUT_OF_RANGE);
      }
      return AMDF_STATUS_OK;
    case AMDF_XDNA_COORDINATE_MODE_PHYSICAL:
      if (footprint->column_origin < device_info->columns.physical_origin ||
          footprint->column_origin - device_info->columns.physical_origin >
              device_info->columns.physical_count ||
          footprint->column_count >
              device_info->columns.physical_count -
                  (footprint->column_origin -
                   device_info->columns.physical_origin)) {
        return amdf_make_api_status(AMDF_STATUS_CODE_OUT_OF_RANGE);
      }
      return AMDF_STATUS_OK;
    default:
      return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
}

static amdf_status_t amdf_xdna_program_validate_components(
    const amdf_xdna_endpoint_info_t* endpoint_info,
    const amdf_xdna_transaction_target_t* transaction_target,
    const amdf_xdna_program_create_info_t* create_info,
    uint64_t* out_total_byte_length) {
  if (create_info->component_count == 0 || create_info->components == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  if (create_info->component_count >
      endpoint_info->program.maximum_component_count) {
    return amdf_make_api_status(AMDF_STATUS_CODE_OUT_OF_RANGE);
  }

  uint32_t component_kinds = 0;
  uint64_t total_byte_length = 0;
  for (uint32_t i = 0; i < create_info->component_count; ++i) {
    const amdf_xdna_program_component_t* component =
        &create_info->components[i];
    const amdf_xdna_binary_format_info_t* format =
        amdf_xdna_program_component_query_format(endpoint_info,
                                                 component->kind);
    if (format == NULL || component->reserved != 0 ||
        component->bytes == NULL || component->byte_length == 0) {
      return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
    }
    if (format->format == AMDF_XDNA_BINARY_FORMAT_UNKNOWN) {
      return amdf_make_api_status(AMDF_STATUS_CODE_UNSUPPORTED);
    }
    const uint32_t kind_bit = UINT32_C(1) << component->kind;
    if ((component_kinds & kind_bit) != 0) {
      return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
    }
    component_kinds |= kind_bit;
    if (component->byte_length >
            endpoint_info->program.maximum_component_byte_length ||
        component->byte_length > UINT64_MAX - total_byte_length) {
      return amdf_make_api_status(AMDF_STATUS_CODE_OUT_OF_RANGE);
    }
    if (format->format != AMDF_XDNA_BINARY_FORMAT_TRANSACTION) {
      return amdf_make_api_status(AMDF_STATUS_CODE_UNSUPPORTED);
    }
    const amdf_status_t status = amdf_xdna_transaction_validate(
        transaction_target, format->version, component->bytes,
        component->byte_length);
    if (!amdf_status_is_ok(status)) {
      return status;
    }
    total_byte_length += component->byte_length;
  }
  if ((component_kinds &
       (UINT32_C(1) << AMDF_XDNA_PROGRAM_COMPONENT_ARRAY_CONFIGURATION)) == 0) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  if (total_byte_length > endpoint_info->program.maximum_total_byte_length ||
      total_byte_length > SIZE_MAX) {
    return amdf_make_api_status(AMDF_STATUS_CODE_OUT_OF_RANGE);
  }
  *out_total_byte_length = total_byte_length;
  return AMDF_STATUS_OK;
}

static amdf_status_t amdf_xdna_program_validate_create_info(
    amdf_device_t* device, const amdf_xdna_program_create_info_t* create_info,
    uint64_t* out_total_byte_length) {
  if (!amdf_device_is_engine(device, AMDF_ENGINE_KIND_XDNA)) {
    return device == NULL
               ? amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT)
               : amdf_make_api_status(AMDF_STATUS_CODE_UNSUPPORTED);
  }
  amdf_status_t status = amdf_structure_validate_input(
      create_info, AMDF_STRUCTURE_TYPE_XDNA_PROGRAM_CREATE_INFO,
      (uint32_t)sizeof(amdf_xdna_program_create_info_t));
  if (!amdf_status_is_ok(status)) {
    return status;
  }
  if (create_info->reserved != 0) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  const amdf_xdna_endpoint_profile_t* profile =
      amdf_xdna_device_get_profile(device);
  const amdf_xdna_endpoint_info_t* endpoint_info =
      amdf_xdna_endpoint_profile_get_info(profile);
  const amdf_xdna_program_flags_t known_flags =
      AMDF_XDNA_PROGRAM_FLAG_RESIDENT | AMDF_XDNA_PROGRAM_FLAG_PREEMPTIBLE;
  if ((create_info->required_flags & ~known_flags) != 0) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  if ((create_info->required_flags & ~endpoint_info->program.supported_flags) !=
      0) {
    return amdf_make_api_status(AMDF_STATUS_CODE_UNSUPPORTED);
  }
  status = amdf_xdna_program_validate_footprint(
      amdf_xdna_device_get_info(device), &create_info->footprint);
  if (!amdf_status_is_ok(status)) {
    return status;
  }
  amdf_xdna_transaction_target_t transaction_target;
  amdf_xdna_device_query_transaction_target(device, &transaction_target);
  return amdf_xdna_program_validate_components(
      endpoint_info, &transaction_target, create_info, out_total_byte_length);
}

static amdf_status_t amdf_xdna_program_calculate_allocation_size(
    uint32_t component_count, uint64_t total_byte_length,
    size_t* out_allocation_size) {
  if ((size_t)component_count > (SIZE_MAX - sizeof(amdf_xdna_program_t)) /
                                    sizeof(amdf_xdna_program_component_t)) {
    return amdf_make_api_status(AMDF_STATUS_CODE_RESOURCE_EXHAUSTED);
  }
  const size_t component_storage_size =
      (size_t)component_count * sizeof(amdf_xdna_program_component_t);
  const size_t prefix_size =
      sizeof(amdf_xdna_program_t) + component_storage_size;
  if ((size_t)total_byte_length > SIZE_MAX - prefix_size) {
    return amdf_make_api_status(AMDF_STATUS_CODE_RESOURCE_EXHAUSTED);
  }
  *out_allocation_size = prefix_size + (size_t)total_byte_length;
  return AMDF_STATUS_OK;
}

amdf_status_t AMDF_CALL amdf_xdna_program_create(
    amdf_device_t* device, const amdf_xdna_program_create_info_t* create_info,
    amdf_xdna_program_t** out_program) {
  if (out_program == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  *out_program = NULL;

  uint64_t total_byte_length = 0;
  amdf_status_t status = amdf_xdna_program_validate_create_info(
      device, create_info, &total_byte_length);
  if (!amdf_status_is_ok(status)) {
    return status;
  }
  size_t allocation_size = 0;
  status = amdf_xdna_program_calculate_allocation_size(
      create_info->component_count, total_byte_length, &allocation_size);
  if (!amdf_status_is_ok(status)) {
    return status;
  }

  amdf_xdna_program_t* program =
      (amdf_xdna_program_t*)calloc(1, allocation_size);
  if (program == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_RESOURCE_EXHAUSTED);
  }
  status = amdf_device_register_child(device);
  if (amdf_status_is_ok(status)) {
    program->device = device;
    amdf_child_tracker_initialize(&program->commands);
    program->components = (amdf_xdna_program_component_t*)(program + 1);
    uint8_t* component_bytes =
        (uint8_t*)(program->components + create_info->component_count);
    for (uint32_t i = 0; i < create_info->component_count; ++i) {
      program->components[i] = create_info->components[i];
      program->components[i].bytes = component_bytes;
      memcpy(component_bytes, create_info->components[i].bytes,
             (size_t)create_info->components[i].byte_length);
      component_bytes += create_info->components[i].byte_length;
    }
    program->info.type = AMDF_STRUCTURE_TYPE_XDNA_PROGRAM_INFO;
    program->info.structure_size = sizeof(program->info);
    program->info.flags = create_info->required_flags;
    program->info.component_count = create_info->component_count;
    program->info.total_byte_length = total_byte_length;
    program->info.reset_epoch = amdf_xdna_device_query_reset_epoch(device);
    program->info.footprint = create_info->footprint;
    *out_program = program;
  } else {
    free(program);
  }
  return status;
}

amdf_status_t AMDF_CALL amdf_xdna_program_query_info(
    amdf_xdna_program_t* program, amdf_xdna_program_info_t* out_info) {
  if (program == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  const amdf_status_t status = amdf_structure_validate_output(
      out_info, AMDF_STRUCTURE_TYPE_XDNA_PROGRAM_INFO,
      (uint32_t)sizeof(amdf_xdna_program_info_t));
  if (!amdf_status_is_ok(status)) {
    return status;
  }
  const uint32_t structure_size = out_info->structure_size;
  void* const next = out_info->next;
  *out_info = program->info;
  out_info->structure_size = structure_size;
  out_info->next = next;
  return AMDF_STATUS_OK;
}

amdf_device_t* amdf_xdna_program_get_device(amdf_xdna_program_t* program) {
  return program->device;
}

const amdf_xdna_program_info_t* amdf_xdna_program_get_info(
    const amdf_xdna_program_t* program) {
  return &program->info;
}

const amdf_xdna_program_component_t* amdf_xdna_program_get_components(
    const amdf_xdna_program_t* program, uint32_t* out_component_count) {
  *out_component_count = program->info.component_count;
  return program->components;
}

amdf_status_t amdf_xdna_program_register_command(amdf_xdna_program_t* program) {
  return amdf_child_tracker_register(&program->commands);
}

void amdf_xdna_program_unregister_command(amdf_xdna_program_t* program) {
  amdf_child_tracker_unregister(&program->commands);
}

amdf_status_t AMDF_CALL
amdf_xdna_program_destroy(amdf_xdna_program_t* program) {
  if (program == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  if (amdf_child_tracker_count(&program->commands) != 0) {
    return amdf_make_api_status(AMDF_STATUS_CODE_BUSY);
  }
  amdf_device_unregister_child(program->device);
  free(program);
  return AMDF_STATUS_OK;
}
