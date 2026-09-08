// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/xdna/umd/kernel_queue.h"

#include <drm/amdxdna_accel.h>
#include <stdlib.h>
#include <sys/ioctl.h>

#include "libamdf/src/atomics.h"
#include "libamdf/src/platform/linux/file.h"
#include "libamdf/src/platform/linux/host_cache.h"
#include "libamdf/src/platform/wait.h"
#include "libamdf/src/xdna/target/npu5/bootstrap.h"
#include "libamdf/src/xdna/target/npu5/ert_packet.h"
#include "libamdf/src/xdna/umd/drm/command.h"
#include "libamdf/src/xdna/umd/drm/device.h"

struct amdf_xdna_umd_kernel_queue_t {
  // Device execution context exclusively leased by this public queue.
  amdf_xdna_umd_device_t* device;
  // Greatest nonzero native point confirmed by timeline wait.
  amdf_atomic_uint64_t progress;
};

static void amdf_linux_xdna_device_record_failure(
    amdf_xdna_umd_device_t* device, amdf_status_t failure) {
  uint64_t expected = AMDF_STATUS_OK;
  amdf_atomic_uint64_compare_exchange_acq_rel(&device->terminal_status,
                                              &expected, failure);
}

static amdf_status_t amdf_linux_xdna_command_query_result(
    const amdf_xdna_umd_command_t* command) {
  // The caller has native fence proof and holds the command's lifetime borrow.
  // ERT state by itself is never proof that the kernel has stopped using it.
  const uint32_t state =
      __atomic_load_n((const uint32_t*)command->packet.host_pointer,
                      __ATOMIC_ACQUIRE) &
      0xf;
  if (state == 0) return amdf_make_api_status(AMDF_STATUS_CODE_INTERNAL);
  return state == AMDF_XDNA_NPU5_ERT_STATE_COMPLETED
             ? AMDF_STATUS_OK
             : amdf_make_status(AMDF_STATUS_DOMAIN_FIRMWARE, state);
}

static amdf_status_t amdf_linux_xdna_command_submit(
    amdf_xdna_umd_device_t* device, const amdf_xdna_umd_command_t* command,
    uint64_t* out_sequence) {
  if (device->last_native_sequence == UINT64_MAX) {
    return amdf_make_api_status(AMDF_STATUS_CODE_RESOURCE_EXHAUSTED);
  }
  // Reusing a prepared command republishes its state, not its immutable
  // payload.
  __atomic_store_n((uint32_t*)command->packet.host_pointer,
                   AMDF_XDNA_NPU5_ERT_HEADER_NEW, __ATOMIC_RELEASE);
  amdf_linux_host_cache_transfer(command->packet.host_pointer,
                                 AMDF_XDNA_NPU5_ERT_PACKET_SIZE,
                                 device->cache_line_size);
  struct amdxdna_drm_exec_cmd submit = {
      .hwctx = device->context,
      .type = AMDXDNA_CMD_SUBMIT_EXEC_BUF,
      .cmd_handles = command->packet.handle,
      .args = (uintptr_t)command->arguments,
      .cmd_count = 1,
      .arg_count = command->argument_count,
  };
  // No operation after native acceptance may turn this into a rejected submit.
  if (ioctl(device->descriptor, DRM_IOCTL_AMDXDNA_EXEC_CMD, &submit) != 0) {
    return amdf_linux_error(errno);
  }
  device->last_native_sequence = submit.seq;
  *out_sequence = submit.seq;
  return AMDF_STATUS_OK;
}

static amdf_status_t amdf_linux_xdna_timeline_wait(
    amdf_xdna_umd_device_t* device, uint64_t sequence,
    const amdf_wait_deadline_t* deadline) {
  for (;;) {
    amdf_wait_budget_t remaining;
    const amdf_status_t status =
        amdf_wait_deadline_query_remaining(deadline, &remaining);
    if (!amdf_status_is_ok(status)) return status;
    struct drm_syncobj_timeline_wait wait = {
        .handles = (uintptr_t)&device->completion_syncobj,
        .points = (uintptr_t)&sequence,
        .timeout_nsec =
            remaining.poll != 0 || remaining.timeout == 0
                ? 0
                : (deadline->timeout > INT64_MAX ? INT64_MAX
                                                 : (int64_t)deadline->timeout),
        .count_handles = 1,
        .flags = DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL |
                 DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT,
    };
    if (ioctl(device->descriptor, DRM_IOCTL_SYNCOBJ_TIMELINE_WAIT, &wait) ==
        0) {
      return AMDF_STATUS_OK;
    }
    const int error = errno;
    if (error != ETIME && error != EINTR) return amdf_linux_error(error);
    // Retry interrupted waits and explicit active polling with the original
    // absolute deadline. A timeout never authorizes releasing accepted work.
    if (remaining.timeout == 0) {
      return amdf_make_api_status(AMDF_STATUS_CODE_DEADLINE_EXCEEDED);
    }
    if (error == ETIME && remaining.poll != 0) amdf_platform_wait_yield();
  }
}

