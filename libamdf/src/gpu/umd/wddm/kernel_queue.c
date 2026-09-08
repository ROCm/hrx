// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/gpu/umd/kernel_queue.h"

#include <stdlib.h>

#include "libamdf/src/gpu/umd/wddm/device.h"

enum {
  AMDF_GPU_WINDOWS_WAIT_SIGNALED = 0,
};

struct amdf_gpu_umd_kernel_queue_t {
  // Device and WKMI adapter borrowed for the lifetime of the queue.
  amdf_gpu_umd_device_t* device;
  // Private native queue state owned by the loaded WKMI bridge.
  amdf_wkmi_bridge_gpu_kernel_queue_t* native;
  // Required command-buffer base alignment in bytes.
  uint32_t command_buffer_alignment;
  // Maximum byte length accepted by one native submission.
  uint64_t maximum_command_buffer_byte_length;
  // Monitored progress-fence object owned by `native`.
  D3DKMT_HANDLE progress_fence;
  // CPU-readable monotonic hardware-queue progress value.
  const volatile uint64_t* progress_fence_pointer;
  // GPU-visible progress address retained for queue interoperability.
  uint64_t progress_fence_device_address;
  // Serializes use of the reusable asynchronous wait event.
  SRWLOCK wait_lock;
  // Manual-reset event reused by externally serialized native waits.
  HANDLE wait_event;
  // Native submission registered to signal `wait_event`, or zero when idle.
  uint64_t wait_event_submission;
  // Greatest progress value assigned to a native submission.
  uint64_t last_native_submission;
  // Query-performance-counter ticks per second.
  uint64_t performance_counter_frequency;
};

static uint64_t amdf_gpu_wddm_query_counter(void) {
  LARGE_INTEGER value;
  QueryPerformanceCounter(&value);
  return (uint64_t)value.QuadPart;
}

static uint64_t amdf_gpu_wddm_counter_elapsed_nanoseconds(uint64_t begin,
                                                          uint64_t end,
                                                          uint64_t frequency) {
  const uint64_t elapsed = end - begin;
  return (elapsed / frequency) * UINT64_C(1000000000) +
         ((elapsed % frequency) * UINT64_C(1000000000)) / frequency;
}

amdf_status_t amdf_gpu_umd_kernel_queue_create(
    amdf_gpu_umd_device_t* device, amdf_queue_command_type_t command_type,
    amdf_gpu_umd_kernel_queue_t** out_queue) {
  *out_queue = NULL;
  amdf_wkmi_bridge_gpu_queue_command_type_t native_command_type;
  switch (command_type) {
    case AMDF_QUEUE_COMMAND_TYPE_GPU_PM4:
      native_command_type = AMDF_WKMI_BRIDGE_GPU_QUEUE_COMMAND_TYPE_PM4;
      break;
    case AMDF_QUEUE_COMMAND_TYPE_GPU_SDMA:
      native_command_type = AMDF_WKMI_BRIDGE_GPU_QUEUE_COMMAND_TYPE_SDMA;
      break;
    default:
      return amdf_make_api_status(AMDF_STATUS_CODE_UNSUPPORTED);
  }
  amdf_gpu_umd_kernel_queue_t* queue =
      (amdf_gpu_umd_kernel_queue_t*)calloc(1, sizeof(*queue));
  if (queue == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_RESOURCE_EXHAUSTED);
  }
  queue->device = device;
  InitializeSRWLock(&queue->wait_lock);

  LARGE_INTEGER frequency;
  amdf_status_t status = AMDF_STATUS_OK;
  if (!QueryPerformanceFrequency(&frequency)) {
    status = amdf_make_status(AMDF_STATUS_DOMAIN_WIN32, GetLastError());
  } else if (frequency.QuadPart <= 0) {
    status = amdf_make_api_status(AMDF_STATUS_CODE_INTERNAL);
  } else {
    queue->performance_counter_frequency = (uint64_t)frequency.QuadPart;
  }
  if (amdf_status_is_ok(status)) {
    queue->wait_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (queue->wait_event == NULL) {
      status = amdf_make_status(AMDF_STATUS_DOMAIN_WIN32, GetLastError());
    }
  }

  amdf_wkmi_bridge_gpu_kernel_queue_info_t queue_info = {0};
  amdf_wkmi_bridge_gpu_kernel_queue_create_info_t create_info = {
      .structure_size = sizeof(create_info),
      .device_handle = device->device,
      .command_type = native_command_type,
  };
  if (amdf_status_is_ok(status)) {
    status = amdf_gpu_wddm_wkmi_adapter_create_kernel_queue(
        &device->wkmi, &create_info, &queue->native, &queue_info);
  }
  if (amdf_status_is_ok(status)) {
    queue->command_buffer_alignment = queue_info.command_buffer_alignment;
    queue->maximum_command_buffer_byte_length =
        queue_info.maximum_command_buffer_byte_length;
    queue->progress_fence = queue_info.progress_fence_handle;
    queue->progress_fence_pointer = queue_info.progress_fence_pointer;
    queue->progress_fence_device_address =
        queue_info.progress_fence_device_address;
    *out_queue = queue;
  } else {
    if (queue->wait_event != NULL && !CloseHandle(queue->wait_event)) {
      status = amdf_make_status(AMDF_STATUS_DOMAIN_WIN32, GetLastError());
    }
    free(queue);
  }
  return status;
}

