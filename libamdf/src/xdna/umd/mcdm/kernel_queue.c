// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/xdna/umd/kernel_queue.h"

#include <stdlib.h>

#include "libamdf/src/xdna/umd/mcdm/command.h"
#include "libamdf/src/xdna/umd/mcdm/device.h"
#include "libamdf/src/xdna/umd/mcdm/kernel_execution.h"

struct amdf_xdna_umd_kernel_queue_t {
  // Device execution state exclusively leased by this public queue.
  amdf_windows_xdna_kernel_execution_t* execution;
};

amdf_status_t amdf_xdna_umd_kernel_queue_create(
    amdf_xdna_umd_device_t* device, amdf_xdna_umd_kernel_queue_t** out_queue) {
  *out_queue = NULL;
  amdf_xdna_umd_kernel_queue_t* queue =
      (amdf_xdna_umd_kernel_queue_t*)calloc(1, sizeof(*queue));
  if (queue == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_RESOURCE_EXHAUSTED);
  }
  const amdf_status_t status = amdf_windows_xdna_kernel_execution_acquire_queue(
      device->kernel_execution);
  if (amdf_status_is_ok(status)) {
    queue->execution = device->kernel_execution;
    *out_queue = queue;
  } else {
    free(queue);
  }
  return status;
}

amdf_status_t amdf_xdna_umd_kernel_queue_submit(
    amdf_xdna_umd_kernel_queue_t* queue, const amdf_xdna_umd_command_t* command,
    uint64_t* out_native_submission) {
  return amdf_windows_xdna_kernel_execution_submit(
      queue->execution, &command->native, out_native_submission);
}

uint64_t amdf_xdna_umd_kernel_queue_query_progress(
    const amdf_xdna_umd_kernel_queue_t* queue) {
  const uint64_t progress =
      amdf_windows_xdna_kernel_execution_query_progress(queue->execution);
  MemoryBarrier();
  return progress;
}

amdf_status_t amdf_xdna_umd_kernel_queue_wait(
    amdf_xdna_umd_kernel_queue_t* queue, uint64_t native_submission,
    uint64_t timeout_nanoseconds, uint64_t poll_duration_nanoseconds) {
  return amdf_windows_xdna_kernel_execution_wait(
      queue->execution, native_submission, timeout_nanoseconds,
      poll_duration_nanoseconds);
}

amdf_status_t amdf_xdna_umd_kernel_queue_query_terminal_status(
    const amdf_xdna_umd_kernel_queue_t* queue) {
  return amdf_windows_xdna_kernel_execution_query_terminal_status(
      queue->execution);
}

amdf_status_t amdf_xdna_umd_kernel_queue_destroy(
    amdf_xdna_umd_kernel_queue_t* queue) {
  amdf_windows_xdna_kernel_execution_release_queue(queue->execution);
  free(queue);
  return AMDF_STATUS_OK;
}
