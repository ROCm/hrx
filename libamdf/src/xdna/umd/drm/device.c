// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/xdna/umd/device.h"

#include <drm/amdxdna_accel.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "libamdf/src/platform/linux/endpoint.h"
#include "libamdf/src/platform/linux/file.h"
#include "libamdf/src/platform/linux/host_cache.h"
#include "libamdf/src/xdna/target/npu5/bootstrap.h"
#include "libamdf/src/xdna/target/npu5/context.h"
#include "libamdf/src/xdna/umd/drm/device.h"

amdf_status_t amdf_xdna_umd_device_destroy(amdf_xdna_umd_device_t* device) {
  // Unpublished children never have accepted work, and ordinary public children
  // must already be gone. Keep every heap allocation alive through HWCTX
  // teardown.
  if (device->context != AMDXDNA_INVALID_CTX_HANDLE) {
    struct amdxdna_drm_destroy_hwctx destroy = {.handle = device->context};
    if (ioctl(device->descriptor, DRM_IOCTL_AMDXDNA_DESTROY_HWCTX, &destroy) !=
        0) {
      return amdf_linux_error(errno);
    }
    device->context = AMDXDNA_INVALID_CTX_HANDLE;
  }
  if (device->completion_syncobj != 0) {
    struct drm_syncobj_destroy destroy = {.handle = device->completion_syncobj};
    if (ioctl(device->descriptor, DRM_IOCTL_SYNCOBJ_DESTROY, &destroy) != 0) {
      return amdf_linux_error(errno);
    }
    device->completion_syncobj = 0;
  }
  amdf_status_t status =
      amdf_linux_release_list_drain(&device->failed_children);
  if (amdf_status_is_ok(status)) {
    status = amdf_linux_xdna_buffer_deinitialize(device->descriptor,
                                                 &device->bootstrap);
  }
  if (amdf_status_is_ok(status)) {
    status =
        amdf_linux_xdna_buffer_deinitialize(device->descriptor, &device->heap);
  }
  if (amdf_status_is_ok(status)) {
    status = amdf_linux_file_close(&device->descriptor);
  }
  if (amdf_status_is_ok(status)) free(device);
  return status;
}

static amdf_status_t amdf_linux_xdna_device_release_failed(
    amdf_linux_release_t* release) {
  return amdf_xdna_umd_device_destroy((amdf_xdna_umd_device_t*)release);
}

static amdf_status_t amdf_linux_xdna_device_qualify(
    amdf_xdna_umd_device_t* device,
    const amdf_xdna_endpoint_profile_t* profile) {
  struct amdxdna_drm_query_aie_metadata metadata = {0};
  struct amdxdna_drm_get_info query = {
      .param = DRM_AMDXDNA_QUERY_AIE_METADATA,
      .buffer_size = sizeof(metadata),
      .buffer = (uintptr_t)&metadata,
  };
  if (ioctl(device->descriptor, DRM_IOCTL_AMDXDNA_GET_INFO, &query) != 0) {
    return amdf_linux_error(errno);
  }
  if (metadata.cols != profile->info->array.column_count ||
      metadata.rows != profile->info->array.row_count ||
      metadata.core.row_count != AMDF_XDNA_NPU5_CORE_ROW_COUNT ||
      metadata.core.row_start != AMDF_XDNA_NPU5_CORE_ROW_ORIGIN ||
      metadata.mem.row_count != profile->transaction.memory_tile_row_count ||
      metadata.mem.row_start != 1 || metadata.shim.row_count != 1 ||
      metadata.shim.row_start != 0) {
    return amdf_make_api_status(AMDF_STATUS_CODE_UNSUPPORTED);
  }
  const long page_size = sysconf(_SC_PAGESIZE);
  if (page_size <= 0 || (page_size & (page_size - 1)) != 0 ||
      (size_t)page_size > AMDF_XDNA_NPU5_HEAP_BYTE_LENGTH) {
    return amdf_make_api_status(AMDF_STATUS_CODE_UNSUPPORTED);
  }
  device->page_size = (size_t)page_size;
  return amdf_linux_host_cache_query_line_size(&device->cache_line_size);
}

