// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Fixed-binding XDNA command prepared for native queue publication.

#include "experimental/xdna/prepared_command.h"

#include <string.h>

#include "experimental/xdna/executable.h"

struct iree_hal_amd_xdna_prepared_command_t {
  // Host allocator owning this prepared command.
  iree_allocator_t host_allocator;
  // Retained executable owning the program and provider entry points.
  iree_hal_executable_t* executable;
  // Provider-owned command prepared from the executable entry.
  amdf_xdna_command_t* native_command;
  // Number of retained direct HAL buffers.
  iree_host_size_t retained_buffer_count;
  // Retained direct HAL buffers in executable binding order.
  iree_hal_buffer_t** retained_buffers;
};

static iree_hal_memory_access_t
iree_hal_amd_xdna_prepared_command_required_access(
    iree_hal_amd_xdna_elf_binding_access_t access) {
  iree_hal_memory_access_t required_access = IREE_HAL_MEMORY_ACCESS_NONE;
  if (iree_any_bit_set(access, IREE_HAL_AMD_XDNA_ELF_BINDING_ACCESS_READ)) {
    required_access |= IREE_HAL_MEMORY_ACCESS_READ;
  }
  if (iree_any_bit_set(access, IREE_HAL_AMD_XDNA_ELF_BINDING_ACCESS_WRITE)) {
    required_access |= IREE_HAL_MEMORY_ACCESS_WRITE;
  }
  return required_access;
}

static iree_hal_buffer_usage_t
iree_hal_amd_xdna_prepared_command_required_usage(
    iree_hal_amd_xdna_elf_binding_access_t access) {
  iree_hal_buffer_usage_t required_usage = IREE_HAL_BUFFER_USAGE_NONE;
  if (iree_any_bit_set(access, IREE_HAL_AMD_XDNA_ELF_BINDING_ACCESS_READ)) {
    required_usage |= IREE_HAL_BUFFER_USAGE_STORAGE_READ;
  }
  if (iree_any_bit_set(access, IREE_HAL_AMD_XDNA_ELF_BINDING_ACCESS_WRITE)) {
    required_usage |= IREE_HAL_BUFFER_USAGE_STORAGE_WRITE;
  }
  return required_usage;
}

static iree_hal_memory_type_t
iree_hal_amd_xdna_prepared_command_required_memory_type(
    const iree_hal_amd_xdna_elf_binding_record_t* contract) {
  iree_hal_memory_type_t required_memory_type = IREE_HAL_MEMORY_TYPE_NONE;
  if (iree_any_bit_set(contract->usage,
                       IREE_HAL_AMD_XDNA_ELF_BINDING_USAGE_DEVICE_VISIBLE)) {
    required_memory_type |= IREE_HAL_MEMORY_TYPE_DEVICE_VISIBLE;
  }
  const bool requires_host_visibility =
      contract->address_space ==
          IREE_HAL_AMD_XDNA_ELF_BINDING_ADDRESS_SPACE_HOST ||
      iree_any_bit_set(contract->usage,
                       IREE_HAL_AMD_XDNA_ELF_BINDING_USAGE_HOST_VISIBLE);
  if (requires_host_visibility) {
    required_memory_type |= IREE_HAL_MEMORY_TYPE_HOST_VISIBLE;
    if (iree_any_bit_set(contract->usage,
                         IREE_HAL_AMD_XDNA_ELF_BINDING_USAGE_COHERENT)) {
      required_memory_type |= IREE_HAL_MEMORY_TYPE_HOST_COHERENT;
    }
    if (iree_any_bit_set(contract->usage,
                         IREE_HAL_AMD_XDNA_ELF_BINDING_USAGE_CACHED)) {
      required_memory_type |= IREE_HAL_MEMORY_TYPE_HOST_CACHED;
    }
  }
  return required_memory_type;
}

