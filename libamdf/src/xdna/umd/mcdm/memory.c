// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/xdna/umd/memory.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "libamdf/src/platform/windows/host_cache.h"
#include "libamdf/src/xdna/umd/mcdm/device.h"

#define AMDF_WINDOWS_XDNA_ALLOCATION_ALIGNMENT UINT64_C(65536)
#define AMDF_WINDOWS_KMT_PAGE_SIZE UINT64_C(4096)

struct amdf_xdna_umd_memory_t {
  // Next device-owned record awaiting a failed construction rollback retry.
  amdf_xdna_umd_memory_t* next_deferred_release;
  // Device borrowed while this attachment remains live.
  amdf_xdna_umd_device_t* device;
  // KMT resource owning the allocation, or zero for an ungrouped allocation.
  D3DKMT_HANDLE resource;
  // KMT physical allocation attached to the XDNA device.
  D3DKMT_HANDLE allocation;
  // Provider-owned host base used by the standard allocation.
  void* host_pointer;
  // Physical allocation length in bytes.
  uint64_t byte_length;
  // Stable XDNA virtual base established before publication.
  uint64_t device_address;
};

struct amdf_xdna_umd_host_mapping_t {
  // First byte exposed by this mapping.
  void* pointer;
  // Exposed byte length.
  uint64_t byte_length;
};

static amdf_status_t amdf_windows_xdna_memory_release_native(
    amdf_xdna_umd_memory_t* memory) {
  if (memory->resource != 0 || memory->allocation != 0) {
    D3DKMT_DESTROYALLOCATION2 destroy = {0};
    destroy.hDevice = memory->device->device;
    destroy.hResource = memory->resource;
    if (memory->resource == 0) {
      destroy.phAllocationList = &memory->allocation;
      destroy.AllocationCount = 1;
    }
    destroy.Flags.AssumeNotInUse = 1;
    const amdf_status_t status =
        amdf_kmt_make_status(memory->device->kmt->destroy_allocation(&destroy));
    if (!amdf_status_is_ok(status)) {
      return status;
    }
    memory->resource = 0;
    memory->allocation = 0;
    memory->device_address = 0;
  }
  if (memory->host_pointer != NULL) {
    if (!VirtualFree(memory->host_pointer, 0, MEM_RELEASE)) {
      return amdf_make_status(AMDF_STATUS_DOMAIN_WIN32, GetLastError());
    }
    memory->host_pointer = NULL;
    memory->byte_length = 0;
  }
  return AMDF_STATUS_OK;
}

static void amdf_windows_xdna_memory_defer_release(
    amdf_xdna_umd_memory_t* memory) {
  AcquireSRWLockExclusive(&memory->device->deferred_memory_release_lock);
  memory->next_deferred_release = memory->device->deferred_memory_release_head;
  memory->device->deferred_memory_release_head = memory;
  ReleaseSRWLockExclusive(&memory->device->deferred_memory_release_lock);
}

amdf_status_t amdf_windows_xdna_device_drain_memory_releases(
    amdf_xdna_umd_device_t* device) {
  AcquireSRWLockExclusive(&device->deferred_memory_release_lock);
  amdf_status_t status = AMDF_STATUS_OK;
  while (amdf_status_is_ok(status) &&
         device->deferred_memory_release_head != NULL) {
    amdf_xdna_umd_memory_t* memory = device->deferred_memory_release_head;
    status = amdf_windows_xdna_memory_release_native(memory);
    if (amdf_status_is_ok(status)) {
      device->deferred_memory_release_head = memory->next_deferred_release;
      free(memory);
    }
  }
  ReleaseSRWLockExclusive(&device->deferred_memory_release_lock);
  return status;
}

static amdf_status_t amdf_windows_xdna_memory_validate_create_info(
    const amdf_memory_create_info_t* create_info, uint64_t* out_byte_length) {
  const amdf_memory_flags_t supported_flags =
      AMDF_MEMORY_FLAG_HOST_VISIBLE | AMDF_MEMORY_FLAG_DEVICE_ADDRESS;
  if (create_info->memory_class != AMDF_MEMORY_CLASS_SYSTEM ||
      (create_info->required_flags & ~supported_flags) != 0) {
    return amdf_make_api_status(AMDF_STATUS_CODE_UNSUPPORTED);
  }
  if (create_info->minimum_alignment > AMDF_WINDOWS_XDNA_ALLOCATION_ALIGNMENT) {
    return amdf_make_api_status(AMDF_STATUS_CODE_UNSUPPORTED);
  }
  if (create_info->byte_length >
      UINT64_MAX - (AMDF_WINDOWS_XDNA_ALLOCATION_ALIGNMENT - 1)) {
    return amdf_make_api_status(AMDF_STATUS_CODE_OUT_OF_RANGE);
  }
  const uint64_t byte_length = (create_info->byte_length +
                                (AMDF_WINDOWS_XDNA_ALLOCATION_ALIGNMENT - 1)) &
                               ~(AMDF_WINDOWS_XDNA_ALLOCATION_ALIGNMENT - 1);
  if (byte_length > SIZE_MAX) {
    return amdf_make_api_status(AMDF_STATUS_CODE_OUT_OF_RANGE);
  }
  *out_byte_length = byte_length;
  return AMDF_STATUS_OK;
}

