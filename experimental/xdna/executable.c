// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "experimental/xdna/executable.h"

#include <stddef.h>
#include <string.h>

#include "experimental/xdna/amdf_status.h"
#include "iree/hal/drivers/amd/xdna/image/aie2p/native_image.h"

// Immutable metadata and native program selected by one exported function.
typedef struct iree_hal_amd_xdna_executable_function_t {
  // Reflected HAL function information.
  iree_hal_executable_function_info_t info;
  // Provider-owned immutable program used by this function.
  amdf_xdna_program_t* program;
  // Invocation CONTROL transaction with entry-relative address patches.
  iree_const_byte_span_t control;
  // First global image binding owned by this function.
  uint32_t first_binding_ordinal;
} iree_hal_amd_xdna_executable_function_t;

typedef struct iree_hal_amd_xdna_executable_t {
  // Common HAL executable state.
  iree_hal_executable_t base;
  // Host allocator owning this executable.
  iree_allocator_t host_allocator;
  // Borrowed XDNA API table used to destroy owned programs.
  const amdf_xdna_api_t* xdna_api;
  // Qualified image retaining source and reflected metadata storage.
  iree_hal_amd_xdna_image_t* image;
  // Native transaction storage retained by function entries.
  iree_hal_amd_xdna_aie2p_native_image_t* native_image;
  // Number of distinct ARRAY programs in |programs|.
  iree_host_size_t program_count;
  // Provider-owned programs in native ARRAY ordinal order.
  amdf_xdna_program_t** programs;
  // Number of exported functions in |functions|.
  iree_host_size_t function_count;
  // Immutable function metadata in export ordinal order.
  iree_hal_amd_xdna_executable_function_t* functions;
} iree_hal_amd_xdna_executable_t;

static const iree_hal_executable_vtable_t iree_hal_amd_xdna_executable_vtable;

static iree_hal_amd_xdna_executable_t* iree_hal_amd_xdna_executable_cast(
    iree_hal_executable_t* base_value) {
  IREE_HAL_ASSERT_TYPE(base_value, &iree_hal_amd_xdna_executable_vtable);
  return (iree_hal_amd_xdna_executable_t*)base_value;
}

static iree_status_t iree_hal_amd_xdna_executable_validate_api(
    const amdf_xdna_api_t* xdna_api) {
  const size_t required_structure_size =
      offsetof(amdf_xdna_api_t, command_destroy) +
      sizeof(((amdf_xdna_api_t*)0)->command_destroy);
  if (xdna_api == NULL || xdna_api->structure_size < required_structure_size ||
      xdna_api->extension_version < AMDF_XDNA_EXTENSION_VERSION_1 ||
      xdna_api->program_create == NULL || xdna_api->program_destroy == NULL ||
      xdna_api->command_create == NULL || xdna_api->command_destroy == NULL) {
    return iree_make_status(
        IREE_STATUS_FAILED_PRECONDITION,
        "libamdf XDNA API does not implement the executable command contract");
  }
  return iree_ok_status();
}

static iree_status_t iree_hal_amd_xdna_executable_validate_bindings(
    const iree_hal_amd_xdna_image_t* image) {
  const iree_host_size_t function_count =
      iree_hal_amd_xdna_image_entry_count(image);
  for (iree_host_size_t i = 0; i < function_count; ++i) {
    const iree_hal_amd_xdna_elf_entry_record_t* entry =
        iree_hal_amd_xdna_image_entry(image, i);
    if (entry->binding_count > UINT16_MAX) {
      return iree_make_status(IREE_STATUS_OUT_OF_RANGE,
                              "XDNA entry[%" PRIhsz
                              "] declares %u bindings, exceeding the HAL "
                              "reflection limit of %u",
                              i, entry->binding_count, (uint32_t)UINT16_MAX);
    }
    const uint64_t binding_end =
        (uint64_t)entry->first_binding_ordinal + entry->binding_count;
    for (uint64_t j = entry->first_binding_ordinal; j < binding_end; ++j) {
      const iree_hal_amd_xdna_elf_binding_record_t* binding =
          iree_hal_amd_xdna_image_binding(image, (iree_host_size_t)j);
      if (binding == NULL ||
          binding->kind != IREE_HAL_AMD_XDNA_ELF_BINDING_KIND_BUFFER) {
        return iree_make_status(
            IREE_STATUS_UNIMPLEMENTED,
            "XDNA entry[%" PRIhsz
            "] uses scalar invocation bindings not representable by the "
            "libamdf fixed-binding command ABI",
            i);
      }
    }
  }
  return iree_ok_status();
}

