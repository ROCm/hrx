// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/gpu/umd/memory.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "libamdf/src/gpu/umd/wddm/device.h"
#include "libamdf/src/gpu/umd/wddm/memory.h"
#include "libamdf/src/platform/windows/host_cache.h"

#define AMDF_WINDOWS_GPU_PAGE_SIZE UINT64_C(4096)
#define AMDF_WINDOWS_GPU_RESERVATION_GRANULARITY UINT64_C(65536)

typedef struct amdf_windows_gpu_memory_plan_t {
  // Page-rounded physical allocation length.
  uint64_t byte_length;
  // Guaranteed base alignment across every exposed address space.
  uint64_t alignment;
  // Private WKMI physical allocation domain.
  amdf_wkmi_bridge_gpu_allocation_domain_t allocation_domain;
  // Private WKMI physical allocation behavior.
  amdf_wkmi_bridge_gpu_allocation_flags_t allocation_flags;
  // Complete achieved public memory properties.
  amdf_memory_flags_t achieved_flags;
} amdf_windows_gpu_memory_plan_t;

struct amdf_gpu_umd_memory_t {
  // Next device-owned record awaiting a failed construction rollback retry.
  amdf_gpu_umd_memory_t* next_deferred_release;
  // Device borrowed while this attachment remains live.
  amdf_gpu_umd_device_t* device;
  // KMT resource grouping the native allocations, or zero when ungrouped.
  D3DKMT_HANDLE resource;
  // Capacity of trailing `allocation_handles` storage.
  uint32_t allocation_capacity;
  // Number of live native handles in `allocation_handles`.
  uint32_t allocation_count;
  // Maximum byte length represented by each native handle except the tail.
  uint64_t maximum_native_allocation_byte_length;
  // First byte of the host reservation owned by system memory.
  void* host_reservation;
  // Host-visible allocation base, or `NULL` for local memory.
  void* host_pointer;
  // Aggregate physical allocation length in bytes.
  uint64_t byte_length;
  // First usable byte of the GPU virtual-address reservation.
  uint64_t device_address;
  // Native base retained for releasing the GPU virtual-address reservation.
  uint64_t device_reservation_base;
  // Native GPU virtual-address reservation length.
  uint64_t device_reservation_byte_length;
  // Prefix length with valid GPU page-table mappings.
  uint64_t mapped_byte_length;
  // Unexpected driver-selected mapping retained until rollback succeeds.
  struct {
    // Driver-selected base address.
    uint64_t device_address;
    // Byte length awaiting rollback.
    uint64_t mapped_byte_length;
    // Paging fence that publishes the mapping before rollback.
    uint64_t paging_fence_value;
  } rollback;
  // Nonzero while the native allocations are resident.
  uint32_t is_resident;
  // Achieved public memory properties.
  amdf_memory_flags_t flags;
  // Native allocation handles in increasing byte-offset order.
  D3DKMT_HANDLE allocation_handles[];
};

struct amdf_gpu_umd_host_mapping_t {
  // Memory borrowed by the generic host-mapping parent.
  amdf_gpu_umd_memory_t* memory;
  // Byte offset of the mapping within `memory`.
  uint64_t memory_byte_offset;
  // First byte exposed by this mapping.
  void* pointer;
  // Exposed byte length.
  uint64_t byte_length;
};

static bool amdf_windows_gpu_align_up(uint64_t value, uint64_t alignment,
                                      uint64_t* out_value) {
  const uint64_t mask = alignment - 1;
  if (value > UINT64_MAX - mask) {
    return false;
  }
  *out_value = (value + mask) & ~mask;
  return true;
}

