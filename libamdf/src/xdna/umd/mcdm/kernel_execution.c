// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/xdna/umd/mcdm/kernel_execution.h"

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "libamdf/src/xdna/transaction.h"
#include "libamdf/src/xdna/umd/mcdm/npu5_legacy_bootstrap_image.h"

enum {
  AMDF_WINDOWS_WAIT_SIGNALED = 0,
  AMDF_WINDOWS_XDNA_KERNEL_COMMAND_SLOT_COUNT = 3,
  AMDF_WINDOWS_XDNA_KERNEL_COMMAND_SLOT_SIZE = 32 * 1024,
  AMDF_WINDOWS_XDNA_KERNEL_EXECUTION_REQUESTED_SIZE = 4200,
  AMDF_WINDOWS_XDNA_KERNEL_EXECUTION_ALLOCATION_SIZE = 8192,
};

#define AMDF_WINDOWS_XDNA_CONTEXT_APERTURE_BASE UINT64_C(0x04000000)
#define AMDF_WINDOWS_XDNA_CONTEXT_PDI_SIZE UINT64_C(0x8000)
#define AMDF_WINDOWS_XDNA_CONTEXT_INSTRUCTION_SIZE UINT64_C(0x4000000)
#define AMDF_WINDOWS_XDNA_CONTEXT_INSTRUCTION_WATERMARK UINT64_C(0x20000)

// One device-owned instruction and ERT execution slot.
typedef struct amdf_windows_xdna_kernel_command_slot_t {
  // Private execution allocation carrying one ERT packet.
  amdf_windows_xdna_private_allocation_t execution_allocation;
  // Context-aperture byte offset of this slot's transaction stream.
  uint64_t instruction_offset;
} amdf_windows_xdna_kernel_command_slot_t;

struct amdf_windows_xdna_kernel_execution_t {
  // Platform device borrowed until destruction succeeds.
  amdf_xdna_umd_device_t* device;
  // Serializes initialization, command-slot ownership, and queue leasing.
  SRWLOCK state_lock;
  // Serializes use of the one reusable asynchronous wait event.
  SRWLOCK wait_lock;
  // Native hardware queue owned by this execution path.
  D3DKMT_HANDLE hardware_queue;
  // Monitored fence signaled as hardware-queue commands retire.
  D3DKMT_HANDLE progress_fence;
  // CPU-readable monotonic hardware-queue progress value.
  const volatile uint64_t* progress_fence_pointer;
  // GPU-visible progress address retained for later queue interoperability.
  uint64_t progress_fence_device_address;
  // Manual-reset event reused by externally serialized native waits.
  HANDLE wait_event;
  // Native submission registered to signal `wait_event`, or zero when idle.
  uint64_t wait_event_submission;
  // Shared completion and context-initialization allocation.
  amdf_windows_xdna_private_allocation_t command_allocation;
  // Context-local executable instruction aperture backing.
  amdf_windows_xdna_private_allocation_t instruction_allocation;
  // Fixed command slots owned for the lifetime of the execution path.
  amdf_windows_xdna_kernel_command_slot_t
      command_slots[AMDF_WINDOWS_XDNA_KERNEL_COMMAND_SLOT_COUNT];
  // Bit set for every command slot held by a live prepared command.
  uint32_t acquired_command_slot_bits;
  // One while a public queue exclusively leases the hardware queue.
  uint32_t queue_lease_count;
  // Completed storage or bootstrap construction phase.
  uint32_t initialization_phase;
  // Native setup submission accepted but not yet observed retired.
  uint64_t initialization_submission;
  // Greatest progress value assigned to a native submission.
  uint64_t last_native_submission;
  // Query-performance-counter ticks per second.
  uint64_t performance_counter_frequency;
};

static uint64_t amdf_windows_xdna_query_counter(void) {
  LARGE_INTEGER value;
  QueryPerformanceCounter(&value);
  return (uint64_t)value.QuadPart;
}

static uint64_t amdf_windows_xdna_counter_elapsed_nanoseconds(
    uint64_t begin, uint64_t end, uint64_t frequency) {
  const uint64_t elapsed = end - begin;
  return (elapsed / frequency) * UINT64_C(1000000000) +
         ((elapsed % frequency) * UINT64_C(1000000000)) / frequency;
}

