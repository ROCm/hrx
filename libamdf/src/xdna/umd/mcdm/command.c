// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/xdna/umd/mcdm/command.h"

#include <stdlib.h>

#include "libamdf/src/xdna/umd/mcdm/device.h"
#include "libamdf/src/xdna/umd/mcdm/kernel_execution.h"

amdf_status_t amdf_xdna_umd_command_create(
    amdf_xdna_umd_device_t* device, const void* program_bytes,
    uint64_t program_byte_length, const void* control_bytes,
    uint64_t control_byte_length, const uint64_t* binding_addresses,
    uint32_t binding_count, amdf_xdna_umd_command_t** out_command) {
  *out_command = NULL;
  amdf_xdna_umd_command_t* command =
      (amdf_xdna_umd_command_t*)calloc(1, sizeof(*command));
  if (command == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_RESOURCE_EXHAUSTED);
  }
  const amdf_status_t status =
      amdf_windows_xdna_kernel_execution_prepare_command(
          device->kernel_execution, program_bytes, program_byte_length,
          control_bytes, control_byte_length, binding_addresses, binding_count,
          &command->native);
  if (amdf_status_is_ok(status)) {
    *out_command = command;
  } else {
    free(command);
  }
  return status;
}

amdf_status_t amdf_xdna_umd_command_destroy(amdf_xdna_umd_command_t* command) {
  amdf_windows_xdna_kernel_execution_release_command(&command->native);
  free(command);
  return AMDF_STATUS_OK;
}