static amdf_status_t amdf_windows_gpu_memory_validate_create_info(
    const amdf_memory_create_info_t* create_info,
    amdf_windows_gpu_memory_plan_t* out_plan) {
  const amdf_memory_flags_t supported_flags =
      AMDF_MEMORY_FLAG_HOST_VISIBLE | AMDF_MEMORY_FLAG_DEVICE_LOCAL |
      AMDF_MEMORY_FLAG_EXECUTABLE | AMDF_MEMORY_FLAG_QUEUE_STORAGE |
      AMDF_MEMORY_FLAG_HOST_COHERENT | AMDF_MEMORY_FLAG_DEVICE_ADDRESS;
  if ((create_info->required_flags & ~supported_flags) != 0) {
    return amdf_make_api_status(AMDF_STATUS_CODE_UNSUPPORTED);
  }

  amdf_windows_gpu_memory_plan_t plan = {0};
  plan.achieved_flags = AMDF_MEMORY_FLAG_DEVICE_ADDRESS;
  plan.alignment = create_info->minimum_alignment;
  switch (create_info->memory_class) {
    case AMDF_MEMORY_CLASS_SYSTEM:
      if ((create_info->required_flags & AMDF_MEMORY_FLAG_DEVICE_LOCAL) != 0) {
        return amdf_make_api_status(AMDF_STATUS_CODE_UNSUPPORTED);
      }
      plan.allocation_domain = AMDF_WKMI_BRIDGE_GPU_ALLOCATION_DOMAIN_SYSTEM;
      plan.achieved_flags |= AMDF_MEMORY_FLAG_HOST_VISIBLE;
      if (plan.alignment < AMDF_WINDOWS_GPU_RESERVATION_GRANULARITY) {
        plan.alignment = AMDF_WINDOWS_GPU_RESERVATION_GRANULARITY;
      }
      break;
    case AMDF_MEMORY_CLASS_LOCAL:
      if ((create_info->required_flags &
           (AMDF_MEMORY_FLAG_HOST_VISIBLE | AMDF_MEMORY_FLAG_HOST_COHERENT)) !=
          0) {
        return amdf_make_api_status(AMDF_STATUS_CODE_UNSUPPORTED);
      }
      plan.allocation_domain = AMDF_WKMI_BRIDGE_GPU_ALLOCATION_DOMAIN_LOCAL;
      plan.achieved_flags |= AMDF_MEMORY_FLAG_DEVICE_LOCAL;
      if (plan.alignment < AMDF_WINDOWS_GPU_RESERVATION_GRANULARITY) {
        plan.alignment = AMDF_WINDOWS_GPU_RESERVATION_GRANULARITY;
      }
      break;
    case AMDF_MEMORY_CLASS_REGISTERED_HOST:
      if ((create_info->required_flags & AMDF_MEMORY_FLAG_DEVICE_LOCAL) != 0) {
        return amdf_make_api_status(AMDF_STATUS_CODE_UNSUPPORTED);
      }
      if ((create_info->byte_length & (AMDF_WINDOWS_GPU_PAGE_SIZE - 1)) != 0 ||
          ((uintptr_t)create_info->registered_host_pointer &
           (AMDF_WINDOWS_GPU_PAGE_SIZE - 1)) != 0) {
        return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
      }
      plan.allocation_domain =
          AMDF_WKMI_BRIDGE_GPU_ALLOCATION_DOMAIN_REGISTERED_HOST;
      plan.achieved_flags |= AMDF_MEMORY_FLAG_HOST_VISIBLE;
      if (plan.alignment < AMDF_WINDOWS_GPU_PAGE_SIZE) {
        plan.alignment = AMDF_WINDOWS_GPU_PAGE_SIZE;
      }
      if (((uintptr_t)create_info->registered_host_pointer &
           (plan.alignment - 1)) != 0) {
        return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
      }
      if (create_info->byte_length >
          UINTPTR_MAX - (uintptr_t)create_info->registered_host_pointer) {
        return amdf_make_api_status(AMDF_STATUS_CODE_OUT_OF_RANGE);
      }
      break;
    default:
      return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }

  if (!amdf_windows_gpu_align_up(create_info->byte_length,
                                 AMDF_WINDOWS_GPU_PAGE_SIZE,
                                 &plan.byte_length)) {
    return amdf_make_api_status(AMDF_STATUS_CODE_OUT_OF_RANGE);
  }
  if (plan.byte_length > SIZE_MAX) {
    return amdf_make_api_status(AMDF_STATUS_CODE_OUT_OF_RANGE);
  }
  if ((create_info->required_flags & AMDF_MEMORY_FLAG_HOST_COHERENT) != 0) {
    plan.allocation_flags |= AMDF_WKMI_BRIDGE_GPU_ALLOCATION_FLAG_FINE_GRAIN;
    plan.achieved_flags |= AMDF_MEMORY_FLAG_HOST_COHERENT;
  }
  if ((create_info->required_flags & AMDF_MEMORY_FLAG_QUEUE_STORAGE) != 0) {
    plan.allocation_flags |= AMDF_WKMI_BRIDGE_GPU_ALLOCATION_FLAG_QUEUE_STORAGE;
    plan.achieved_flags |= AMDF_MEMORY_FLAG_QUEUE_STORAGE;
  }
  if ((create_info->required_flags & AMDF_MEMORY_FLAG_EXECUTABLE) != 0) {
    plan.achieved_flags |= AMDF_MEMORY_FLAG_EXECUTABLE;
  }
  *out_plan = plan;
  return AMDF_STATUS_OK;
}

