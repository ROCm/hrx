// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/gpu/umd/wddm/wkmi/adapter.h"

#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(D3DKMT_HANDLE) == sizeof(uint32_t),
               "WKMI bridge handles must match D3DKMT handles");

static amdf_status_t amdf_gpu_wddm_wkmi_make_status(
    amdf_wkmi_bridge_result_t result, uint32_t native_status) {
  switch (result) {
    case AMDF_WKMI_BRIDGE_RESULT_SUCCESS:
      return AMDF_STATUS_OK;
    case AMDF_WKMI_BRIDGE_RESULT_UNSUPPORTED:
      return amdf_make_api_status(AMDF_STATUS_CODE_UNSUPPORTED);
    case AMDF_WKMI_BRIDGE_RESULT_INVALID_ARGUMENT:
      return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
    case AMDF_WKMI_BRIDGE_RESULT_VERSION_MISMATCH:
      return amdf_make_api_status(AMDF_STATUS_CODE_VERSION_MISMATCH);
    case AMDF_WKMI_BRIDGE_RESULT_NATIVE_FAILURE:
      return amdf_make_status(AMDF_STATUS_DOMAIN_NTSTATUS, native_status);
    case AMDF_WKMI_BRIDGE_RESULT_RESOURCE_EXHAUSTED:
      return amdf_make_api_status(AMDF_STATUS_CODE_RESOURCE_EXHAUSTED);
    case AMDF_WKMI_BRIDGE_RESULT_BUFFER_TOO_SMALL:
      return amdf_make_api_status(AMDF_STATUS_CODE_BUFFER_TOO_SMALL);
    case AMDF_WKMI_BRIDGE_RESULT_OUT_OF_RANGE:
      return amdf_make_api_status(AMDF_STATUS_CODE_OUT_OF_RANGE);
    case AMDF_WKMI_BRIDGE_RESULT_BUSY:
      return amdf_make_api_status(AMDF_STATUS_CODE_BUSY);
    default:
      return amdf_make_api_status(AMDF_STATUS_CODE_INTERNAL);
  }
}

amdf_status_t amdf_gpu_wddm_wkmi_adapter_initialize(
    D3DKMT_HANDLE adapter_handle, uint32_t physical_adapter_index,
    amdf_gpu_wddm_wkmi_adapter_t* out_adapter,
    amdf_wkmi_bridge_gpu_properties_t* out_properties, bool* out_available) {
  *out_adapter = (amdf_gpu_wddm_wkmi_adapter_t){0};
  *out_properties = (amdf_wkmi_bridge_gpu_properties_t){0};
  *out_available = false;

  amdf_gpu_wddm_wkmi_adapter_t adapter = {0};
  amdf_status_t status = amdf_gpu_wddm_wkmi_loader_initialize(&adapter.loader);
  if (amdf_status_is_ok(status)) {
    uint32_t native_status = 0;
    const amdf_wkmi_bridge_result_t result =
        adapter.loader.api->gpu_adapter_open(
            (uint32_t)adapter_handle, physical_adapter_index, &adapter.native,
            out_properties, &native_status);
    if (result == AMDF_WKMI_BRIDGE_RESULT_UNSUPPORTED) {
      status = AMDF_STATUS_OK;
    } else {
      status = amdf_gpu_wddm_wkmi_make_status(result, native_status);
      *out_available = amdf_status_is_ok(status);
    }
  }

  if (*out_available) {
    *out_adapter = adapter;
  } else if (adapter.loader.module != NULL) {
    const amdf_status_t cleanup_status =
        amdf_gpu_wddm_wkmi_adapter_deinitialize(&adapter);
    if (!amdf_status_is_ok(cleanup_status)) {
      status = cleanup_status;
    }
  }
  if (!amdf_status_is_ok(status)) {
    *out_properties = (amdf_wkmi_bridge_gpu_properties_t){0};
  }
  return status;
}