static amdf_status_t amdf_windows_xdna_memory_create_allocation(
    amdf_xdna_umd_memory_t* memory) {
  D3DDDI_ALLOCATIONINFO2 allocation_info = {0};
  allocation_info.pSystemMem = memory->host_pointer;
  allocation_info.VidPnSourceId = D3DDDI_ID_UNINITIALIZED;

  D3DKMT_CREATESTANDARDALLOCATION standard_allocation = {0};
  standard_allocation.Type = D3DKMT_STANDARDALLOCATIONTYPE_EXISTINGHEAP;
  standard_allocation.ExistingHeapData.Size =
      (D3DKMT_SIZE_T)memory->byte_length;

  D3DKMT_CREATEALLOCATION create = {0};
  create.hDevice = memory->device->device;
  create.pStandardAllocation = &standard_allocation;
  create.NumAllocations = 1;
  create.pAllocationInfo2 = &allocation_info;
  create.Flags.StandardAllocation = 1;
  create.Flags.ExistingSysMem = 1;
  amdf_status_t status =
      amdf_kmt_make_status(memory->device->kmt->create_allocation(&create));
  if (amdf_status_is_ok(status)) {
    memory->resource = create.hResource;
    memory->allocation = allocation_info.hAllocation;
    if (memory->allocation == 0) {
      status = amdf_make_api_status(AMDF_STATUS_CODE_INTERNAL);
    }
  }
  return status;
}

static amdf_status_t amdf_windows_xdna_memory_map_device_address(
    amdf_xdna_umd_memory_t* memory) {
  D3DDDI_MAPGPUVIRTUALADDRESS map = {0};
  map.hPagingQueue = memory->device->paging_queue;
  map.hAllocation = memory->allocation;
  map.SizeInPages = memory->byte_length / AMDF_WINDOWS_KMT_PAGE_SIZE;
  map.Protection.Write = 1;
  NTSTATUS native_status = memory->device->kmt->map_gpu_virtual_address(&map);
  if (!amdf_kmt_status_is_success_or_pending(native_status)) {
    return amdf_kmt_make_status(native_status);
  }
  amdf_status_t status = amdf_kmt_wait_for_paging(
      memory->device->kmt, memory->device->device,
      memory->device->paging_sync_object, memory->device->paging_fence,
      map.PagingFenceValue);
  if (amdf_status_is_ok(status)) {
    memory->device_address = map.VirtualAddress;
    if (memory->device_address == 0 ||
        (memory->device_address &
         (AMDF_WINDOWS_XDNA_ALLOCATION_ALIGNMENT - 1)) != 0) {
      status = amdf_make_api_status(AMDF_STATUS_CODE_INTERNAL);
    }
  }
  return status;
}

static amdf_status_t amdf_windows_xdna_memory_make_resident(
    amdf_xdna_umd_memory_t* memory) {
  const D3DKMT_HANDLE allocation = memory->allocation;
  D3DDDI_MAKERESIDENT make_resident = {0};
  make_resident.hPagingQueue = memory->device->paging_queue;
  make_resident.NumAllocations = 1;
  make_resident.AllocationList = &allocation;
  const NTSTATUS native_status =
      memory->device->kmt->make_resident(&make_resident);
  if (!amdf_kmt_status_is_success_or_pending(native_status)) {
    return amdf_kmt_make_status(native_status);
  }
  const amdf_status_t status = amdf_kmt_wait_for_paging(
      memory->device->kmt, memory->device->device,
      memory->device->paging_sync_object, memory->device->paging_fence,
      make_resident.PagingFenceValue);
  if (amdf_status_is_ok(status) && make_resident.NumAllocations != 1) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INTERNAL);
  }
  return status;
}