static amdf_status_t amdf_windows_gpu_memory_allocate_host_storage(
    const amdf_windows_gpu_memory_plan_t* plan, amdf_gpu_umd_memory_t* memory) {
  uint64_t reservation_byte_length = plan->byte_length;
  if (plan->alignment > AMDF_WINDOWS_GPU_RESERVATION_GRANULARITY) {
    const uint64_t alignment_slack =
        plan->alignment - AMDF_WINDOWS_GPU_RESERVATION_GRANULARITY;
    if (reservation_byte_length > UINT64_MAX - alignment_slack) {
      return amdf_make_api_status(AMDF_STATUS_CODE_OUT_OF_RANGE);
    }
    reservation_byte_length += alignment_slack;
  }
  if (reservation_byte_length > SIZE_MAX) {
    return amdf_make_api_status(AMDF_STATUS_CODE_OUT_OF_RANGE);
  }

  memory->host_reservation = VirtualAlloc(NULL, (SIZE_T)reservation_byte_length,
                                          MEM_RESERVE, PAGE_READWRITE);
  if (memory->host_reservation == NULL) {
    return amdf_make_status(AMDF_STATUS_DOMAIN_WIN32, GetLastError());
  }
  uint64_t aligned_pointer = 0;
  if (!amdf_windows_gpu_align_up((uint64_t)(uintptr_t)memory->host_reservation,
                                 plan->alignment, &aligned_pointer)) {
    return amdf_make_api_status(AMDF_STATUS_CODE_OUT_OF_RANGE);
  }
  memory->host_pointer =
      VirtualAlloc((void*)(uintptr_t)aligned_pointer, (SIZE_T)plan->byte_length,
                   MEM_COMMIT, PAGE_READWRITE);
  if (memory->host_pointer == NULL) {
    return amdf_make_status(AMDF_STATUS_DOMAIN_WIN32, GetLastError());
  }
  return AMDF_STATUS_OK;
}

static amdf_status_t amdf_windows_gpu_memory_reserve_device_address(
    const amdf_windows_gpu_memory_plan_t* plan, amdf_gpu_umd_memory_t* memory) {
  uint64_t usable_reservation_byte_length = 0;
  if (!amdf_windows_gpu_align_up(plan->byte_length,
                                 AMDF_WINDOWS_GPU_RESERVATION_GRANULARITY,
                                 &usable_reservation_byte_length)) {
    return amdf_make_api_status(AMDF_STATUS_CODE_OUT_OF_RANGE);
  }
  uint64_t reservation_byte_length = usable_reservation_byte_length;
  if (plan->alignment > AMDF_WINDOWS_GPU_RESERVATION_GRANULARITY) {
    const uint64_t alignment_slack =
        plan->alignment - AMDF_WINDOWS_GPU_RESERVATION_GRANULARITY;
    if (reservation_byte_length > UINT64_MAX - alignment_slack) {
      return amdf_make_api_status(AMDF_STATUS_CODE_OUT_OF_RANGE);
    }
    reservation_byte_length += alignment_slack;
  }

  D3DDDI_RESERVEGPUVIRTUALADDRESS reserve = {0};
  reserve.hAdapter = memory->device->adapter;
  reserve.Size = reservation_byte_length;
  const amdf_status_t status = amdf_kmt_make_status(
      memory->device->kmt->reserve_gpu_virtual_address(&reserve));
  if (!amdf_status_is_ok(status)) {
    return status;
  }
  memory->device_reservation_base = reserve.VirtualAddress;
  memory->device_reservation_byte_length = reservation_byte_length;
  if (!amdf_windows_gpu_align_up(reserve.VirtualAddress, plan->alignment,
                                 &memory->device_address) ||
      memory->device_address < reserve.VirtualAddress ||
      memory->device_address - reserve.VirtualAddress >
          reservation_byte_length - usable_reservation_byte_length) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INTERNAL);
  }
  return AMDF_STATUS_OK;
}