static iree_status_t iree_hal_amd_xdna_prepared_command_validate_binding(
    const iree_hal_amd_xdna_elf_binding_record_t* contract,
    const iree_hal_amd_xdna_prepared_command_binding_t* binding,
    amdf_xdna_command_binding_t* out_native_binding) {
  *out_native_binding = (amdf_xdna_command_binding_t){0};
  if (binding->buffer_ref.buffer == NULL || binding->memory == NULL) {
    return iree_make_status(
        IREE_STATUS_INVALID_ARGUMENT,
        "XDNA prepared-command binding %u is not fully resolved",
        contract->binding_ordinal);
  }

  iree_device_size_t resource_byte_offset = 0;
  iree_device_size_t resource_byte_length = 0;
  IREE_RETURN_IF_ERROR(iree_hal_buffer_calculate_range(
      /*base_offset=*/0,
      iree_hal_buffer_byte_length(binding->buffer_ref.buffer),
      binding->buffer_ref.offset, binding->buffer_ref.length,
      &resource_byte_offset, &resource_byte_length));
  IREE_RETURN_IF_ERROR(iree_hal_buffer_validate_access(
      iree_hal_buffer_allowed_access(binding->buffer_ref.buffer),
      iree_hal_amd_xdna_prepared_command_required_access(contract->access)));
  IREE_RETURN_IF_ERROR(iree_hal_buffer_validate_usage(
      iree_hal_buffer_allowed_usage(binding->buffer_ref.buffer),
      iree_hal_amd_xdna_prepared_command_required_usage(contract->access)));
  IREE_RETURN_IF_ERROR(iree_hal_buffer_validate_memory_type(
      iree_hal_buffer_memory_type(binding->buffer_ref.buffer),
      iree_hal_amd_xdna_prepared_command_required_memory_type(contract)));

  if (resource_byte_offset < contract->minimum_byte_offset ||
      resource_byte_offset > contract->maximum_byte_offset) {
    return iree_make_status(
        IREE_STATUS_OUT_OF_RANGE,
        "XDNA binding %u offset %" PRIu64 " is outside [%" PRIu64 ", %" PRIu64
        "]",
        contract->binding_ordinal, (uint64_t)resource_byte_offset,
        contract->minimum_byte_offset, contract->maximum_byte_offset);
  }
  if (resource_byte_length < contract->minimum_byte_length) {
    return iree_make_status(
        IREE_STATUS_OUT_OF_RANGE,
        "XDNA binding %u length %" PRIu64 " is smaller than required %" PRIu64,
        contract->binding_ordinal, (uint64_t)resource_byte_length,
        contract->minimum_byte_length);
  }
  if ((binding->device_address & (contract->minimum_alignment - 1)) != 0) {
    return iree_make_status(IREE_STATUS_OUT_OF_RANGE,
                            "XDNA binding %u device address 0x%" PRIx64
                            " does not satisfy %" PRIu64 "-byte alignment",
                            contract->binding_ordinal, binding->device_address,
                            contract->minimum_alignment);
  }
  if ((resource_byte_length != 0 &&
       binding->device_address > UINT64_MAX - (resource_byte_length - 1)) ||
      binding->memory_byte_offset > UINT64_MAX - resource_byte_length) {
    return iree_make_status(IREE_STATUS_OUT_OF_RANGE,
                            "XDNA binding %u native range overflows",
                            contract->binding_ordinal);
  }

  *out_native_binding = (amdf_xdna_command_binding_t){
      .memory = binding->memory,
      .byte_offset = binding->memory_byte_offset,
      .byte_length = resource_byte_length,
  };
  return iree_ok_status();
}