static iree_status_t iree_hal_amd_xdna_executable_create_programs(
    amdf_device_t* device, amdf_xdna_program_flags_t required_program_flags,
    iree_hal_amd_xdna_executable_t* executable) {
  const iree_hal_amd_xdna_elf_abi_note_t* abi_note =
      iree_hal_amd_xdna_image_abi_note(executable->image);
  const amdf_xdna_program_footprint_t footprint = {
      .coordinate_mode = AMDF_XDNA_COORDINATE_MODE_CONTEXT_RELATIVE,
      .column_origin = 0,
      .column_count = abi_note->context_column_count,
      .row_count = abi_note->context_row_count,
  };
  for (iree_host_size_t i = 0; i < executable->program_count; ++i) {
    iree_const_byte_span_t transaction = iree_const_byte_span_empty();
    IREE_RETURN_IF_ERROR(
        iree_hal_amd_xdna_aie2p_native_image_query_array_configuration(
            executable->native_image, i, &transaction));
    const amdf_xdna_program_component_t component = {
        .kind = AMDF_XDNA_PROGRAM_COMPONENT_ARRAY_CONFIGURATION,
        .bytes = transaction.data,
        .byte_length = transaction.data_length,
    };
    const amdf_xdna_program_create_info_t create_info = {
        .type = AMDF_STRUCTURE_TYPE_XDNA_PROGRAM_CREATE_INFO,
        .structure_size = sizeof(create_info),
        .component_count = 1,
        .required_flags = required_program_flags,
        .components = &component,
        .footprint = footprint,
    };
    const amdf_status_t amdf_status = executable->xdna_api->program_create(
        device, &create_info, &executable->programs[i]);
    if (!amdf_status_is_ok(amdf_status)) {
      return IREE_HAL_AMD_STATUS_FROM_AMDF(amdf_status, "xdna.program_create");
    }
    if (executable->programs[i] == NULL) {
      return iree_make_status(
          IREE_STATUS_FAILED_PRECONDITION,
          "libamdf XDNA program creation succeeded without a program");
    }
  }
  return iree_ok_status();
}

static iree_status_t iree_hal_amd_xdna_executable_initialize_functions(
    iree_hal_amd_xdna_executable_t* executable) {
  for (iree_host_size_t i = 0; i < executable->function_count; ++i) {
    const iree_hal_amd_xdna_elf_entry_record_t* image_entry =
        iree_hal_amd_xdna_image_entry(executable->image, i);
    iree_hal_amd_xdna_aie2p_native_entry_t native_entry;
    IREE_RETURN_IF_ERROR(iree_hal_amd_xdna_aie2p_native_image_query_entry(
        executable->native_image, i, &native_entry));
    if (native_entry.array_ordinal >= executable->program_count) {
      return iree_make_status(
          IREE_STATUS_FAILED_PRECONDITION,
          "XDNA entry[%" PRIhsz "] selects an invalid native ARRAY ordinal", i);
    }
    executable->functions[i] = (iree_hal_amd_xdna_executable_function_t){
        .info =
            {
                .name =
                    iree_hal_amd_xdna_image_entry_name(executable->image, i),
                .binding_count = (uint16_t)image_entry->binding_count,
                .parameter_count = (uint16_t)image_entry->binding_count,
                .maximum_workgroup_invocations = 1,
                .workgroup_size = {1, 1, 1},
            },
        .program = executable->programs[native_entry.array_ordinal],
        .control = native_entry.control,
        .first_binding_ordinal = image_entry->first_binding_ordinal,
    };
  }
  return iree_ok_status();
}