uint64_t amdf_windows_xdna_kernel_execution_query_progress(
    const amdf_windows_xdna_kernel_execution_t* execution) {
  return execution->progress_fence_pointer == NULL
             ? 0
             : *execution->progress_fence_pointer;
}

static amdf_status_t amdf_windows_xdna_kernel_execution_submit_native(
    amdf_windows_xdna_kernel_execution_t* execution, uint64_t command_address,
    uint32_t command_byte_length,
    const amdf_windows_xdna_legacy_submission_t* submission,
    uint64_t* out_native_submission) {
  if (execution->last_native_submission == UINT64_MAX) {
    return amdf_make_api_status(AMDF_STATUS_CODE_RESOURCE_EXHAUSTED);
  }
  const uint64_t native_submission = execution->last_native_submission + 1;
  D3DKMT_SUBMITCOMMANDTOHWQUEUE submit = {0};
  submit.hHwQueue = execution->hardware_queue;
  submit.HwQueueProgressFenceId = native_submission;
  submit.CommandBuffer = command_address;
  submit.CommandLength = command_byte_length;
  submit.PrivateDriverDataSize = submission->byte_length;
  submit.pPrivateDriverData = (void*)submission->bytes;
  MemoryBarrier();
  const amdf_status_t status = amdf_kmt_make_status(
      execution->device->kmt->submit_command_to_hardware_queue(&submit));
  if (amdf_status_is_ok(status)) {
    execution->last_native_submission = native_submission;
    *out_native_submission = native_submission;
  }
  return status;
}

static amdf_status_t amdf_windows_xdna_kernel_execution_wait_synchronous(
    amdf_windows_xdna_kernel_execution_t* execution, uint64_t submission) {
  if (amdf_windows_xdna_kernel_execution_query_progress(execution) >=
      submission) {
    MemoryBarrier();
    return AMDF_STATUS_OK;
  }
  D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU wait = {0};
  wait.hDevice = execution->device->device;
  wait.ObjectCount = 1;
  wait.ObjectHandleArray = &execution->progress_fence;
  wait.FenceValueArray = &submission;
  const amdf_status_t status =
      amdf_kmt_make_status(execution->device->kmt->wait_from_cpu(&wait));
  if (!amdf_status_is_ok(status)) {
    return status;
  }
  MemoryBarrier();
  return amdf_windows_xdna_kernel_execution_query_progress(execution) >=
                 submission
             ? AMDF_STATUS_OK
             : amdf_make_api_status(AMDF_STATUS_CODE_INTERNAL);
}

static amdf_status_t amdf_windows_xdna_kernel_execution_submit_setup(
    amdf_windows_xdna_kernel_execution_t* execution, uint64_t command_address,
    uint32_t command_byte_length,
    const amdf_windows_xdna_legacy_submission_t* submission) {
  amdf_status_t status = AMDF_STATUS_OK;
  if (execution->initialization_submission == 0) {
    status = amdf_windows_xdna_kernel_execution_submit_native(
        execution, command_address, command_byte_length, submission,
        &execution->initialization_submission);
  }
  if (amdf_status_is_ok(status)) {
    status = amdf_windows_xdna_kernel_execution_wait_synchronous(
        execution, execution->initialization_submission);
  }
  if (amdf_status_is_ok(status)) {
    execution->initialization_submission = 0;
  }
  return status;
}

static amdf_status_t amdf_windows_xdna_kernel_execution_create_wait_state(
    amdf_windows_xdna_kernel_execution_t* execution) {
  if (execution->performance_counter_frequency == 0) {
    LARGE_INTEGER frequency;
    if (!QueryPerformanceFrequency(&frequency)) {
      return amdf_make_status(AMDF_STATUS_DOMAIN_WIN32, GetLastError());
    }
    if (frequency.QuadPart <= 0) {
      return amdf_make_api_status(AMDF_STATUS_CODE_INTERNAL);
    }
    execution->performance_counter_frequency = (uint64_t)frequency.QuadPart;
  }
  if (execution->wait_event == NULL) {
    execution->wait_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (execution->wait_event == NULL) {
      return amdf_make_status(AMDF_STATUS_DOMAIN_WIN32, GetLastError());
    }
  }
  return AMDF_STATUS_OK;
}