amdf_status_t amdf_gpu_umd_kernel_queue_submit(
    amdf_gpu_umd_kernel_queue_t* queue, uint64_t command_buffer_address,
    uint64_t command_buffer_byte_length, uint64_t* out_native_submission) {
  if (queue->native == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_FAILED_PRECONDITION);
  }
  if (queue->last_native_submission == UINT64_MAX) {
    return amdf_make_api_status(AMDF_STATUS_CODE_RESOURCE_EXHAUSTED);
  }
  if (command_buffer_address == 0 || command_buffer_byte_length == 0 ||
      command_buffer_address % queue->command_buffer_alignment != 0 ||
      command_buffer_byte_length % queue->command_buffer_alignment != 0) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  if (command_buffer_byte_length > queue->maximum_command_buffer_byte_length) {
    return amdf_make_api_status(AMDF_STATUS_CODE_OUT_OF_RANGE);
  }

  const uint64_t native_submission = queue->last_native_submission + 1;
  MemoryBarrier();
  const amdf_status_t status = amdf_gpu_wddm_wkmi_adapter_submit_kernel_queue(
      &queue->device->wkmi, queue->native, command_buffer_address,
      command_buffer_byte_length, native_submission);
  if (amdf_status_is_ok(status)) {
    queue->last_native_submission = native_submission;
    *out_native_submission = native_submission;
  }
  return status;
}

uint64_t amdf_gpu_umd_kernel_queue_query_progress(
    const amdf_gpu_umd_kernel_queue_t* queue) {
  if (queue->native == NULL) {
    return queue->last_native_submission;
  }
  const uint64_t progress = *queue->progress_fence_pointer;
  MemoryBarrier();
  return progress;
}

