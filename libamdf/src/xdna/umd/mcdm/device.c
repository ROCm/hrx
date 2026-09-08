// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/xdna/umd/device.h"

#include <stddef.h>
#include <stdlib.h>

#include "libamdf/src/platform/windows/endpoint.h"
#include "libamdf/src/xdna/umd/mcdm/device.h"
#include "libamdf/src/xdna/umd/mcdm/kernel_execution.h"
#include "libamdf/src/xdna/umd/mcdm/legacy_context.h"

static amdf_status_t amdf_windows_xdna_query_legacy_context_abi(
    const amdf_platform_endpoint_t* endpoint) {
  uint32_t private_info[2] = {0};
  const amdf_status_t status = amdf_kmt_query_adapter_info(
      &endpoint->instance->kmt, endpoint->adapter, KMTQAITYPE_UMDRIVERPRIVATE,
      private_info, sizeof(private_info));
  if (!amdf_status_is_ok(status)) {
    return status;
  }
  if (private_info[0] != 0 || private_info[1] != 3) {
    return amdf_make_api_status(AMDF_STATUS_CODE_UNSUPPORTED);
  }
  return AMDF_STATUS_OK;
}

static amdf_status_t amdf_windows_xdna_device_release_native(
    amdf_xdna_umd_device_t* device) {
  if (device->kernel_execution != NULL) {
    const amdf_status_t status =
        amdf_windows_xdna_kernel_execution_destroy(device->kernel_execution);
    if (!amdf_status_is_ok(status)) {
      return status;
    }
    device->kernel_execution = NULL;
  }
  const amdf_status_t memory_status =
      amdf_windows_xdna_device_drain_memory_releases(device);
  if (!amdf_status_is_ok(memory_status)) {
    return memory_status;
  }
  if (device->context != 0) {
    D3DKMT_DESTROYCONTEXT destroy_context = {0};
    destroy_context.hContext = device->context;
    const amdf_status_t status =
        amdf_kmt_make_status(device->kmt->destroy_context(&destroy_context));
    if (!amdf_status_is_ok(status)) {
      return status;
    }
    device->context = 0;
  }
  if (device->paging_queue != 0) {
    D3DDDI_DESTROYPAGINGQUEUE destroy_paging_queue = {0};
    destroy_paging_queue.hPagingQueue = device->paging_queue;
    const amdf_status_t status = amdf_kmt_make_status(
        device->kmt->destroy_paging_queue(&destroy_paging_queue));
    if (!amdf_status_is_ok(status)) {
      return status;
    }
    device->paging_queue = 0;
    device->paging_sync_object = 0;
    device->paging_fence = NULL;
  }
  if (device->device != 0) {
    D3DKMT_DESTROYDEVICE destroy_device = {0};
    destroy_device.hDevice = device->device;
    const amdf_status_t status =
        amdf_kmt_make_status(device->kmt->destroy_device(&destroy_device));
    if (!amdf_status_is_ok(status)) {
      return status;
    }
    device->device = 0;
  }
  return AMDF_STATUS_OK;
}

