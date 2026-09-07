// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/xdna/command.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "libamdf/src/child_tracker.h"
#include "libamdf/src/memory.h"
#include "libamdf/src/structure.h"
#include "libamdf/src/xdna/device.h"
#include "libamdf/src/xdna/endpoint_profile.h"
#include "libamdf/src/xdna/memory.h"
#include "libamdf/src/xdna/program.h"
#include "libamdf/src/xdna/umd/command.h"

struct amdf_xdna_command_t {
  // Program borrowed for the lifetime of this command.
  amdf_xdna_program_t* program;
  // Immutable properties established before publication.
  amdf_xdna_command_info_t info;
  // Number of accepted native submissions still borrowing this command.
  amdf_child_tracker_t submissions;
  // Native immutable command and instruction-slot lease.
  amdf_xdna_umd_command_t* umd;
  // Resolved binding records at the beginning of trailing storage.
  amdf_xdna_resolved_binding_t* bindings;
  // Memory objects borrowed in binding order for lifetime release.
  amdf_memory_t** memory_borrows;
  // Native binding records consumed while preparing the native command.
  amdf_xdna_umd_command_binding_t* umd_bindings;
  // Copied target-native invocation control in trailing storage.
  uint8_t* control_bytes;
};

static const amdf_xdna_program_component_t*
amdf_xdna_command_query_array_configuration(
    const amdf_xdna_program_t* program) {
  uint32_t component_count = 0;
  const amdf_xdna_program_component_t* components =
      amdf_xdna_program_get_components(program, &component_count);
  for (uint32_t i = 0; i < component_count; ++i) {
    if (components[i].kind == AMDF_XDNA_PROGRAM_COMPONENT_ARRAY_CONFIGURATION) {
      return &components[i];
    }
  }
  return NULL;
}

static amdf_status_t amdf_xdna_command_validate_create_info(
    amdf_xdna_program_t* program,
    const amdf_xdna_command_create_info_t* create_info) {
  if (program == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  const amdf_status_t status = amdf_structure_validate_input(
      create_info, AMDF_STRUCTURE_TYPE_XDNA_COMMAND_CREATE_INFO,
      (uint32_t)sizeof(amdf_xdna_command_create_info_t));
  if (!amdf_status_is_ok(status)) {
    return status;
  }
  if (create_info->reserved != 0 || create_info->control_bytes == NULL ||
      create_info->control_byte_length == 0 ||
      (create_info->binding_count != 0) != (create_info->bindings != NULL)) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }

  amdf_device_t* device = amdf_xdna_program_get_device(program);
  const amdf_xdna_endpoint_profile_t* profile =
      amdf_xdna_device_get_profile(device);
  const amdf_xdna_endpoint_info_t* endpoint_info =
      amdf_xdna_endpoint_profile_get_info(profile);
  const uint64_t reset_epoch = amdf_xdna_device_query_reset_epoch(device);
  if (amdf_xdna_program_get_info(program)->reset_epoch != reset_epoch) {
    return amdf_make_api_status(AMDF_STATUS_CODE_FAILED_PRECONDITION);
  }
  if (endpoint_info->command.control_format.format ==
      AMDF_XDNA_BINARY_FORMAT_UNKNOWN) {
    return amdf_make_api_status(AMDF_STATUS_CODE_UNSUPPORTED);
  }
  if (create_info->binding_count >
          endpoint_info->command.maximum_binding_count ||
      create_info->control_byte_length >
          endpoint_info->command.maximum_control_byte_length ||
      create_info->control_byte_length > SIZE_MAX) {
    return amdf_make_api_status(AMDF_STATUS_CODE_OUT_OF_RANGE);
  }
  if (endpoint_info->command.control_format.format !=
      AMDF_XDNA_BINARY_FORMAT_TRANSACTION) {
    return amdf_make_api_status(AMDF_STATUS_CODE_UNSUPPORTED);
  }
  amdf_xdna_transaction_target_t transaction_target;
  amdf_xdna_device_query_transaction_target(device, &transaction_target);
  amdf_status_t validation_status = amdf_xdna_transaction_validate(
      &transaction_target, endpoint_info->command.control_format.version,
      create_info->control_bytes, create_info->control_byte_length);
  if (!amdf_status_is_ok(validation_status)) {
    return validation_status;
  }

  const amdf_xdna_program_component_t* array_configuration =
      amdf_xdna_command_query_array_configuration(program);
  if (array_configuration == NULL || array_configuration->byte_length < 16 ||
      create_info->control_byte_length < 16 ||
      create_info->control_byte_length >
          UINT64_MAX - (array_configuration->byte_length - 16)) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INTERNAL);
  }
  const uint64_t native_byte_length =
      array_configuration->byte_length - 16 + create_info->control_byte_length;
  if (endpoint_info->command.maximum_native_byte_length == 0) {
    return amdf_make_api_status(AMDF_STATUS_CODE_UNSUPPORTED);
  }
  if (native_byte_length > endpoint_info->command.maximum_native_byte_length) {
    return amdf_make_api_status(AMDF_STATUS_CODE_OUT_OF_RANGE);
  }

  for (uint32_t i = 0; i < create_info->binding_count; ++i) {
    const amdf_xdna_command_binding_t* binding = &create_info->bindings[i];
    if (binding->memory == NULL || binding->memory->device != device ||
        binding->byte_length == 0 ||
        (binding->memory->info.flags & AMDF_MEMORY_FLAG_DEVICE_ADDRESS) == 0 ||
        binding->byte_offset > binding->memory->info.byte_length ||
        binding->byte_length >
            binding->memory->info.byte_length - binding->byte_offset ||
        binding->byte_offset >
            UINT64_MAX - binding->memory->info.device_address) {
      return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
    }
    if (binding->memory->info.reset_epoch != reset_epoch) {
      return amdf_make_api_status(AMDF_STATUS_CODE_FAILED_PRECONDITION);
    }
  }
  return AMDF_STATUS_OK;
}