static amdf_status_t amdf_windows_xdna_kernel_execution_create_queue(
    amdf_windows_xdna_kernel_execution_t* execution) {
  amdf_status_t status =
      amdf_windows_xdna_kernel_execution_create_wait_state(execution);
  if (!amdf_status_is_ok(status)) {
    return status;
  }
  if (execution->hardware_queue != 0) {
    return execution->progress_fence != 0 &&
                   execution->progress_fence_pointer != NULL &&
                   execution->progress_fence_device_address != 0
               ? AMDF_STATUS_OK
               : amdf_make_api_status(AMDF_STATUS_CODE_INTERNAL);
  }
  D3DKMT_CREATEHWQUEUE create = {0};
  create.hHwContext = execution->device->context;
  status = amdf_kmt_make_status(
      execution->device->kmt->create_hardware_queue(&create));
  if (!amdf_status_is_ok(status)) {
    return status;
  }
  execution->hardware_queue = create.hHwQueue;
  execution->progress_fence = create.hHwQueueProgressFence;
  execution->progress_fence_pointer =
      (const volatile uint64_t*)create.HwQueueProgressFenceCPUVirtualAddress;
  execution->progress_fence_device_address =
      create.HwQueueProgressFenceGPUVirtualAddress;
  if (execution->hardware_queue == 0 || execution->progress_fence == 0 ||
      execution->progress_fence_pointer == NULL ||
      execution->progress_fence_device_address == 0) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INTERNAL);
  }
  execution->last_native_submission =
      amdf_windows_xdna_kernel_execution_query_progress(execution);
  return AMDF_STATUS_OK;
}

static amdf_status_t amdf_windows_xdna_kernel_execution_create_command(
    amdf_windows_xdna_kernel_execution_t* execution) {
  const amdf_windows_xdna_private_allocation_descriptor_t descriptor = {
      .requested_byte_length = 4096,
      .allocation_byte_length = 4096,
      .type = 0x332B,
      .policy = 0,
      .flags = AMDF_WINDOWS_XDNA_PRIVATE_ALLOCATION_FLAG_SHARED_RESOURCE,
  };
  amdf_status_t status = amdf_windows_xdna_private_allocation_create(
      execution->device, &descriptor, &execution->command_allocation);
  if (amdf_status_is_ok(status)) {
    status = amdf_windows_xdna_private_allocation_lock(
        &execution->command_allocation);
  }
  return status;
}

static amdf_status_t amdf_windows_xdna_kernel_execution_create_instructions(
    amdf_windows_xdna_kernel_execution_t* execution) {
  const uint32_t xcl_flags =
      ((execution->device->command_aperture_cookie | 0x100u) << 16) | 1u;
  const amdf_windows_xdna_private_allocation_descriptor_t descriptor = {
      .requested_byte_length = AMDF_WINDOWS_XDNA_CONTEXT_INSTRUCTION_SIZE,
      .allocation_byte_length = AMDF_WINDOWS_XDNA_CONTEXT_INSTRUCTION_SIZE,
      .type = 0x3323,
      .policy = 2,
      .xcl_flags = xcl_flags,
      .selector = 1,
      .flags = AMDF_WINDOWS_XDNA_PRIVATE_ALLOCATION_FLAG_DEVICE_ADDRESS,
  };
  amdf_status_t status = amdf_windows_xdna_private_allocation_create(
      execution->device, &descriptor, &execution->instruction_allocation);
  if (amdf_status_is_ok(status)) {
    status = amdf_windows_xdna_private_allocation_lock(
        &execution->instruction_allocation);
  }
  return status;
}

static amdf_status_t amdf_windows_xdna_kernel_execution_publish_aperture(
    amdf_windows_xdna_kernel_execution_t* execution) {
  amdf_windows_xdna_legacy_submission_t submission;
  amdf_windows_xdna_legacy_submission_build_aperture(
      &execution->instruction_allocation, &submission);
  return amdf_windows_xdna_kernel_execution_submit_setup(
      execution, execution->instruction_allocation.device_address,
      (uint32_t)
          execution->instruction_allocation.descriptor.allocation_byte_length,
      &submission);
}