static amdf_status_t amdf_windows_gpu_memory_create_allocations(
    const amdf_windows_gpu_memory_plan_t* plan, amdf_gpu_umd_memory_t* memory) {
  amdf_wkmi_bridge_gpu_allocation_create_info_t create_info = {0};
  create_info.structure_size = sizeof(create_info);
  create_info.device_handle = memory->device->device;
  create_info.domain = plan->allocation_domain;
  create_info.flags = plan->allocation_flags;
  create_info.byte_length = plan->byte_length;
  if (plan->allocation_domain == AMDF_WKMI_BRIDGE_GPU_ALLOCATION_DOMAIN_LOCAL) {
    create_info.placement_device_address = memory->device_address;
  } else {
    create_info.host_pointer = memory->host_pointer;
  }
  uint32_t created_allocation_count = 0;
  const amdf_status_t status = amdf_gpu_wddm_wkmi_adapter_create_allocations(
      &memory->device->wkmi, &create_info, memory->allocation_capacity,
      memory->allocation_handles, &memory->resource, &created_allocation_count);
  memory->allocation_count = created_allocation_count;
  if (amdf_status_is_ok(status) &&
      created_allocation_count != memory->allocation_capacity) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INTERNAL);
  }
  return status;
}

static amdf_status_t amdf_windows_gpu_memory_unmap_range(
    amdf_gpu_umd_memory_t* memory, uint64_t device_address,
    uint64_t byte_length) {
  D3DDDI_MAPGPUVIRTUALADDRESS unmap = {0};
  unmap.hPagingQueue = memory->device->paging_queue;
  unmap.BaseAddress = device_address;
  unmap.SizeInPages = byte_length / AMDF_WINDOWS_GPU_PAGE_SIZE;
  unmap.Protection.NoAccess = 1;
  const NTSTATUS native_status =
      memory->device->kmt->map_gpu_virtual_address(&unmap);
  if (!amdf_kmt_status_is_success_or_pending(native_status)) {
    return amdf_kmt_make_status(native_status);
  }
  return amdf_kmt_wait_for_paging(memory->device->kmt, memory->device->device,
                                  memory->device->paging_sync_object,
                                  memory->device->paging_fence,
                                  unmap.PagingFenceValue);
}

static amdf_status_t amdf_windows_gpu_memory_rollback_unexpected_mapping(
    amdf_gpu_umd_memory_t* memory) {
  if (memory->rollback.mapped_byte_length == 0) {
    return AMDF_STATUS_OK;
  }
  amdf_status_t status = amdf_kmt_wait_for_paging(
      memory->device->kmt, memory->device->device,
      memory->device->paging_sync_object, memory->device->paging_fence,
      memory->rollback.paging_fence_value);
  if (amdf_status_is_ok(status)) {
    status = amdf_windows_gpu_memory_unmap_range(
        memory, memory->rollback.device_address,
        memory->rollback.mapped_byte_length);
  }
  if (amdf_status_is_ok(status)) {
    memory->rollback.device_address = 0;
    memory->rollback.mapped_byte_length = 0;
    memory->rollback.paging_fence_value = 0;
  }
  return status;
}