iree_status_t iree_hal_amd_xdna_executable_create(
    const amdf_xdna_api_t* xdna_api, amdf_device_t* device,
    const iree_hal_queue_family_t* queue_family,
    iree_byte_sequence_t* source_sequence,
    const iree_hal_amd_xdna_aie2p_target_t* target,
    amdf_xdna_program_flags_t required_program_flags,
    iree_allocator_t host_allocator, iree_hal_executable_t** out_executable) {
  IREE_ASSERT_ARGUMENT(queue_family);
  IREE_ASSERT_ARGUMENT(out_executable);
  *out_executable = NULL;
  if (device == NULL || source_sequence == NULL) {
    return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                            "XDNA device and image source are required");
  }
  IREE_RETURN_IF_ERROR(iree_hal_amd_xdna_executable_validate_api(xdna_api));

  iree_hal_amd_xdna_image_target_t image_target;
  IREE_RETURN_IF_ERROR(iree_hal_amd_xdna_aie2p_target_initialize_image_target(
      target, &image_target));
  iree_hal_amd_xdna_image_t* image = NULL;
  IREE_RETURN_IF_ERROR(iree_hal_amd_xdna_image_create(
      source_sequence, &image_target, host_allocator, &image));

  iree_hal_amd_xdna_aie2p_native_image_t* native_image = NULL;
  iree_status_t status = iree_hal_amd_xdna_aie2p_native_image_create(
      image, target, host_allocator, &native_image);
  if (iree_status_is_ok(status)) {
    status = iree_hal_amd_xdna_executable_validate_bindings(image);
  }

  const iree_host_size_t program_count =
      iree_status_is_ok(status)
          ? iree_hal_amd_xdna_aie2p_native_image_array_count(native_image)
          : 0;
  const iree_host_size_t function_count =
      iree_status_is_ok(status)
          ? iree_hal_amd_xdna_aie2p_native_image_entry_count(native_image)
          : 0;
  iree_host_size_t programs_offset = 0;
  iree_host_size_t functions_offset = 0;
  iree_host_size_t total_size = 0;
  if (iree_status_is_ok(status)) {
    status = IREE_STRUCT_LAYOUT(
        sizeof(iree_hal_amd_xdna_executable_t), &total_size,
        IREE_STRUCT_FIELD_ALIGNED(program_count, amdf_xdna_program_t*,
                                  iree_alignof(amdf_xdna_program_t*),
                                  &programs_offset),
        IREE_STRUCT_FIELD_ALIGNED(
            function_count, iree_hal_amd_xdna_executable_function_t,
            iree_alignof(iree_hal_amd_xdna_executable_function_t),
            &functions_offset));
  }

  iree_hal_amd_xdna_executable_t* executable = NULL;
  if (iree_status_is_ok(status)) {
    status =
        iree_allocator_malloc(host_allocator, total_size, (void**)&executable);
  }
  if (iree_status_is_ok(status)) {
    memset(executable, 0, total_size);
    iree_hal_executable_initialize(
        queue_family, &iree_hal_amd_xdna_executable_vtable, &executable->base);
    executable->host_allocator = host_allocator;
    executable->xdna_api = xdna_api;
    executable->image = image;
    executable->native_image = native_image;
    executable->program_count = program_count;
    executable->programs =
        (amdf_xdna_program_t**)((uint8_t*)executable + programs_offset);
    executable->function_count = function_count;
    executable->functions =
        (iree_hal_amd_xdna_executable_function_t*)((uint8_t*)executable +
                                                   functions_offset);
    image = NULL;
    native_image = NULL;
    status = iree_hal_amd_xdna_executable_create_programs(
        device, required_program_flags, executable);
  }
  if (iree_status_is_ok(status)) {
    status = iree_hal_amd_xdna_executable_initialize_functions(executable);
  }

  iree_hal_amd_xdna_aie2p_native_image_destroy(native_image);
  iree_hal_amd_xdna_image_destroy(image);
  if (iree_status_is_ok(status)) {
    *out_executable = (iree_hal_executable_t*)executable;
  } else if (executable != NULL) {
    iree_hal_executable_destroy((iree_hal_executable_t*)executable);
  }
  return status;
}