static amdf_status_t amdf_windows_xdna_kernel_execution_publish_pdi(
    amdf_windows_xdna_kernel_execution_t* execution) {
  amdf_status_t status = amdf_windows_xdna_private_allocation_lock(
      &execution->instruction_allocation);
  if (!amdf_status_is_ok(status)) {
    return status;
  }
  const void* pdi_data = NULL;
  size_t pdi_data_size = 0;
  amdf_windows_xdna_npu5_legacy_bootstrap_query_pdi(&pdi_data, &pdi_data_size);
  if (pdi_data == NULL || pdi_data_size == 0 ||
      pdi_data_size > AMDF_WINDOWS_XDNA_CONTEXT_PDI_SIZE) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INTERNAL);
  }
  memset(execution->instruction_allocation.host_pointer, 0,
         (size_t)AMDF_WINDOWS_XDNA_CONTEXT_PDI_SIZE);
  memcpy(execution->instruction_allocation.host_pointer, pdi_data,
         pdi_data_size);
  return amdf_windows_xdna_private_allocation_publish(
      &execution->instruction_allocation, 0,
      AMDF_WINDOWS_XDNA_CONTEXT_PDI_SIZE);
}

static amdf_status_t amdf_windows_xdna_kernel_execution_initialize_context(
    amdf_windows_xdna_kernel_execution_t* execution) {
  if (execution->initialization_submission == 0) {
    memset(execution->command_allocation.host_pointer, 0,
           (size_t)
               execution->command_allocation.descriptor.allocation_byte_length);
    uint64_t* command_words =
        (uint64_t*)execution->command_allocation.host_pointer;
    command_words[0] = 1;
    command_words[1] = AMDF_WINDOWS_XDNA_CONTEXT_APERTURE_BASE;
  }
  amdf_windows_xdna_legacy_submission_t submission;
  amdf_windows_xdna_legacy_submission_build_context_initialize(
      &execution->command_allocation, &submission);
  return amdf_windows_xdna_kernel_execution_submit_setup(execution, 0, 0,
                                                         &submission);
}

static amdf_status_t amdf_windows_xdna_kernel_execution_create_slots(
    amdf_windows_xdna_kernel_execution_t* execution) {
  for (uint32_t i = 0; i < AMDF_WINDOWS_XDNA_KERNEL_COMMAND_SLOT_COUNT; ++i) {
    amdf_windows_xdna_kernel_command_slot_t* slot =
        &execution->command_slots[i];
    const amdf_windows_xdna_private_allocation_descriptor_t descriptor = {
        .requested_byte_length =
            AMDF_WINDOWS_XDNA_KERNEL_EXECUTION_REQUESTED_SIZE,
        .allocation_byte_length =
            AMDF_WINDOWS_XDNA_KERNEL_EXECUTION_ALLOCATION_SIZE,
        .type = 0x3328,
        .policy = 2,
        .xcl_flags = 0x80000000u,
        .flags = AMDF_WINDOWS_XDNA_PRIVATE_ALLOCATION_FLAG_DEVICE_ADDRESS,
    };
    amdf_status_t status = amdf_windows_xdna_private_allocation_create(
        execution->device, &descriptor, &slot->execution_allocation);
    if (amdf_status_is_ok(status)) {
      status = amdf_windows_xdna_private_allocation_lock(
          &slot->execution_allocation);
    }
    if (!amdf_status_is_ok(status)) {
      return status;
    }
    slot->instruction_offset =
        AMDF_WINDOWS_XDNA_CONTEXT_PDI_SIZE +
        (uint64_t)i * AMDF_WINDOWS_XDNA_KERNEL_COMMAND_SLOT_SIZE;
  }
  return AMDF_STATUS_OK;
}

static amdf_status_t amdf_windows_xdna_kernel_execution_publish_watermark(
    amdf_windows_xdna_kernel_execution_t* execution) {
  amdf_windows_xdna_legacy_submission_t submission;
  amdf_windows_xdna_legacy_submission_build_watermark(
      &execution->instruction_allocation,
      AMDF_WINDOWS_XDNA_CONTEXT_INSTRUCTION_WATERMARK, &submission);
  return amdf_windows_xdna_kernel_execution_submit_setup(execution, 0, 0,
                                                         &submission);
}