iree_status_t iree_hal_amd_xdna_prepared_command_create(
    iree_hal_executable_t* executable, iree_hal_executable_function_t function,
    iree_host_size_t binding_count,
    const iree_hal_amd_xdna_prepared_command_binding_t* bindings,
    iree_allocator_t host_allocator,
    iree_hal_amd_xdna_prepared_command_t** out_prepared_command) {
  IREE_ASSERT_ARGUMENT(out_prepared_command);
  *out_prepared_command = NULL;
  if (!iree_hal_amd_xdna_executable_isa(executable)) {
    return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                            "an XDNA executable is required");
  }
  if ((binding_count != 0) != (bindings != NULL)) {
    return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                            "XDNA prepared-command bindings are inconsistent");
  }
  if (binding_count > UINT32_MAX) {
    return iree_make_status(IREE_STATUS_OUT_OF_RANGE,
                            "XDNA prepared-command binding count is too large");
  }

  iree_host_size_t retained_buffers_offset = 0;
  iree_host_size_t total_size = 0;
  iree_status_t status = IREE_STRUCT_LAYOUT(
      sizeof(iree_hal_amd_xdna_prepared_command_t), &total_size,
      IREE_STRUCT_FIELD_ALIGNED(binding_count, iree_hal_buffer_t*,
                                iree_alignof(iree_hal_buffer_t*),
                                &retained_buffers_offset));
  iree_hal_amd_xdna_prepared_command_t* prepared_command = NULL;
  if (iree_status_is_ok(status)) {
    status = iree_allocator_malloc(host_allocator, total_size,
                                   (void**)&prepared_command);
  }
  if (iree_status_is_ok(status)) {
    memset(prepared_command, 0, total_size);
    prepared_command->host_allocator = host_allocator;
    prepared_command->retained_buffer_count = binding_count;
    prepared_command->retained_buffers =
        (iree_hal_buffer_t**)((uint8_t*)prepared_command +
                              retained_buffers_offset);
  }

  iree_host_size_t native_bindings_size = 0;
  amdf_xdna_command_binding_t* native_bindings = NULL;
  if (iree_status_is_ok(status) && binding_count != 0) {
    status = IREE_STRUCT_LAYOUT(
        0, &native_bindings_size,
        IREE_STRUCT_FIELD(binding_count, amdf_xdna_command_binding_t, NULL));
  }
  if (iree_status_is_ok(status) && binding_count != 0) {
    status = iree_allocator_malloc(host_allocator, native_bindings_size,
                                   (void**)&native_bindings);
  }
  for (iree_host_size_t i = 0; iree_status_is_ok(status) && i < binding_count;
       ++i) {
    iree_hal_amd_xdna_elf_binding_record_t contract;
    status = iree_hal_amd_xdna_executable_query_binding(executable, function, i,
                                                        &contract);
    if (iree_status_is_ok(status)) {
      status = iree_hal_amd_xdna_prepared_command_validate_binding(
          &contract, &bindings[i], &native_bindings[i]);
    }
  }
  if (iree_status_is_ok(status)) {
    status = iree_hal_amd_xdna_executable_create_native_command(
        executable, function, (uint32_t)binding_count, native_bindings,
        &prepared_command->native_command);
  }
  iree_allocator_free(host_allocator, native_bindings);

  if (iree_status_is_ok(status)) {
    iree_hal_executable_retain(executable);
    prepared_command->executable = executable;
    for (iree_host_size_t i = 0; i < binding_count; ++i) {
      iree_hal_buffer_retain(bindings[i].buffer_ref.buffer);
      prepared_command->retained_buffers[i] = bindings[i].buffer_ref.buffer;
    }
    *out_prepared_command = prepared_command;
  } else {
    iree_allocator_free(host_allocator, prepared_command);
  }
  return status;
}

iree_status_t iree_hal_amd_xdna_prepared_command_destroy(
    iree_hal_amd_xdna_prepared_command_t* prepared_command) {
  if (prepared_command == NULL) return iree_ok_status();
  IREE_RETURN_IF_ERROR(iree_hal_amd_xdna_executable_destroy_native_command(
      prepared_command->executable, prepared_command->native_command));

  for (iree_host_size_t i = prepared_command->retained_buffer_count; i > 0;
       --i) {
    iree_hal_buffer_release(prepared_command->retained_buffers[i - 1]);
  }
  iree_hal_executable_release(prepared_command->executable);
  iree_allocator_free(prepared_command->host_allocator, prepared_command);
  return iree_ok_status();
}

amdf_xdna_command_t* iree_hal_amd_xdna_prepared_command_get_native_command(
    const iree_hal_amd_xdna_prepared_command_t* prepared_command) {
  return prepared_command != NULL ? prepared_command->native_command : NULL;
}