static amdf_status_t amdf_xdna_command_calculate_allocation_size(
    uint32_t binding_count, uint64_t control_byte_length,
    size_t* out_allocation_size) {
  const size_t binding_record_size = sizeof(amdf_xdna_resolved_binding_t) +
                                     sizeof(amdf_memory_t*) +
                                     sizeof(amdf_xdna_umd_command_binding_t);
  if ((size_t)binding_count >
      (SIZE_MAX - sizeof(amdf_xdna_command_t)) / binding_record_size) {
    return amdf_make_api_status(AMDF_STATUS_CODE_RESOURCE_EXHAUSTED);
  }
  const size_t binding_storage_size =
      (size_t)binding_count * binding_record_size;
  const size_t prefix_size = sizeof(amdf_xdna_command_t) + binding_storage_size;
  if ((size_t)control_byte_length > SIZE_MAX - prefix_size) {
    return amdf_make_api_status(AMDF_STATUS_CODE_RESOURCE_EXHAUSTED);
  }
  *out_allocation_size = prefix_size + (size_t)control_byte_length;
  return AMDF_STATUS_OK;
}

amdf_status_t AMDF_CALL
amdf_xdna_command_create(amdf_xdna_program_t* program,
                         const amdf_xdna_command_create_info_t* create_info,
                         amdf_xdna_command_t** out_command) {
  if (out_command == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  *out_command = NULL;
  amdf_status_t status =
      amdf_xdna_command_validate_create_info(program, create_info);
  if (!amdf_status_is_ok(status)) {
    return status;
  }

  size_t allocation_size = 0;
  status = amdf_xdna_command_calculate_allocation_size(
      create_info->binding_count, create_info->control_byte_length,
      &allocation_size);
  if (!amdf_status_is_ok(status)) {
    return status;
  }
  amdf_xdna_command_t* command =
      (amdf_xdna_command_t*)calloc(1, allocation_size);
  if (command == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_RESOURCE_EXHAUSTED);
  }

  status = amdf_xdna_program_register_command(program);
  const bool program_registered = amdf_status_is_ok(status);
  uint32_t registered_binding_count = 0;
  while (amdf_status_is_ok(status) &&
         registered_binding_count < create_info->binding_count) {
    status = amdf_memory_register_child(
        create_info->bindings[registered_binding_count].memory);
    if (amdf_status_is_ok(status)) {
      ++registered_binding_count;
    }
  }
  if (amdf_status_is_ok(status)) {
    command->program = program;
    command->bindings = (amdf_xdna_resolved_binding_t*)(command + 1);
    command->memory_borrows =
        (amdf_memory_t**)(command->bindings + create_info->binding_count);
    command->umd_bindings =
        (amdf_xdna_umd_command_binding_t*)(command->memory_borrows +
                                           create_info->binding_count);
    command->control_bytes =
        (uint8_t*)(command->umd_bindings + create_info->binding_count);
    amdf_child_tracker_initialize(&command->submissions);
    for (uint32_t i = 0; i < create_info->binding_count; ++i) {
      const amdf_xdna_command_binding_t* source = &create_info->bindings[i];
      command->memory_borrows[i] = source->memory;
      command->bindings[i].device_address =
          source->memory->info.device_address + source->byte_offset;
      command->bindings[i].byte_length = source->byte_length;
      command->umd_bindings[i].memory =
          amdf_xdna_memory_get_umd(source->memory);
      command->umd_bindings[i].device_address =
          command->bindings[i].device_address;
      command->umd_bindings[i].byte_length = source->byte_length;
    }
    memcpy(command->control_bytes, create_info->control_bytes,
           (size_t)create_info->control_byte_length);
    const amdf_xdna_program_component_t* array_configuration =
        amdf_xdna_command_query_array_configuration(program);
    status = amdf_xdna_umd_command_create(
        amdf_xdna_device_get_umd(amdf_xdna_program_get_device(program)),
        array_configuration->bytes, array_configuration->byte_length,
        command->control_bytes, create_info->control_byte_length,
        create_info->binding_count != 0 ? command->umd_bindings : NULL,
        create_info->binding_count, &command->umd);
  }
  if (amdf_status_is_ok(status)) {
    command->info.type = AMDF_STRUCTURE_TYPE_XDNA_COMMAND_INFO;
    command->info.structure_size = sizeof(command->info);
    command->info.binding_count = create_info->binding_count;
    command->info.control_byte_length = create_info->control_byte_length;
    command->info.reset_epoch =
        amdf_xdna_program_get_info(program)->reset_epoch;
    *out_command = command;
  } else {
    while (registered_binding_count != 0) {
      --registered_binding_count;
      amdf_memory_unregister_child(
          create_info->bindings[registered_binding_count].memory);
    }
    if (program_registered) {
      amdf_xdna_program_unregister_command(program);
    }
    free(command);
  }
  return status;
}