static amdf_status_t amdf_windows_xdna_kernel_execution_advance(
    amdf_windows_xdna_kernel_execution_t* execution, uint32_t target_phase) {
  if (!amdf_kmt_api_supports_xdna_kernel_execution(execution->device->kmt)) {
    return amdf_make_api_status(AMDF_STATUS_CODE_UNSUPPORTED);
  }
  AcquireSRWLockExclusive(&execution->state_lock);
  amdf_status_t status = AMDF_STATUS_OK;
  while (amdf_status_is_ok(status) &&
         execution->initialization_phase < target_phase) {
    switch (execution->initialization_phase) {
      case 0:
        status = amdf_windows_xdna_kernel_execution_create_command(execution);
        break;
      case 1:
        status =
            amdf_windows_xdna_kernel_execution_create_instructions(execution);
        break;
      case 2:
        status = amdf_windows_xdna_kernel_execution_create_slots(execution);
        break;
      case 3:
        status = amdf_windows_xdna_kernel_execution_create_queue(execution);
        break;
      case 4:
        status = amdf_windows_xdna_kernel_execution_publish_aperture(execution);
        break;
      case 5:
        status = amdf_windows_xdna_kernel_execution_publish_pdi(execution);
        break;
      case 6:
        status =
            amdf_windows_xdna_kernel_execution_initialize_context(execution);
        break;
      case 7:
        status =
            amdf_windows_xdna_kernel_execution_publish_watermark(execution);
        break;
      default:
        status = amdf_make_api_status(AMDF_STATUS_CODE_INTERNAL);
        break;
    }
    if (amdf_status_is_ok(status)) {
      ++execution->initialization_phase;
    }
  }
  ReleaseSRWLockExclusive(&execution->state_lock);
  return status;
}

static amdf_status_t amdf_windows_xdna_kernel_execution_initialize(
    amdf_windows_xdna_kernel_execution_t* execution) {
  return amdf_windows_xdna_kernel_execution_advance(execution, 8);
}

amdf_status_t amdf_windows_xdna_kernel_execution_create(
    amdf_xdna_umd_device_t* device,
    amdf_windows_xdna_kernel_execution_t** out_execution) {
  if (device == NULL || out_execution == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  *out_execution = NULL;
  amdf_windows_xdna_kernel_execution_t* execution =
      (amdf_windows_xdna_kernel_execution_t*)calloc(1, sizeof(*execution));
  if (execution == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_RESOURCE_EXHAUSTED);
  }
  execution->device = device;
  InitializeSRWLock(&execution->state_lock);
  InitializeSRWLock(&execution->wait_lock);
  *out_execution = execution;
  return AMDF_STATUS_OK;
}