bool iree_hal_amd_xdna_executable_isa(iree_hal_executable_t* executable) {
  return iree_hal_resource_is((const iree_hal_resource_t*)executable,
                              &iree_hal_amd_xdna_executable_vtable);
}

iree_status_t iree_hal_amd_xdna_executable_query_entry(
    iree_hal_executable_t* base_executable,
    iree_hal_executable_function_t function,
    iree_hal_amd_xdna_executable_entry_t* out_entry) {
  IREE_ASSERT_ARGUMENT(out_entry);
  *out_entry = (iree_hal_amd_xdna_executable_entry_t){0};
  iree_hal_amd_xdna_executable_t* executable =
      iree_hal_amd_xdna_executable_cast(base_executable);
  if (!iree_hal_executable_function_is_index_in_range(
          function, executable->function_count)) {
    return iree_make_status(IREE_STATUS_OUT_OF_RANGE,
                            "function id %" PRIu64
                            " out of range; executable has %" PRIhsz " exports",
                            function.value, executable->function_count);
  }
  const iree_hal_amd_xdna_executable_function_t* executable_function =
      &executable->functions[iree_hal_executable_function_index(function)];
  *out_entry = (iree_hal_amd_xdna_executable_entry_t){
      .program = executable_function->program,
      .control = executable_function->control,
      .binding_count = executable_function->info.binding_count,
  };
  return iree_ok_status();
}

iree_status_t iree_hal_amd_xdna_executable_query_binding(
    iree_hal_executable_t* base_executable,
    iree_hal_executable_function_t function, iree_host_size_t binding_ordinal,
    iree_hal_amd_xdna_elf_binding_record_t* out_binding) {
  IREE_ASSERT_ARGUMENT(out_binding);
  memset(out_binding, 0, sizeof(*out_binding));
  iree_hal_amd_xdna_executable_t* executable =
      iree_hal_amd_xdna_executable_cast(base_executable);
  if (!iree_hal_executable_function_is_index_in_range(
          function, executable->function_count)) {
    return iree_make_status(IREE_STATUS_OUT_OF_RANGE,
                            "XDNA executable function is out of range");
  }
  const iree_hal_amd_xdna_executable_function_t* executable_function =
      &executable->functions[iree_hal_executable_function_index(function)];
  if (binding_ordinal >= executable_function->info.binding_count) {
    return iree_make_status(IREE_STATUS_OUT_OF_RANGE,
                            "XDNA executable binding is out of range");
  }
  const iree_host_size_t image_binding_ordinal =
      executable_function->first_binding_ordinal + binding_ordinal;
  const iree_hal_amd_xdna_elf_binding_record_t* binding =
      iree_hal_amd_xdna_image_binding(executable->image, image_binding_ordinal);
  if (binding == NULL) {
    return iree_make_status(
        IREE_STATUS_FAILED_PRECONDITION,
        "qualified XDNA executable binding metadata is inconsistent");
  }
  *out_binding = *binding;
  return iree_ok_status();
}