amdf_status_t AMDF_CALL amdf_xdna_command_query_info(
    amdf_xdna_command_t* command, amdf_xdna_command_info_t* out_info) {
  if (command == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  const amdf_status_t status = amdf_structure_validate_output(
      out_info, AMDF_STRUCTURE_TYPE_XDNA_COMMAND_INFO,
      (uint32_t)sizeof(amdf_xdna_command_info_t));
  if (!amdf_status_is_ok(status)) {
    return status;
  }
  const uint32_t structure_size = out_info->structure_size;
  void* const next = out_info->next;
  *out_info = command->info;
  out_info->structure_size = structure_size;
  out_info->next = next;
  return AMDF_STATUS_OK;
}

amdf_xdna_program_t* amdf_xdna_command_get_program(
    amdf_xdna_command_t* command) {
  return command->program;
}

const void* amdf_xdna_command_get_control_bytes(
    const amdf_xdna_command_t* command, uint64_t* out_byte_length) {
  *out_byte_length = command->info.control_byte_length;
  return command->control_bytes;
}

const amdf_xdna_resolved_binding_t* amdf_xdna_command_get_bindings(
    const amdf_xdna_command_t* command, uint32_t* out_binding_count) {
  *out_binding_count = command->info.binding_count;
  return command->bindings;
}

const amdf_xdna_umd_command_t* amdf_xdna_command_get_umd(
    const amdf_xdna_command_t* command) {
  return command->umd;
}

amdf_status_t amdf_xdna_command_register_submission(
    amdf_xdna_command_t* command) {
  return amdf_child_tracker_register(&command->submissions);
}

void amdf_xdna_command_unregister_submission(amdf_xdna_command_t* command) {
  amdf_child_tracker_unregister(&command->submissions);
}

amdf_status_t AMDF_CALL
amdf_xdna_command_destroy(amdf_xdna_command_t* command) {
  if (command == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  if (amdf_child_tracker_count(&command->submissions) != 0) {
    return amdf_make_api_status(AMDF_STATUS_CODE_BUSY);
  }
  const amdf_status_t status = amdf_xdna_umd_command_destroy(command->umd);
  if (!amdf_status_is_ok(status)) {
    return status;
  }
  command->umd = NULL;
  for (uint32_t i = 0; i < command->info.binding_count; ++i) {
    amdf_memory_unregister_child(command->memory_borrows[i]);
  }
  amdf_xdna_program_unregister_command(command->program);
  free(command);
  return AMDF_STATUS_OK;
}