amdf_status_t amdf_windows_xdna_kernel_execution_destroy(
    amdf_windows_xdna_kernel_execution_t* execution) {
  if (execution == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  AcquireSRWLockExclusive(&execution->state_lock);
  if (execution->queue_lease_count != 0 ||
      execution->acquired_command_slot_bits != 0) {
    ReleaseSRWLockExclusive(&execution->state_lock);
    return amdf_make_api_status(AMDF_STATUS_CODE_BUSY);
  }
  if (execution->initialization_submission != 0 &&
      amdf_windows_xdna_kernel_execution_query_progress(execution) <
          execution->initialization_submission) {
    ReleaseSRWLockExclusive(&execution->state_lock);
    return amdf_make_api_status(AMDF_STATUS_CODE_BUSY);
  }
  execution->initialization_submission = 0;
  if (execution->wait_event_submission != 0) {
    const DWORD wait_result = WaitForSingleObject(execution->wait_event, 0);
    if (wait_result == WAIT_TIMEOUT) {
      ReleaseSRWLockExclusive(&execution->state_lock);
      return amdf_make_api_status(AMDF_STATUS_CODE_BUSY);
    }
    if (wait_result != AMDF_WINDOWS_WAIT_SIGNALED) {
      const amdf_status_t status =
          wait_result == WAIT_FAILED
              ? amdf_make_status(AMDF_STATUS_DOMAIN_WIN32, GetLastError())
              : amdf_make_api_status(AMDF_STATUS_CODE_INTERNAL);
      ReleaseSRWLockExclusive(&execution->state_lock);
      return status;
    }
    execution->wait_event_submission = 0;
  }

  amdf_status_t status = AMDF_STATUS_OK;
  if (execution->hardware_queue != 0) {
    D3DKMT_DESTROYHWQUEUE destroy = {0};
    destroy.hHwQueue = execution->hardware_queue;
    status = amdf_kmt_make_status(
        execution->device->kmt->destroy_hardware_queue(&destroy));
    if (amdf_status_is_ok(status)) {
      execution->hardware_queue = 0;
      execution->progress_fence = 0;
      execution->progress_fence_pointer = NULL;
      execution->progress_fence_device_address = 0;
    }
  }
  for (uint32_t i = AMDF_WINDOWS_XDNA_KERNEL_COMMAND_SLOT_COUNT;
       amdf_status_is_ok(status) && i != 0; --i) {
    amdf_windows_xdna_private_allocation_t* allocation =
        &execution->command_slots[i - 1].execution_allocation;
    if (allocation->device != NULL) {
      status = amdf_windows_xdna_private_allocation_destroy(allocation);
    }
  }
  if (amdf_status_is_ok(status) &&
      execution->instruction_allocation.device != NULL) {
    status = amdf_windows_xdna_private_allocation_destroy(
        &execution->instruction_allocation);
  }
  if (amdf_status_is_ok(status) &&
      execution->command_allocation.device != NULL) {
    status = amdf_windows_xdna_private_allocation_destroy(
        &execution->command_allocation);
  }
  if (amdf_status_is_ok(status) && execution->wait_event != NULL) {
    if (!CloseHandle(execution->wait_event)) {
      status = amdf_make_status(AMDF_STATUS_DOMAIN_WIN32, GetLastError());
    } else {
      execution->wait_event = NULL;
    }
  }
  ReleaseSRWLockExclusive(&execution->state_lock);
  if (amdf_status_is_ok(status)) {
    free(execution);
  }
  return status;
}

amdf_status_t amdf_windows_xdna_kernel_execution_prepare_command(
    amdf_windows_xdna_kernel_execution_t* execution, const void* program_bytes,
    uint64_t program_byte_length, const void* control_bytes,
    uint64_t control_byte_length, const uint64_t* binding_addresses,
    uint32_t binding_count, amdf_windows_xdna_kernel_command_t* out_command) {
  if (execution == NULL || program_bytes == NULL || control_bytes == NULL ||
      out_command == NULL || binding_count > 5 ||
      (binding_count != 0) != (binding_addresses != NULL)) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  memset(out_command, 0, sizeof(*out_command));
  amdf_status_t status =
      amdf_windows_xdna_kernel_execution_advance(execution, 3);
  if (!amdf_status_is_ok(status)) {
    return status;
  }
  AcquireSRWLockExclusive(&execution->state_lock);
  uint32_t slot_ordinal = AMDF_WINDOWS_XDNA_KERNEL_COMMAND_SLOT_COUNT;
  for (uint32_t i = 0; i < AMDF_WINDOWS_XDNA_KERNEL_COMMAND_SLOT_COUNT; ++i) {
    if ((execution->acquired_command_slot_bits & (1u << i)) == 0) {
      slot_ordinal = i;
      break;
    }
  }
  if (slot_ordinal == AMDF_WINDOWS_XDNA_KERNEL_COMMAND_SLOT_COUNT) {
    ReleaseSRWLockExclusive(&execution->state_lock);
    return amdf_make_api_status(AMDF_STATUS_CODE_RESOURCE_EXHAUSTED);
  }

  amdf_windows_xdna_kernel_command_slot_t* slot =
      &execution->command_slots[slot_ordinal];
  uint8_t* instruction_bytes =
      (uint8_t*)execution->instruction_allocation.host_pointer +
      slot->instruction_offset;
  uint64_t instruction_byte_length = 0;
  status = amdf_xdna_transaction_compose(
      program_bytes, program_byte_length, control_bytes, control_byte_length,
      instruction_bytes, AMDF_WINDOWS_XDNA_KERNEL_COMMAND_SLOT_SIZE,
      &instruction_byte_length);
  if (amdf_status_is_ok(status)) {
    status = amdf_windows_xdna_private_allocation_publish(
        &execution->instruction_allocation, slot->instruction_offset,
        instruction_byte_length);
  }

  amdf_windows_xdna_legacy_ert_packet_t packet;
  if (amdf_status_is_ok(status)) {
    status = amdf_windows_xdna_legacy_ert_packet_build(
        AMDF_WINDOWS_XDNA_CONTEXT_APERTURE_BASE + slot->instruction_offset,
        (uint32_t)instruction_byte_length, binding_addresses, binding_count,
        &packet);
  }
  if (amdf_status_is_ok(status)) {
    memcpy(slot->execution_allocation.host_pointer, &packet, sizeof(packet));
    status = amdf_windows_xdna_private_allocation_publish(
        &slot->execution_allocation, 0, sizeof(packet));
  }
  if (amdf_status_is_ok(status)) {
    out_command->execution = execution;
    out_command->slot_ordinal = slot_ordinal;
    out_command->command_byte_length =
        AMDF_WINDOWS_XDNA_KERNEL_EXECUTION_REQUESTED_SIZE;
    out_command->command_address = slot->execution_allocation.device_address;
    amdf_windows_xdna_legacy_submission_build_execute(
        &slot->execution_allocation, &execution->command_allocation, &packet,
        &out_command->submission);
    execution->acquired_command_slot_bits |= 1u << slot_ordinal;
  }
  ReleaseSRWLockExclusive(&execution->state_lock);
  return status;
}

void amdf_windows_xdna_kernel_execution_release_command(
    amdf_windows_xdna_kernel_command_t* command) {
  amdf_windows_xdna_kernel_execution_t* execution = command->execution;
  AcquireSRWLockExclusive(&execution->state_lock);
  const uint32_t slot_bit = 1u << command->slot_ordinal;
  assert((execution->acquired_command_slot_bits & slot_bit) != 0 &&
         "releasing an unowned XDNA command slot");
  execution->acquired_command_slot_bits &= ~slot_bit;
  ReleaseSRWLockExclusive(&execution->state_lock);
  memset(command, 0, sizeof(*command));
}

amdf_status_t amdf_windows_xdna_kernel_execution_acquire_queue(
    amdf_windows_xdna_kernel_execution_t* execution) {
  if (execution == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  amdf_status_t status =
      amdf_windows_xdna_kernel_execution_initialize(execution);
  if (!amdf_status_is_ok(status)) {
    return status;
  }
  AcquireSRWLockExclusive(&execution->state_lock);
  if (execution->queue_lease_count != 0) {
    status = amdf_make_api_status(AMDF_STATUS_CODE_RESOURCE_EXHAUSTED);
  } else {
    execution->queue_lease_count = 1;
  }
  ReleaseSRWLockExclusive(&execution->state_lock);
  return status;
}

void amdf_windows_xdna_kernel_execution_release_queue(
    amdf_windows_xdna_kernel_execution_t* execution) {
  AcquireSRWLockExclusive(&execution->state_lock);
  assert(execution->queue_lease_count == 1 &&
         "releasing an unowned XDNA hardware queue");
  execution->queue_lease_count = 0;
  ReleaseSRWLockExclusive(&execution->state_lock);
}

amdf_status_t amdf_windows_xdna_kernel_execution_submit(
    amdf_windows_xdna_kernel_execution_t* execution,
    const amdf_windows_xdna_kernel_command_t* command,
    uint64_t* out_native_submission) {
  if (execution == NULL || command == NULL || out_native_submission == NULL ||
      command->execution != execution) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  uint64_t* command_words =
      (uint64_t*)execution->command_allocation.host_pointer;
  // These words are CPU/UMD coordination state. The target-native packet was
  // copied into the immutable private submission record during preparation.
  command_words[0] = 1;
  command_words[1] = 0;
  return amdf_windows_xdna_kernel_execution_submit_native(
      execution, command->command_address, command->command_byte_length,
      &command->submission, out_native_submission);
}

amdf_status_t amdf_windows_xdna_kernel_execution_wait(
    amdf_windows_xdna_kernel_execution_t* execution, uint64_t native_submission,
    uint64_t timeout_nanoseconds, uint64_t poll_duration_nanoseconds) {
  if (execution == NULL || native_submission == 0) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  const uint64_t begin = amdf_windows_xdna_query_counter();
  const uint64_t poll_limit =
      timeout_nanoseconds == AMDF_TIMEOUT_INFINITE
          ? poll_duration_nanoseconds
          : (poll_duration_nanoseconds < timeout_nanoseconds
                 ? poll_duration_nanoseconds
                 : timeout_nanoseconds);
  while (amdf_windows_xdna_kernel_execution_query_progress(execution) <
         native_submission) {
    const uint64_t now = amdf_windows_xdna_query_counter();
    if (amdf_windows_xdna_counter_elapsed_nanoseconds(
            begin, now, execution->performance_counter_frequency) >=
        poll_limit) {
      break;
    }
    YieldProcessor();
  }
  if (amdf_windows_xdna_kernel_execution_query_progress(execution) >=
      native_submission) {
    MemoryBarrier();
    return AMDF_STATUS_OK;
  }
  if (timeout_nanoseconds == 0 || poll_limit == timeout_nanoseconds) {
    return amdf_make_api_status(AMDF_STATUS_CODE_DEADLINE_EXCEEDED);
  }

  AcquireSRWLockExclusive(&execution->wait_lock);
  amdf_status_t status = AMDF_STATUS_OK;
  while (amdf_windows_xdna_kernel_execution_query_progress(execution) <
         native_submission) {
    if (execution->wait_event_submission == 0) {
      if (!ResetEvent(execution->wait_event)) {
        status = amdf_make_status(AMDF_STATUS_DOMAIN_WIN32, GetLastError());
        break;
      }
      D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU wait = {0};
      wait.hDevice = execution->device->device;
      wait.ObjectCount = 1;
      wait.ObjectHandleArray = &execution->progress_fence;
      wait.FenceValueArray = &native_submission;
      wait.hAsyncEvent = execution->wait_event;
      status =
          amdf_kmt_make_status(execution->device->kmt->wait_from_cpu(&wait));
      if (!amdf_status_is_ok(status)) {
        break;
      }
      execution->wait_event_submission = native_submission;
    }

    DWORD wait_milliseconds = INFINITE;
    if (timeout_nanoseconds != AMDF_TIMEOUT_INFINITE) {
      const uint64_t elapsed_nanoseconds =
          amdf_windows_xdna_counter_elapsed_nanoseconds(
              begin, amdf_windows_xdna_query_counter(),
              execution->performance_counter_frequency);
      if (elapsed_nanoseconds >= timeout_nanoseconds) {
        status = amdf_make_api_status(AMDF_STATUS_CODE_DEADLINE_EXCEEDED);
        break;
      }
      const uint64_t remaining_nanoseconds =
          timeout_nanoseconds - elapsed_nanoseconds;
      const uint64_t remaining_milliseconds =
          (remaining_nanoseconds + UINT64_C(999999)) / UINT64_C(1000000);
      wait_milliseconds = remaining_milliseconds >= MAXDWORD
                              ? MAXDWORD - 1
                              : (DWORD)remaining_milliseconds;
    }
    const DWORD wait_result =
        WaitForSingleObject(execution->wait_event, wait_milliseconds);
    if (wait_result == WAIT_TIMEOUT) {
      if (amdf_windows_xdna_kernel_execution_query_progress(execution) <
          native_submission) {
        status = amdf_make_api_status(AMDF_STATUS_CODE_DEADLINE_EXCEEDED);
      }
      break;
    }
    if (wait_result != AMDF_WINDOWS_WAIT_SIGNALED) {
      status = wait_result == WAIT_FAILED
                   ? amdf_make_status(AMDF_STATUS_DOMAIN_WIN32, GetLastError())
                   : amdf_make_api_status(AMDF_STATUS_CODE_INTERNAL);
      break;
    }
    execution->wait_event_submission = 0;
  }
  ReleaseSRWLockExclusive(&execution->wait_lock);
  if (amdf_status_is_ok(status)) {
    MemoryBarrier();
  }
  return status;
}
