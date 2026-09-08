// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/gpu/umd/device.h"

#include <stddef.h>
#include <stdlib.h>

#include "libamdf/src/gpu/umd/wddm/device.h"
#include "libamdf/src/gpu/umd/wddm/memory.h"
#include "libamdf/src/platform/windows/endpoint.h"

static amdf_status_t amdf_gpu_wddm_device_release_native(
    amdf_gpu_umd_device_t* device) {
  amdf_status_t status = amdf_gpu_wddm_device_drain_memory_releases(device);
  if (!amdf_status_is_ok(status)) {
    return status;
  }
  status = amdf_gpu_wddm_wkmi_adapter_deinitialize(&device->wkmi);
  if (!amdf_status_is_ok(status)) {
    return status;
  }
  if (device->paging_queue != 0) {
    D3DDDI_DESTROYPAGINGQUEUE destroy_paging_queue = {0};
    destroy_paging_queue.hPagingQueue = device->paging_queue;
    status = amdf_kmt_make_status(
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
    status = amdf_kmt_make_status(device->kmt->destroy_device(&destroy_device));
    if (!amdf_status_is_ok(status)) {
      return status;
    }
    device->device = 0;
  }
  return AMDF_STATUS_OK;
}

amdf_status_t amdf_gpu_umd_device_create(
    amdf_platform_endpoint_t* endpoint, amdf_gpu_umd_device_t** out_device,
    amdf_gpu_umd_device_result_t* out_result) {
  *out_device = NULL;
  if (!amdf_kmt_api_supports_paging_devices(&endpoint->instance->kmt)) {
    return amdf_make_api_status(AMDF_STATUS_CODE_UNSUPPORTED);
  }

  amdf_gpu_umd_device_t* device =
      (amdf_gpu_umd_device_t*)calloc(1, sizeof(*device));
  if (device == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_RESOURCE_EXHAUSTED);
  }
  InitializeSRWLock(&device->deferred_memory_release_lock);
  amdf_kmt_device_status_initialize(&device->status);
  device->kmt = &endpoint->instance->kmt;
  device->adapter = endpoint->adapter;
  device->physical_adapter_index = endpoint->physical_adapter_index;

  amdf_wkmi_bridge_gpu_properties_t properties = {0};
  bool wkmi_available = false;
  amdf_status_t status = amdf_gpu_wddm_wkmi_adapter_initialize(
      endpoint->adapter, endpoint->physical_adapter_index, &device->wkmi,
      &properties, &wkmi_available);
  (void)properties;
  if (amdf_status_is_ok(status) && !wkmi_available) {
    status = amdf_make_api_status(AMDF_STATUS_CODE_UNSUPPORTED);
  }

  D3DKMT_CREATEDEVICE create_device = {0};
  create_device.hAdapter = endpoint->adapter;
  if (amdf_status_is_ok(status)) {
    status = amdf_kmt_make_status(device->kmt->create_device(&create_device));
  }
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

  if (amdf_status_is_ok(status)) {
    amdf_gpu_umd_device_result_t result = {0};
    result.id.words[0] = endpoint->id.words[0];
    result.id.words[1] =
        ((uint64_t)device->paging_queue << 32) | device->device;
    result.reset_epoch = 1;
    *out_result = result;
    *out_device = device;
  } else {
    const amdf_status_t release_status =
        amdf_gpu_wddm_device_release_native(device);
    if (!amdf_status_is_ok(release_status)) {
      status = release_status;
    }
    free(device);
  }
  return status;
}

amdf_status_t amdf_gpu_umd_device_destroy(amdf_gpu_umd_device_t* device) {
  const amdf_status_t status = amdf_gpu_wddm_device_release_native(device);
  if (amdf_status_is_ok(status)) {
    free(device);
  }
  return status;
}