iree_status_t iree_hal_amd_xdna_executable_create_native_command(
    iree_hal_executable_t* base_executable,
    iree_hal_executable_function_t function, uint32_t binding_count,
    const amdf_xdna_command_binding_t* bindings,
    amdf_xdna_command_t** out_command) {
  IREE_ASSERT_ARGUMENT(out_command);
  *out_command = NULL;
  if ((binding_count != 0) != (bindings != NULL)) {
    return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                            "XDNA command binding array is inconsistent");
  }
  iree_hal_amd_xdna_executable_t* executable =
      iree_hal_amd_xdna_executable_cast(base_executable);
  if (!iree_hal_executable_function_is_index_in_range(
          function, executable->function_count)) {
    return iree_make_status(IREE_STATUS_OUT_OF_RANGE,
                            "XDNA executable function is out of range");
  }
  const iree_hal_amd_xdna_executable_function_t* executable_function =
      &executable->functions[iree_hal_executable_function_index(function)];
  if (binding_count != executable_function->info.binding_count) {
    return iree_make_status(
        IREE_STATUS_INVALID_ARGUMENT,
        "XDNA function requires %u bindings but command provides %u",
        executable_function->info.binding_count, binding_count);
  }

  const amdf_xdna_command_create_info_t create_info = {
      .type = AMDF_STRUCTURE_TYPE_XDNA_COMMAND_CREATE_INFO,
      .structure_size = sizeof(create_info),
      .binding_count = binding_count,
      .control_bytes = executable_function->control.data,
      .control_byte_length = executable_function->control.data_length,
      .bindings = bindings,
  };
  const amdf_status_t amdf_status = executable->xdna_api->command_create(
      executable_function->program, &create_info, out_command);
  if (!amdf_status_is_ok(amdf_status)) {
    return IREE_HAL_AMD_STATUS_FROM_AMDF(amdf_status, "xdna.command_create");
  }
  if (*out_command == NULL) {
    return iree_make_status(
        IREE_STATUS_FAILED_PRECONDITION,
        "libamdf XDNA command creation succeeded without a command");
  }
  return iree_ok_status();
}

iree_status_t iree_hal_amd_xdna_executable_destroy_native_command(
    iree_hal_executable_t* base_executable, amdf_xdna_command_t* command) {
  if (command == NULL) {
    return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                            "XDNA command is required");
  }
  iree_hal_amd_xdna_executable_t* executable =
      iree_hal_amd_xdna_executable_cast(base_executable);
  const amdf_status_t amdf_status =
      executable->xdna_api->command_destroy(command);
  if (!amdf_status_is_ok(amdf_status)) {
    return IREE_HAL_AMD_STATUS_FROM_AMDF(amdf_status, "xdna.command_destroy");
  }
  return iree_ok_status();
}

static void iree_hal_amd_xdna_executable_destroy(
    iree_hal_executable_t* base_executable) {
  iree_hal_amd_xdna_executable_t* executable =
      iree_hal_amd_xdna_executable_cast(base_executable);
  const iree_allocator_t host_allocator = executable->host_allocator;
  for (iree_host_size_t i = 0; i < executable->program_count; ++i) {
    if (executable->programs[i] == NULL) continue;
    const amdf_status_t status =
        executable->xdna_api->program_destroy(executable->programs[i]);
    if (IREE_UNLIKELY(!amdf_status_is_ok(status))) {
      // Every prepared command retains the executable through its owning HAL
      // command buffer. Reaching final executable release with a live provider
      // command is an ownership invariant violation that cannot be recovered
      // after the HAL resource has begun destruction.
      iree_abort();
    }
  }
  iree_hal_amd_xdna_aie2p_native_image_destroy(executable->native_image);
  iree_hal_amd_xdna_image_destroy(executable->image);
  iree_allocator_free(host_allocator, executable);
}

static iree_host_size_t iree_hal_amd_xdna_executable_function_count(
    iree_hal_executable_t* base_executable) {
  return iree_hal_amd_xdna_executable_cast(base_executable)->function_count;
}

static iree_status_t iree_hal_amd_xdna_executable_function_info(
    iree_hal_executable_t* base_executable,
    iree_hal_executable_function_t function,
    iree_hal_executable_function_info_t* out_info) {
  iree_hal_amd_xdna_executable_t* executable =
      iree_hal_amd_xdna_executable_cast(base_executable);
  if (!iree_hal_executable_function_is_index_in_range(
          function, executable->function_count)) {
    return iree_make_status(IREE_STATUS_OUT_OF_RANGE,
                            "XDNA executable function is out of range");
  }
  *out_info =
      executable->functions[iree_hal_executable_function_index(function)].info;
  return iree_ok_status();
}