amdf_status_t amdf_gpu_umd_kernel_queue_wait(
    amdf_gpu_umd_kernel_queue_t* queue, uint64_t native_submission,
    uint64_t timeout_nanoseconds, uint64_t poll_duration_nanoseconds) {
  const uint64_t begin = amdf_gpu_wddm_query_counter();
  const uint64_t poll_limit =
      timeout_nanoseconds == AMDF_TIMEOUT_INFINITE
          ? poll_duration_nanoseconds
          : (poll_duration_nanoseconds < timeout_nanoseconds
                 ? poll_duration_nanoseconds
                 : timeout_nanoseconds);
  while (amdf_gpu_umd_kernel_queue_query_progress(queue) < native_submission) {
    const uint64_t now = amdf_gpu_wddm_query_counter();
    if (amdf_gpu_wddm_counter_elapsed_nanoseconds(
            begin, now, queue->performance_counter_frequency) >= poll_limit) {
      break;
    }
    YieldProcessor();
  }
  if (amdf_gpu_umd_kernel_queue_query_progress(queue) >= native_submission) {
    return AMDF_STATUS_OK;
  }
  if (timeout_nanoseconds == 0 || poll_limit == timeout_nanoseconds) {
    return amdf_make_api_status(AMDF_STATUS_CODE_DEADLINE_EXCEEDED);
  }

  AcquireSRWLockExclusive(&queue->wait_lock);
  amdf_status_t status = AMDF_STATUS_OK;
  while (amdf_gpu_umd_kernel_queue_query_progress(queue) < native_submission) {
    if (queue->wait_event_submission == 0) {
      if (!ResetEvent(queue->wait_event)) {
        status = amdf_make_status(AMDF_STATUS_DOMAIN_WIN32, GetLastError());
        break;
      }
      D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU wait = {0};
      wait.hDevice = queue->device->device;
      wait.ObjectCount = 1;
      wait.ObjectHandleArray = &queue->progress_fence;
      wait.FenceValueArray = &native_submission;
      wait.hAsyncEvent = queue->wait_event;
      status = amdf_kmt_make_status(queue->device->kmt->wait_from_cpu(&wait));
      if (!amdf_status_is_ok(status)) {
        break;
      }
      queue->wait_event_submission = native_submission;
    }

    DWORD wait_milliseconds = INFINITE;
    if (timeout_nanoseconds != AMDF_TIMEOUT_INFINITE) {
      const uint64_t elapsed_nanoseconds =
          amdf_gpu_wddm_counter_elapsed_nanoseconds(
              begin, amdf_gpu_wddm_query_counter(),
              queue->performance_counter_frequency);
      if (elapsed_nanoseconds >= timeout_nanoseconds) {
        status = amdf_make_api_status(AMDF_STATUS_CODE_DEADLINE_EXCEEDED);
        break;
      }
      const uint64_t remaining_nanoseconds =
          timeout_nanoseconds - elapsed_nanoseconds;
      const uint64_t remaining_milliseconds =
          remaining_nanoseconds / UINT64_C(1000000) +
          (remaining_nanoseconds % UINT64_C(1000000) != 0);
      wait_milliseconds = remaining_milliseconds >= MAXDWORD
                              ? MAXDWORD - 1
                              : (DWORD)remaining_milliseconds;
    }
    const DWORD wait_result =
        WaitForSingleObject(queue->wait_event, wait_milliseconds);
    if (wait_result == WAIT_TIMEOUT) {
      continue;
    }
    if (wait_result != AMDF_GPU_WINDOWS_WAIT_SIGNALED) {
      status = wait_result == WAIT_FAILED
                   ? amdf_make_status(AMDF_STATUS_DOMAIN_WIN32, GetLastError())
                   : amdf_make_api_status(AMDF_STATUS_CODE_INTERNAL);
      break;
    }
    queue->wait_event_submission = 0;
  }
  ReleaseSRWLockExclusive(&queue->wait_lock);
  return status;
}

amdf_status_t amdf_gpu_umd_kernel_queue_destroy(
    amdf_gpu_umd_kernel_queue_t* queue) {
  if (queue->native != NULL) {
    const amdf_status_t status =
        amdf_gpu_wddm_wkmi_adapter_destroy_kernel_queue(&queue->device->wkmi,
                                                        queue->native);
    if (!amdf_status_is_ok(status)) {
      return status;
    }
    queue->native = NULL;
  }
  if (queue->wait_event != NULL) {
    if (!CloseHandle(queue->wait_event)) {
      return amdf_make_status(AMDF_STATUS_DOMAIN_WIN32, GetLastError());
    }
    queue->wait_event = NULL;
  }
  free(queue);
  return AMDF_STATUS_OK;
}