static amdf_status_t amdf_windows_gpu_memory_map_device_address(
    amdf_gpu_umd_memory_t* memory) {
  uint64_t remaining_byte_length = memory->byte_length;
  uint64_t last_paging_fence_value = 0;
  amdf_status_t status = AMDF_STATUS_OK;
  for (uint32_t i = 0;
       amdf_status_is_ok(status) && i < memory->allocation_count; ++i) {
    const uint64_t chunk_byte_length =
        remaining_byte_length < memory->maximum_native_allocation_byte_length
            ? remaining_byte_length
            : memory->maximum_native_allocation_byte_length;
    D3DDDI_MAPGPUVIRTUALADDRESS map = {0};
    map.hPagingQueue = memory->device->paging_queue;
    map.BaseAddress = memory->device_address + memory->mapped_byte_length;
    map.hAllocation = memory->allocation_handles[i];
    map.SizeInPages = chunk_byte_length / AMDF_WINDOWS_GPU_PAGE_SIZE;
    map.Protection.Write = 1;
    map.Protection.Execute = (memory->flags & AMDF_MEMORY_FLAG_EXECUTABLE) != 0;
    const NTSTATUS native_status =
        memory->device->kmt->map_gpu_virtual_address(&map);
    if (!amdf_kmt_status_is_success_or_pending(native_status)) {
      status = amdf_kmt_make_status(native_status);
    } else if (map.VirtualAddress != map.BaseAddress) {
      memory->rollback.device_address = map.VirtualAddress;
      memory->rollback.mapped_byte_length = chunk_byte_length;
      memory->rollback.paging_fence_value = map.PagingFenceValue;
      status = amdf_windows_gpu_memory_rollback_unexpected_mapping(memory);
      if (amdf_status_is_ok(status)) {
        status = amdf_make_api_status(AMDF_STATUS_CODE_INTERNAL);
      }
    } else {
      memory->mapped_byte_length += chunk_byte_length;
      if (map.PagingFenceValue > last_paging_fence_value) {
        last_paging_fence_value = map.PagingFenceValue;
      }
      remaining_byte_length -= chunk_byte_length;
    }
  }
  if (amdf_status_is_ok(status)) {
    status = amdf_kmt_wait_for_paging(
        memory->device->kmt, memory->device->device,
        memory->device->paging_sync_object, memory->device->paging_fence,
        last_paging_fence_value);
  }
  return status;
}

static amdf_status_t amdf_windows_gpu_memory_make_resident(
    amdf_gpu_umd_memory_t* memory) {
  D3DDDI_MAKERESIDENT make_resident = {0};
  make_resident.hPagingQueue = memory->device->paging_queue;
  make_resident.NumAllocations = memory->allocation_count;
  make_resident.AllocationList = memory->allocation_handles;
  make_resident.Flags.CantTrimFurther = 1;
  const NTSTATUS native_status =
      memory->device->kmt->make_resident(&make_resident);
  if (!amdf_kmt_status_is_success_or_pending(native_status)) {
    return amdf_kmt_make_status(native_status);
  }
  memory->is_resident = 1;
  const amdf_status_t status = amdf_kmt_wait_for_paging(
      memory->device->kmt, memory->device->device,
      memory->device->paging_sync_object, memory->device->paging_fence,
      make_resident.PagingFenceValue);
  if (amdf_status_is_ok(status) &&
      make_resident.NumAllocations != memory->allocation_count) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INTERNAL);
  }
  return status;
}

static amdf_status_t amdf_windows_gpu_memory_unmap_device_address(
    amdf_gpu_umd_memory_t* memory) {
  if (memory->mapped_byte_length == 0) {
    return AMDF_STATUS_OK;
  }
  const amdf_status_t status = amdf_windows_gpu_memory_unmap_range(
      memory, memory->device_address, memory->mapped_byte_length);
  if (amdf_status_is_ok(status)) {
    memory->mapped_byte_length = 0;
  }
  return status;
}

static amdf_status_t amdf_windows_gpu_memory_evict(
    amdf_gpu_umd_memory_t* memory) {
  if (memory->is_resident == 0) {
    return AMDF_STATUS_OK;
  }
  D3DKMT_EVICT evict = {0};
  evict.hDevice = memory->device->device;
  evict.NumAllocations = memory->allocation_count;
  evict.AllocationList = memory->allocation_handles;
  const amdf_status_t status =
      amdf_kmt_make_status(memory->device->kmt->evict(&evict));
  if (amdf_status_is_ok(status)) {
    memory->is_resident = 0;
  }
  return status;
}