amdf_status_t amdf_gpu_wddm_wkmi_adapter_deinitialize(
    amdf_gpu_wddm_wkmi_adapter_t* adapter) {
  if (adapter->native != NULL) {
    uint32_t native_status = 0;
    const amdf_wkmi_bridge_result_t result =
        adapter->loader.api->gpu_adapter_close(adapter->native, &native_status);
    const amdf_status_t status =
        amdf_gpu_wddm_wkmi_make_status(result, native_status);
    if (!amdf_status_is_ok(status)) {
      return status;
    }
    adapter->native = NULL;
  }
  if (adapter->loader.module == NULL) {
    return AMDF_STATUS_OK;
  }
  return amdf_gpu_wddm_wkmi_loader_deinitialize(&adapter->loader);
}

amdf_status_t amdf_gpu_wddm_wkmi_adapter_query_allocation_layout(
    const amdf_gpu_wddm_wkmi_adapter_t* adapter, uint64_t byte_length,
    uint32_t* out_allocation_count,
    uint64_t* out_maximum_allocation_byte_length) {
  const amdf_wkmi_bridge_result_t result =
      adapter->loader.api->gpu_allocation_query_layout(
          adapter->native, byte_length, out_allocation_count,
          out_maximum_allocation_byte_length);
  return amdf_gpu_wddm_wkmi_make_status(result, 0);
}

amdf_status_t amdf_gpu_wddm_wkmi_adapter_create_allocations(
    const amdf_gpu_wddm_wkmi_adapter_t* adapter,
    const amdf_wkmi_bridge_gpu_allocation_create_info_t* create_info,
    uint32_t allocation_handle_capacity, D3DKMT_HANDLE* out_allocation_handles,
    D3DKMT_HANDLE* out_resource, uint32_t* out_allocation_count) {
  uint32_t native_status = 0;
  const amdf_wkmi_bridge_result_t result =
      adapter->loader.api->gpu_allocation_create(
          adapter->native, create_info, allocation_handle_capacity,
          (uint32_t*)out_allocation_handles, (uint32_t*)out_resource,
          out_allocation_count, &native_status);
  return amdf_gpu_wddm_wkmi_make_status(result, native_status);
}

amdf_status_t amdf_gpu_wddm_wkmi_adapter_create_kernel_queue(
    const amdf_gpu_wddm_wkmi_adapter_t* adapter,
    const amdf_wkmi_bridge_gpu_kernel_queue_create_info_t* create_info,
    amdf_wkmi_bridge_gpu_kernel_queue_t** out_queue,
    amdf_wkmi_bridge_gpu_kernel_queue_info_t* out_info) {
  uint32_t native_status = 0;
  const amdf_wkmi_bridge_result_t result =
      adapter->loader.api->gpu_kernel_queue_create(
          adapter->native, create_info, out_queue, out_info, &native_status);
  return amdf_gpu_wddm_wkmi_make_status(result, native_status);
}

amdf_status_t amdf_gpu_wddm_wkmi_adapter_submit_kernel_queue(
    const amdf_gpu_wddm_wkmi_adapter_t* adapter,
    amdf_wkmi_bridge_gpu_kernel_queue_t* queue, uint64_t command_buffer_address,
    uint64_t command_buffer_byte_length, uint64_t progress_value) {
  uint32_t native_status = 0;
  const amdf_wkmi_bridge_result_t result =
      adapter->loader.api->gpu_kernel_queue_submit(
          queue, command_buffer_address, command_buffer_byte_length,
          progress_value, &native_status);
  return amdf_gpu_wddm_wkmi_make_status(result, native_status);
}

amdf_status_t amdf_gpu_wddm_wkmi_adapter_destroy_kernel_queue(
    const amdf_gpu_wddm_wkmi_adapter_t* adapter,
    amdf_wkmi_bridge_gpu_kernel_queue_t* queue) {
  uint32_t native_status = 0;
  const amdf_wkmi_bridge_result_t result =
      adapter->loader.api->gpu_kernel_queue_destroy(queue, &native_status);
  return amdf_gpu_wddm_wkmi_make_status(result, native_status);
}