amdf_status_t amdf_xdna_umd_device_create(
    amdf_platform_endpoint_t* endpoint,
    const amdf_xdna_endpoint_profile_t* profile,
    const amdf_xdna_device_create_info_t* create_info,
    amdf_xdna_umd_device_t** out_device,
    amdf_xdna_umd_device_result_t* out_result) {
  *out_device = NULL;
  if (profile->model != AMDF_PCI_XDNA_MODEL_NPU5 ||
      endpoint->driver.major_version != 0 ||
      endpoint->driver.minor_version < 8 ||
      (create_info->acceptable_scheduling_modes &
       AMDF_XDNA_SCHEDULING_MODE_TIME_SLICED) == 0 ||
      (create_info->physical_column_origin !=
           AMDF_XDNA_PHYSICAL_COLUMN_ORIGIN_ANY &&
       create_info->physical_column_origin !=
           profile->info->array.column_origin)) {
    return amdf_make_api_status(AMDF_STATUS_CODE_UNSUPPORTED);
  }
  amdf_xdna_umd_device_t* device = calloc(1, sizeof(*device));
  if (device == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_RESOURCE_EXHAUSTED);
  }
  device->failed_construction.destroy = amdf_linux_xdna_device_release_failed;
  device->descriptor = -1;
  device->context = AMDXDNA_INVALID_CTX_HANDLE;
  amdf_status_t status =
      amdf_linux_endpoint_open_file(endpoint, &device->descriptor);
  if (amdf_status_is_ok(status)) {
    status = amdf_linux_xdna_device_qualify(device, profile);
  }
  if (amdf_status_is_ok(status)) {
    status = amdf_linux_xdna_buffer_initialize(
        device->descriptor, AMDXDNA_BO_DEV_HEAP,
        AMDF_XDNA_NPU5_HEAP_BYTE_LENGTH, AMDF_XDNA_NPU5_HEAP_BYTE_LENGTH,
        device->page_size, NULL, &device->heap);
  }
  const void* pdi = NULL;
  size_t pdi_byte_length = 0;
  amdf_xdna_npu5_bootstrap_query_pdi(&pdi, &pdi_byte_length);
  if (amdf_status_is_ok(status)) {
    const size_t length =
        (pdi_byte_length + device->page_size - 1) & ~(device->page_size - 1);
    status = amdf_linux_xdna_buffer_initialize(
        device->descriptor, AMDXDNA_BO_DEV, length, device->page_size,
        device->page_size, &device->heap, &device->bootstrap);
  }
  if (amdf_status_is_ok(status)) {
    memcpy(device->bootstrap.host_pointer, pdi, pdi_byte_length);
    amdf_linux_host_cache_transfer(device->bootstrap.host_pointer,
                                   pdi_byte_length, device->cache_line_size);
    struct amdxdna_qos_info qos = {.priority = AMDXDNA_QOS_NORMAL_PRIORITY};
    // Full physical width has exactly one legal placement, at origin zero.
    // The public logical width remains the caller's program admission limit.
    struct amdxdna_drm_create_hwctx create = {
        .qos_p = (uintptr_t)&qos,
        .num_tiles =
            profile->info->array.column_count * AMDF_XDNA_NPU5_CORE_ROW_COUNT,
    };
    if (ioctl(device->descriptor, DRM_IOCTL_AMDXDNA_CREATE_HWCTX, &create) !=
        0) {
      status = amdf_linux_error(errno);
    } else {
      device->context = create.handle;
      device->completion_syncobj = create.syncobj_handle;
    }
  }
  if (amdf_status_is_ok(status)) {
    amdf_xdna_umd_device_result_t result = {0};
    result.id.words[0] = (uintptr_t)device;
    result.id.words[1] = endpoint->info.id.words[1];
    result.reset_epoch = 1;
    result.scheduling_mode = AMDF_XDNA_SCHEDULING_MODE_TIME_SLICED;
    result.placement_generation = 1;
    result.physical_column_origin = profile->info->array.column_origin;
    result.physical_column_count = profile->info->array.column_count;
    *out_result = result;
    *out_device = device;
  } else {
    const amdf_status_t release_status = amdf_xdna_umd_device_destroy(device);
    if (!amdf_status_is_ok(release_status)) {
      amdf_linux_release_list_push(&endpoint->failed_constructions,
                                   &device->failed_construction);
      status = release_status;
    }
  }
  return status;
}