static amdf_status_t amdf_windows_gpu_memory_destroy_allocations(
    amdf_gpu_umd_memory_t* memory) {
  if (memory->resource == 0 && memory->allocation_count == 0) {
    return AMDF_STATUS_OK;
  }
  D3DKMT_DESTROYALLOCATION2 destroy = {0};
  destroy.hDevice = memory->device->device;
  destroy.hResource = memory->resource;
  if (memory->resource == 0) {
    destroy.phAllocationList = memory->allocation_handles;
    destroy.AllocationCount = memory->allocation_count;
  }
  destroy.Flags.AssumeNotInUse = 1;
  const amdf_status_t status =
      amdf_kmt_make_status(memory->device->kmt->destroy_allocation(&destroy));
  if (amdf_status_is_ok(status)) {
    memory->resource = 0;
    memory->allocation_count = 0;
  }
  return status;
}

static amdf_status_t amdf_windows_gpu_memory_free_device_address(
    amdf_gpu_umd_memory_t* memory) {
  if (memory->device_reservation_base == 0) {
    return AMDF_STATUS_OK;
  }
  D3DKMT_FREEGPUVIRTUALADDRESS free_address = {0};
  free_address.hAdapter = memory->device->adapter;
  free_address.BaseAddress = memory->device_reservation_base;
  free_address.Size = memory->device_reservation_byte_length;
  const amdf_status_t status = amdf_kmt_make_status(
      memory->device->kmt->free_gpu_virtual_address(&free_address));
  if (amdf_status_is_ok(status)) {
    memory->device_address = 0;
    memory->device_reservation_base = 0;
    memory->device_reservation_byte_length = 0;
  }
  return status;
}

static amdf_status_t amdf_windows_gpu_memory_free_host_storage(
    amdf_gpu_umd_memory_t* memory) {
  if (memory->host_reservation == NULL) {
    return AMDF_STATUS_OK;
  }
  if (!VirtualFree(memory->host_reservation, 0, MEM_RELEASE)) {
    return amdf_make_status(AMDF_STATUS_DOMAIN_WIN32, GetLastError());
  }
  memory->host_reservation = NULL;
  memory->host_pointer = NULL;
  return AMDF_STATUS_OK;
}

static amdf_status_t amdf_windows_gpu_memory_release_native(
    amdf_gpu_umd_memory_t* memory) {
  amdf_status_t status =
      amdf_windows_gpu_memory_rollback_unexpected_mapping(memory);
  if (amdf_status_is_ok(status)) {
    status = amdf_windows_gpu_memory_unmap_device_address(memory);
  }
  if (amdf_status_is_ok(status)) {
    status = amdf_windows_gpu_memory_evict(memory);
  }
  if (amdf_status_is_ok(status)) {
    status = amdf_windows_gpu_memory_destroy_allocations(memory);
  }
  if (amdf_status_is_ok(status)) {
    status = amdf_windows_gpu_memory_free_device_address(memory);
  }
  if (amdf_status_is_ok(status)) {
    status = amdf_windows_gpu_memory_free_host_storage(memory);
  }
  return status;
}

static void amdf_windows_gpu_memory_defer_release(
    amdf_gpu_umd_memory_t* memory) {
  AcquireSRWLockExclusive(&memory->device->deferred_memory_release_lock);
  memory->next_deferred_release = memory->device->deferred_memory_release_head;
  memory->device->deferred_memory_release_head = memory;
  ReleaseSRWLockExclusive(&memory->device->deferred_memory_release_lock);
}

amdf_status_t amdf_gpu_wddm_device_drain_memory_releases(
    amdf_gpu_umd_device_t* device) {
  AcquireSRWLockExclusive(&device->deferred_memory_release_lock);
  amdf_status_t status = AMDF_STATUS_OK;
  while (amdf_status_is_ok(status) &&
         device->deferred_memory_release_head != NULL) {
    amdf_gpu_umd_memory_t* memory = device->deferred_memory_release_head;
    status = amdf_windows_gpu_memory_release_native(memory);
    if (amdf_status_is_ok(status)) {
      device->deferred_memory_release_head = memory->next_deferred_release;
      free(memory);
    }
  }
  ReleaseSRWLockExclusive(&device->deferred_memory_release_lock);
  return status;
}

