// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/xdna/umd/drm/command.h"

#include <drm/amdxdna_accel.h>
#include <stdlib.h>

#include "libamdf/src/platform/linux/host_cache.h"
#include "libamdf/src/xdna/transaction.h"
#include "libamdf/src/xdna/umd/drm/device.h"
#include "libamdf/src/xdna/umd/drm/memory.h"

amdf_status_t amdf_xdna_umd_command_destroy(amdf_xdna_umd_command_t* command) {
  amdf_status_t status = amdf_linux_xdna_buffer_deinitialize(
      command->device->descriptor, &command->packet);
  if (amdf_status_is_ok(status)) {
    status = amdf_linux_xdna_buffer_deinitialize(command->device->descriptor,
                                                 &command->instruction);
  }
  if (amdf_status_is_ok(status)) free(command);
  return status;
}

static amdf_status_t amdf_linux_xdna_command_release_failed(
    amdf_linux_release_t* release) {
  return amdf_xdna_umd_command_destroy((amdf_xdna_umd_command_t*)release);
}

amdf_status_t amdf_xdna_umd_command_create(
    amdf_xdna_umd_device_t* device, const void* program_bytes,
    uint64_t program_byte_length, const void* control_bytes,
    uint64_t control_byte_length,
    const amdf_xdna_umd_command_binding_t* bindings, uint32_t binding_count,
    amdf_xdna_umd_command_t** out_command) {
  *out_command = NULL;
  amdf_xdna_umd_command_t* command = calloc(1, sizeof(*command));
  if (command == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_RESOURCE_EXHAUSTED);
  }
  command->device = device;
  command->failed_construction.destroy = amdf_linux_xdna_command_release_failed;
  // The common command boundary validated both headers and the composed limit.
  const size_t transaction_length =
      (size_t)(program_byte_length + control_byte_length - 16);
  const size_t allocation_length =
      (transaction_length + device->page_size - 1) & ~(device->page_size - 1);
  amdf_status_t status = amdf_linux_xdna_buffer_initialize(
      device->descriptor, AMDXDNA_BO_DEV, allocation_length, device->page_size,
      device->page_size, &device->heap, &command->instruction);
  uint64_t instruction_length = 0;
  if (amdf_status_is_ok(status)) {
    status = amdf_xdna_transaction_compose(
        program_bytes, program_byte_length, control_bytes, control_byte_length,
        command->instruction.host_pointer, allocation_length,
        &instruction_length);
  }
  if (amdf_status_is_ok(status)) {
    amdf_linux_host_cache_transfer(command->instruction.host_pointer,
                                   (size_t)instruction_length,
                                   device->cache_line_size);
    status = amdf_linux_xdna_buffer_initialize(
        device->descriptor, AMDXDNA_BO_CMD, device->page_size,
        device->page_size, device->page_size, NULL, &command->packet);
  }
  if (amdf_status_is_ok(status)) {
    command->arguments[0] = device->heap.handle;
    command->arguments[1] = device->bootstrap.handle;
    command->arguments[2] = command->instruction.handle;
    command->arguments[3] = command->packet.handle;
    command->argument_count = 4;
    uint64_t addresses[AMDF_XDNA_NPU5_MAXIMUM_BINDING_COUNT];
    for (uint32_t i = 0; i < binding_count; ++i) {
      addresses[i] = bindings[i].device_address;
      const uint32_t handle = bindings[i].memory->buffer.handle;
      uint32_t index = 0;
      while (index < command->argument_count &&
             command->arguments[index] != handle) {
        ++index;
      }
      if (index == command->argument_count) {
        command->arguments[command->argument_count++] = handle;
      }
    }
    amdf_xdna_npu5_ert_packet_build(
        command->instruction.device_address, (uint32_t)instruction_length,
        addresses, binding_count, command->packet.host_pointer);
    *out_command = command;
  } else {
    const amdf_status_t release_status = amdf_xdna_umd_command_destroy(command);
    if (!amdf_status_is_ok(release_status)) {
      amdf_linux_release_list_push(&device->failed_children,
                                   &command->failed_construction);
      status = release_status;
    }
  }
  return status;
}