static iree_status_t iree_hal_amd_xdna_executable_function_parameters(
    iree_hal_executable_t* base_executable,
    iree_hal_executable_function_t function, iree_host_size_t capacity,
    iree_hal_executable_function_parameter_t* out_parameters) {
  iree_hal_amd_xdna_executable_t* executable =
      iree_hal_amd_xdna_executable_cast(base_executable);
  if (!iree_hal_executable_function_is_index_in_range(
          function, executable->function_count)) {
    return iree_make_status(IREE_STATUS_OUT_OF_RANGE,
                            "XDNA executable function is out of range");
  }
  const iree_hal_amd_xdna_executable_function_t* executable_function =
      &executable->functions[iree_hal_executable_function_index(function)];
  const iree_host_size_t copy_count =
      iree_min(capacity, executable_function->info.parameter_count);
  for (iree_host_size_t i = 0; i < copy_count; ++i) {
    out_parameters[i] = (iree_hal_executable_function_parameter_t){
        .type = IREE_HAL_EXECUTABLE_FUNCTION_PARAMETER_TYPE_BINDING,
        .offset = (uint16_t)i,
    };
  }
  return iree_ok_status();
}

static iree_status_t iree_hal_amd_xdna_executable_lookup_function_by_name(
    iree_hal_executable_t* base_executable, iree_string_view_t name,
    iree_hal_executable_function_t* out_function) {
  iree_hal_amd_xdna_executable_t* executable =
      iree_hal_amd_xdna_executable_cast(base_executable);
  for (iree_host_size_t i = 0; i < executable->function_count; ++i) {
    if (iree_string_view_equal(executable->functions[i].info.name, name)) {
      *out_function = iree_hal_executable_function_from_index((uint32_t)i);
      return iree_ok_status();
    }
  }
  return iree_make_status(IREE_STATUS_NOT_FOUND,
                          "XDNA executable function '%.*s' was not found",
                          (int)name.size, name.data);
}

static iree_status_t iree_hal_amd_xdna_executable_try_lookup_global_by_name(
    iree_hal_executable_t* base_executable, iree_string_view_t name,
    bool* out_found, iree_hal_executable_global_t* out_global) {
  (void)base_executable;
  (void)name;
  *out_found = false;
  *out_global = iree_hal_executable_global_invalid();
  return iree_ok_status();
}

static iree_status_t iree_hal_amd_xdna_executable_global_info(
    iree_hal_executable_t* base_executable, iree_hal_executable_global_t global,
    iree_hal_executable_global_info_t* out_info) {
  (void)base_executable;
  (void)global;
  memset(out_info, 0, sizeof(*out_info));
  return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                          "invalid XDNA executable global");
}

static iree_status_t iree_hal_amd_xdna_executable_global_buffer(
    iree_hal_executable_t* base_executable, iree_hal_executable_global_t global,
    iree_hal_buffer_t** out_buffer) {
  (void)base_executable;
  (void)global;
  *out_buffer = NULL;
  return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                          "invalid XDNA executable global");
}

static const iree_hal_executable_vtable_t iree_hal_amd_xdna_executable_vtable =
    {
        .destroy = iree_hal_amd_xdna_executable_destroy,
        .function_count = iree_hal_amd_xdna_executable_function_count,
        .function_info = iree_hal_amd_xdna_executable_function_info,
        .function_parameters = iree_hal_amd_xdna_executable_function_parameters,
        .lookup_function_by_name =
            iree_hal_amd_xdna_executable_lookup_function_by_name,
        .try_lookup_global_by_name =
            iree_hal_amd_xdna_executable_try_lookup_global_by_name,
        .global_info = iree_hal_amd_xdna_executable_global_info,
        .global_buffer = iree_hal_amd_xdna_executable_global_buffer,
};