static amdf_status_t amdf_linux_xdna_kernel_queue_initialize(
    amdf_xdna_umd_device_t* device) {
  const void* transaction;
  size_t transaction_length;
  amdf_xdna_npu5_bootstrap_query_transaction(&transaction, &transaction_length);
  amdf_status_t status = amdf_xdna_umd_command_create(
      device, transaction, transaction_length, transaction, transaction_length,
      NULL, 0, &device->bootstrap_command);
  if (!amdf_status_is_ok(status)) return status;

  struct {
    // One CU record in the flexible-array UAPI payload.
    uint16_t count;
    // Reserved UAPI header fields, required to be zero.
    uint16_t reserved[3];
    // CU zero's context-lifetime interpreter PDI and function code.
    struct amdxdna_cu_config cu;
  } configuration = {.count = 1, .cu = {.cu_bo = device->bootstrap.handle}};
  struct amdxdna_drm_config_hwctx configure = {
      .handle = device->context,
      .param_type = DRM_AMDXDNA_HWCTX_CONFIG_CU,
      .param_val = (uintptr_t)&configuration,
      .param_val_size = sizeof(configuration),
  };
  // The kernel retains CU configuration even if admission reports an error.
  // All failures from this point are terminal; destruction still owns cleanup.
  if (ioctl(device->descriptor, DRM_IOCTL_AMDXDNA_CONFIG_HWCTX, &configure) !=
      0) {
    status = amdf_linux_error(errno);
  }
  uint64_t sequence = 0;
  if (amdf_status_is_ok(status)) {
    status = amdf_linux_xdna_command_submit(device, device->bootstrap_command,
                                            &sequence);
  }
  if (amdf_status_is_ok(status)) {
    const amdf_wait_deadline_t deadline = {AMDF_TIMEOUT_INFINITE, 0};
    status = amdf_linux_xdna_timeline_wait(device, sequence, &deadline);
  }
  if (amdf_status_is_ok(status)) {
    status = amdf_linux_xdna_command_query_result(device->bootstrap_command);
  }
  if (!amdf_status_is_ok(status))
    amdf_linux_xdna_device_record_failure(device, status);
  return status;
}

amdf_status_t amdf_xdna_umd_kernel_queue_create(
    amdf_xdna_umd_device_t* device, amdf_xdna_umd_kernel_queue_t** out_queue) {
  *out_queue = NULL;
  amdf_xdna_umd_kernel_queue_t* queue = calloc(1, sizeof(*queue));
  if (queue == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_RESOURCE_EXHAUSTED);
  }
  uint32_t expected = 0;
  if (!amdf_atomic_uint32_compare_exchange_acq_rel(&device->queue_leased,
                                                   &expected, 1)) {
    free(queue);
    return amdf_make_api_status(AMDF_STATUS_CODE_BUSY);
  }
  amdf_status_t status =
      amdf_atomic_uint64_load_acquire(&device->terminal_status);
  if (amdf_status_is_ok(status) && device->bootstrap_command == NULL) {
    // DRM point zero aliases the current fence. Retire it under this exclusive
    // initialization lease so every public submission has a stable nonzero
    // point.
    status = amdf_linux_xdna_kernel_queue_initialize(device);
  }
  if (amdf_status_is_ok(status)) {
    queue->device = device;
    *out_queue = queue;
  } else {
    amdf_atomic_uint32_store_release(&device->queue_leased, 0);
    free(queue);
  }
  return status;
}

amdf_status_t amdf_xdna_umd_kernel_queue_submit(
    amdf_xdna_umd_kernel_queue_t* queue, const amdf_xdna_umd_command_t* command,
    uint64_t* out_native_submission) {
  const amdf_status_t status =
      amdf_atomic_uint64_load_acquire(&queue->device->terminal_status);
  if (!amdf_status_is_ok(status)) return status;
  return amdf_linux_xdna_command_submit(queue->device, command,
                                        out_native_submission);
}

uint64_t amdf_xdna_umd_kernel_queue_query_progress(
    const amdf_xdna_umd_kernel_queue_t* queue) {
  return amdf_atomic_uint64_load_acquire(&queue->progress);
}

void amdf_xdna_umd_kernel_queue_retire_command(
    amdf_xdna_umd_kernel_queue_t* queue,
    const amdf_xdna_umd_command_t* command) {
  const amdf_status_t status = amdf_linux_xdna_command_query_result(command);
  if (!amdf_status_is_ok(status)) {
    amdf_linux_xdna_device_record_failure(queue->device, status);
  }
}

amdf_status_t amdf_xdna_umd_kernel_queue_query_terminal_status(
    const amdf_xdna_umd_kernel_queue_t* queue) {
  return amdf_atomic_uint64_load_acquire(&queue->device->terminal_status);
}

amdf_status_t amdf_xdna_umd_kernel_queue_wait(
    amdf_xdna_umd_kernel_queue_t* queue, uint64_t native_submission,
    const amdf_wait_deadline_t* deadline) {
  if (amdf_atomic_uint64_load_acquire(&queue->progress) >= native_submission) {
    return AMDF_STATUS_OK;
  }
  const amdf_status_t status =
      amdf_linux_xdna_timeline_wait(queue->device, native_submission, deadline);
  if (amdf_status_is_ok(status)) {
    uint64_t progress = amdf_atomic_uint64_load_acquire(&queue->progress);
    while (progress < native_submission &&
           !amdf_atomic_uint64_compare_exchange_acq_rel(
               &queue->progress, &progress, native_submission)) {
    }
  }
  return status;
}

amdf_status_t amdf_xdna_umd_kernel_queue_destroy(
    amdf_xdna_umd_kernel_queue_t* queue) {
  amdf_atomic_uint32_store_release(&queue->device->queue_leased, 0);
  free(queue);
  return AMDF_STATUS_OK;
}