amdf_status_t amdf_xdna_umd_device_create(
    amdf_platform_endpoint_t* endpoint,
    const amdf_xdna_endpoint_profile_t* profile,
    const amdf_xdna_device_create_info_t* create_info,
    amdf_xdna_umd_device_t** out_device,
    amdf_xdna_umd_device_result_t* out_result) {
  *out_device = NULL;
  if (!amdf_kmt_api_supports_device_contexts(&endpoint->instance->kmt)) {
    return amdf_make_api_status(AMDF_STATUS_CODE_UNSUPPORTED);
  }
  if (profile->model != AMDF_PCI_XDNA_MODEL_NPU5 ||
      (create_info->acceptable_scheduling_modes &
       AMDF_XDNA_SCHEDULING_MODE_TIME_SLICED) == 0 ||
      (create_info->physical_column_origin !=
           AMDF_XDNA_PHYSICAL_COLUMN_ORIGIN_ANY &&
       create_info->physical_column_origin !=
           profile->info->array.column_origin)) {
    return amdf_make_api_status(AMDF_STATUS_CODE_UNSUPPORTED);
  }
  amdf_status_t status = amdf_windows_xdna_query_legacy_context_abi(endpoint);
  if (!amdf_status_is_ok(status)) {
    return status;
  }

  uint8_t* context_data = NULL;
  uint32_t context_data_size = 0;
  status = amdf_windows_xdna_legacy_context_build(
      create_info->logical_column_count, profile->info->array.column_origin,
      &context_data, &context_data_size);
  if (!amdf_status_is_ok(status)) {
    return status;
  }
  amdf_xdna_umd_device_t* device =
      (amdf_xdna_umd_device_t*)calloc(1, sizeof(*device));
  if (device == NULL) {
    free(context_data);
    return amdf_make_api_status(AMDF_STATUS_CODE_RESOURCE_EXHAUSTED);
  }
  InitializeSRWLock(&device->deferred_memory_release_lock);
  amdf_kmt_device_status_initialize(&device->status);
  device->kmt = &endpoint->instance->kmt;

  D3DKMT_CREATEDEVICE create_device = {0};
  create_device.hAdapter = endpoint->adapter;
  status = amdf_kmt_make_status(device->kmt->create_device(&create_device));
  if (amdf_status_is_ok(status)) {
    device->device = create_device.hDevice;
    if (device->device == 0) {
      status = amdf_make_api_status(AMDF_STATUS_CODE_INTERNAL);
    }
  }

  D3DKMT_CREATEPAGINGQUEUE create_paging_queue = {0};
  if (amdf_status_is_ok(status)) {
    create_paging_queue.hDevice = device->device;
    create_paging_queue.Priority = D3DDDI_PAGINGQUEUE_PRIORITY_NORMAL;
    create_paging_queue.PhysicalAdapterIndex = endpoint->physical_adapter_index;
    status = amdf_kmt_make_status(
        device->kmt->create_paging_queue(&create_paging_queue));
  }
  if (amdf_status_is_ok(status)) {
    device->paging_queue = create_paging_queue.hPagingQueue;
    device->paging_sync_object = create_paging_queue.hSyncObject;
    device->paging_fence = (const volatile uint64_t*)
                               create_paging_queue.FenceValueCPUVirtualAddress;
    if (device->paging_queue == 0 || device->paging_sync_object == 0 ||
        device->paging_fence == NULL) {
      status = amdf_make_api_status(AMDF_STATUS_CODE_INTERNAL);
    }
  }

  D3DKMT_CREATECONTEXTVIRTUAL create_context = {0};
  if (amdf_status_is_ok(status)) {
    create_context.hDevice = device->device;
    create_context.NodeOrdinal = 0;
    create_context.EngineAffinity = 1;
    create_context.Flags.HwQueueSupported = 1;
    create_context.pPrivateDriverData = context_data;
    create_context.PrivateDriverDataSize = context_data_size;
    create_context.ClientHint = (D3DKMT_CLIENTHINT)25;
    status = amdf_kmt_make_status(
        device->kmt->create_context_virtual(&create_context));
  }
  if (amdf_status_is_ok(status)) {
    device->context = create_context.hContext;
    if (device->context == 0) {
      status = amdf_make_api_status(AMDF_STATUS_CODE_INTERNAL);
    } else {
      status = amdf_windows_xdna_legacy_context_query_command_aperture_cookie(
          context_data, context_data_size, &device->command_aperture_cookie);
    }
  }
  free(context_data);

  if (amdf_status_is_ok(status)) {
    status = amdf_windows_xdna_kernel_execution_create(
        device, &device->kernel_execution);
  }
  if (amdf_status_is_ok(status)) {
    amdf_xdna_umd_device_result_t result = {0};
    result.id.words[0] = endpoint->id.words[0];
    result.id.words[1] = ((uint64_t)device->context << 32) | device->device;
    result.reset_epoch = 1;
    result.scheduling_mode = AMDF_XDNA_SCHEDULING_MODE_TIME_SLICED;
    result.placement_generation = 1;
    result.physical_column_origin = profile->info->array.column_origin;
    result.physical_column_count = profile->info->array.column_count;
    *out_result = result;
    *out_device = device;
  } else {
    const amdf_status_t release_status =
        amdf_windows_xdna_device_release_native(device);
    if (!amdf_status_is_ok(release_status)) {
      status = release_status;
    }
    free(device);
  }
  return status;
}

amdf_status_t amdf_xdna_umd_device_destroy(amdf_xdna_umd_device_t* device) {
  const amdf_status_t status = amdf_windows_xdna_device_release_native(device);
  if (amdf_status_is_ok(status)) {
    free(device);
  }
  return status;
}