amdf_status_t amdf_gpu_umd_memory_create(
    amdf_gpu_umd_device_t* device, const amdf_memory_create_info_t* create_info,
    amdf_gpu_umd_memory_t** out_memory,
    amdf_gpu_umd_memory_result_t* out_result) {
  *out_memory = NULL;
  if (!amdf_kmt_api_supports_gpu_memory(device->kmt)) {
    return amdf_make_api_status(AMDF_STATUS_CODE_UNSUPPORTED);
  }
  amdf_windows_gpu_memory_plan_t plan = {0};
  amdf_status_t status =
      amdf_windows_gpu_memory_validate_create_info(create_info, &plan);
  if (!amdf_status_is_ok(status)) {
    return status;
  }

  uint32_t allocation_count = 0;
  uint64_t maximum_native_allocation_byte_length = 0;
  status = amdf_gpu_wddm_wkmi_adapter_query_allocation_layout(
      &device->wkmi, plan.byte_length, &allocation_count,
      &maximum_native_allocation_byte_length);
  if (!amdf_status_is_ok(status)) {
    return status;
  }
  if (allocation_count == 0 || maximum_native_allocation_byte_length == 0) {
    return amdf_make_api_status(AMDF_STATUS_CODE_OUT_OF_RANGE);
  }

  const size_t memory_size =
      offsetof(amdf_gpu_umd_memory_t, allocation_handles) +
      (size_t)allocation_count * sizeof(D3DKMT_HANDLE);
  amdf_gpu_umd_memory_t* memory =
      (amdf_gpu_umd_memory_t*)calloc(1, memory_size);
  if (memory == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_RESOURCE_EXHAUSTED);
  }
  memory->device = device;
  memory->allocation_capacity = allocation_count;
  memory->maximum_native_allocation_byte_length =
      maximum_native_allocation_byte_length;
  memory->byte_length = plan.byte_length;
  memory->flags = plan.achieved_flags;
  if (create_info->memory_class == AMDF_MEMORY_CLASS_SYSTEM) {
    status = amdf_windows_gpu_memory_allocate_host_storage(&plan, memory);
  } else if (create_info->memory_class == AMDF_MEMORY_CLASS_REGISTERED_HOST) {
    memory->host_pointer = create_info->registered_host_pointer;
  }
  if (amdf_status_is_ok(status)) {
    status = amdf_windows_gpu_memory_reserve_device_address(&plan, memory);
  }
  if (amdf_status_is_ok(status)) {
    status = amdf_windows_gpu_memory_create_allocations(&plan, memory);
  }
  if (amdf_status_is_ok(status)) {
    status = amdf_windows_gpu_memory_map_device_address(memory);
  }
  if (amdf_status_is_ok(status)) {
    status = amdf_windows_gpu_memory_make_resident(memory);
  }

  if (amdf_status_is_ok(status)) {
    amdf_gpu_umd_memory_result_t result = {0};
    result.memory_class = create_info->memory_class;
    result.flags = plan.achieved_flags;
    result.byte_length = plan.byte_length;
    result.alignment = plan.alignment;
    if (create_info->memory_class == AMDF_MEMORY_CLASS_REGISTERED_HOST) {
      result.physical_backing_id.words[0] =
          (uint64_t)(uintptr_t)memory->host_pointer;
      result.physical_backing_id.words[1] = memory->byte_length;
    } else {
      result.physical_backing_id.words[0] =
          ((uint64_t)memory->device->device << 32) |
          (memory->resource != 0 ? memory->resource
                                 : memory->allocation_handles[0]);
      result.physical_backing_id.words[1] =
          ((uint64_t)memory->allocation_count << 32) |
          memory->allocation_handles[memory->allocation_count - 1];
    }
    result.device_address = memory->device_address;
    *out_result = result;
    *out_memory = memory;
  } else {
    const amdf_status_t release_status =
        amdf_windows_gpu_memory_release_native(memory);
    if (!amdf_status_is_ok(release_status)) {
      amdf_windows_gpu_memory_defer_release(memory);
      return release_status;
    }
    free(memory);
  }
  return status;
}