amdf_status_t amdf_xdna_umd_memory_create(
    amdf_xdna_umd_device_t* device,
    const amdf_memory_create_info_t* create_info,
    amdf_xdna_umd_memory_t** out_memory,
    amdf_xdna_umd_memory_result_t* out_result) {
  *out_memory = NULL;
  if (!amdf_kmt_api_supports_memory(device->kmt)) {
    return amdf_make_api_status(AMDF_STATUS_CODE_UNSUPPORTED);
  }
  uint64_t byte_length = 0;
  amdf_status_t status =
      amdf_windows_xdna_memory_validate_create_info(create_info, &byte_length);
  if (!amdf_status_is_ok(status)) {
    return status;
  }

  amdf_xdna_umd_memory_t* memory =
      (amdf_xdna_umd_memory_t*)calloc(1, sizeof(*memory));
  if (memory == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_RESOURCE_EXHAUSTED);
  }
  memory->device = device;
  memory->byte_length = byte_length;
  memory->host_pointer = VirtualAlloc(NULL, (SIZE_T)byte_length,
                                      MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  if (memory->host_pointer == NULL) {
    status = amdf_make_status(AMDF_STATUS_DOMAIN_WIN32, GetLastError());
  }
  if (amdf_status_is_ok(status)) {
    status = amdf_windows_xdna_memory_create_allocation(memory);
  }
  if (amdf_status_is_ok(status)) {
    status = amdf_windows_xdna_memory_map_device_address(memory);
  }
  if (amdf_status_is_ok(status)) {
    status = amdf_windows_xdna_memory_make_resident(memory);
  }

  if (amdf_status_is_ok(status)) {
    amdf_xdna_umd_memory_result_t result = {0};
    result.memory_class = AMDF_MEMORY_CLASS_SYSTEM;
    result.flags =
        AMDF_MEMORY_FLAG_HOST_VISIBLE | AMDF_MEMORY_FLAG_DEVICE_ADDRESS;
    result.byte_length = memory->byte_length;
    result.alignment = AMDF_WINDOWS_XDNA_ALLOCATION_ALIGNMENT;
    result.physical_backing_id.words[0] =
        (uint64_t)(uintptr_t)memory->host_pointer;
    result.physical_backing_id.words[1] = memory->byte_length;
    result.device_address = memory->device_address;
    *out_result = result;
    *out_memory = memory;
  } else {
    const amdf_status_t release_status =
        amdf_windows_xdna_memory_release_native(memory);
    if (!amdf_status_is_ok(release_status)) {
      amdf_windows_xdna_memory_defer_release(memory);
      return release_status;
    }
    free(memory);
  }
  return status;
}

amdf_status_t amdf_xdna_umd_memory_destroy(amdf_xdna_umd_memory_t* memory) {
  const amdf_status_t status = amdf_windows_xdna_memory_release_native(memory);
  if (amdf_status_is_ok(status)) {
    free(memory);
  }
  return status;
}

amdf_status_t amdf_xdna_umd_memory_map(
    amdf_xdna_umd_memory_t* memory, const amdf_memory_map_info_t* map_info,
    amdf_xdna_umd_host_mapping_t** out_mapping,
    amdf_xdna_umd_host_mapping_result_t* out_result) {
  *out_mapping = NULL;
  amdf_xdna_umd_host_mapping_t* mapping =
      (amdf_xdna_umd_host_mapping_t*)calloc(1, sizeof(*mapping));
  if (mapping == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_RESOURCE_EXHAUSTED);
  }
  mapping->pointer = (uint8_t*)memory->host_pointer + map_info->byte_offset;
  mapping->byte_length = map_info->byte_length;

  amdf_xdna_umd_host_mapping_result_t result = {0};
  result.flags = map_info->flags;
  result.pointer = mapping->pointer;
  result.byte_length = mapping->byte_length;
  result.cacheability = AMDF_HOST_CACHEABILITY_WRITE_BACK;
  result.cache_line_size = amdf_windows_host_cache_line_size();
  *out_result = result;
  *out_mapping = mapping;
  return AMDF_STATUS_OK;
}

amdf_status_t amdf_xdna_umd_host_mapping_cache_control(
    amdf_xdna_umd_host_mapping_t* mapping,
    amdf_host_cache_operation_t operation, uint64_t byte_offset,
    uint64_t byte_length) {
  return amdf_windows_host_cache_control(
      operation, (uint8_t*)mapping->pointer + byte_offset, byte_length);
}

amdf_status_t amdf_xdna_umd_host_mapping_destroy(
    amdf_xdna_umd_host_mapping_t* mapping) {
  free(mapping);
  return AMDF_STATUS_OK;
}