amdf_status_t amdf_gpu_umd_memory_destroy(amdf_gpu_umd_memory_t* memory) {
  const amdf_status_t status = amdf_windows_gpu_memory_release_native(memory);
  if (amdf_status_is_ok(status)) {
    free(memory);
  }
  return status;
}

amdf_status_t amdf_gpu_umd_memory_map(
    amdf_gpu_umd_memory_t* memory, const amdf_memory_map_info_t* map_info,
    amdf_gpu_umd_host_mapping_t** out_mapping,
    amdf_gpu_umd_host_mapping_result_t* out_result) {
  *out_mapping = NULL;
  amdf_gpu_umd_host_mapping_t* mapping =
      (amdf_gpu_umd_host_mapping_t*)calloc(1, sizeof(*mapping));
  if (mapping == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_RESOURCE_EXHAUSTED);
  }
  mapping->memory = memory;
  mapping->memory_byte_offset = map_info->byte_offset;
  mapping->pointer = (uint8_t*)memory->host_pointer + map_info->byte_offset;
  mapping->byte_length = map_info->byte_length;

  amdf_gpu_umd_host_mapping_result_t result = {0};
  result.flags = map_info->flags;
  result.pointer = mapping->pointer;
  result.byte_length = mapping->byte_length;
  result.cacheability = (memory->flags & AMDF_MEMORY_FLAG_HOST_COHERENT) != 0
                            ? AMDF_HOST_CACHEABILITY_COHERENT
                            : AMDF_HOST_CACHEABILITY_WRITE_BACK;
  result.cache_line_size = amdf_windows_host_cache_line_size();
  *out_result = result;
  *out_mapping = mapping;
  return AMDF_STATUS_OK;
}

amdf_status_t amdf_gpu_umd_host_mapping_cache_control(
    amdf_gpu_umd_host_mapping_t* mapping, amdf_host_cache_operation_t operation,
    uint64_t byte_offset, uint64_t byte_length) {
  amdf_status_t status = amdf_windows_host_cache_control(
      operation, (uint8_t*)mapping->pointer + byte_offset, byte_length);
  if (!amdf_status_is_ok(status) || byte_length == 0 ||
      operation != AMDF_HOST_CACHE_OPERATION_FLUSH) {
    return status;
  }

  amdf_gpu_umd_memory_t* memory = mapping->memory;
  uint64_t allocation_byte_offset = mapping->memory_byte_offset + byte_offset;
  uint64_t remaining_byte_length = byte_length;
  while (remaining_byte_length != 0) {
    const uint64_t allocation_index =
        allocation_byte_offset / memory->maximum_native_allocation_byte_length;
    const uint64_t native_byte_offset =
        allocation_byte_offset % memory->maximum_native_allocation_byte_length;
    const uint64_t native_available_byte_length =
        memory->maximum_native_allocation_byte_length - native_byte_offset;
    const uint64_t native_byte_length =
        remaining_byte_length < native_available_byte_length
            ? remaining_byte_length
            : native_available_byte_length;
    D3DKMT_INVALIDATECACHE invalidate = {0};
    invalidate.hDevice = memory->device->device;
    invalidate.hAllocation = memory->allocation_handles[allocation_index];
    invalidate.Offset = native_byte_offset;
    invalidate.Length = native_byte_length;
    status = amdf_kmt_make_status(
        memory->device->kmt->invalidate_cache(&invalidate));
    if (!amdf_status_is_ok(status)) {
      return status;
    }
    allocation_byte_offset += native_byte_length;
    remaining_byte_length -= native_byte_length;
  }
  return AMDF_STATUS_OK;
}

amdf_status_t amdf_gpu_umd_host_mapping_destroy(
    amdf_gpu_umd_host_mapping_t* mapping) {
  free(mapping);
  return AMDF_STATUS_OK;
}
