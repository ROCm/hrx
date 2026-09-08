// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#define _GNU_SOURCE
#include "libamdf/src/xdna/umd/drm/buffer.h"

#include <drm/amdxdna_accel.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "libamdf/src/platform/linux/file.h"

amdf_status_t amdf_linux_xdna_buffer_initialize(
    int descriptor, uint32_t type, size_t byte_length, size_t alignment,
    size_t page_size, const amdf_linux_xdna_buffer_t* heap,
    amdf_linux_xdna_buffer_t* buffer) {
  buffer->byte_length = byte_length;
  struct amdxdna_drm_create_bo create = {.type = type, .size = byte_length};
  if (ioctl(descriptor, DRM_IOCTL_AMDXDNA_CREATE_BO, &create) != 0) {
    return amdf_linux_error(errno);
  }
  buffer->handle = create.handle;
  struct amdxdna_drm_get_bo_info info = {.handle = create.handle};
  if (ioctl(descriptor, DRM_IOCTL_AMDXDNA_GET_BO_INFO, &info) != 0) {
    return amdf_linux_error(errno);
  }
  if (type == AMDXDNA_BO_DEV) {
    // The kernel reports a subrange of the one live heap mapping. Validate
    // native output before turning it into a host pointer used for copying.
    if (info.xdna_addr < heap->device_address ||
        byte_length > heap->byte_length ||
        info.xdna_addr - heap->device_address >
            heap->byte_length - byte_length ||
        info.vaddr != (uintptr_t)heap->host_pointer + info.xdna_addr -
                          heap->device_address) {
      return amdf_make_api_status(AMDF_STATUS_CODE_INTERNAL);
    }
    buffer->host_pointer = (void*)(uintptr_t)info.vaddr;
  } else {
    void* address = NULL;
    int flags = MAP_SHARED;
    if (alignment > page_size) {
      const size_t reserved_length = byte_length + alignment;
      void* reservation = mmap(NULL, reserved_length, PROT_NONE,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      if (reservation == MAP_FAILED) return amdf_linux_error(errno);
      buffer->mapping.base = reservation;
      buffer->mapping.byte_length = reserved_length;
      address = (void*)(((uintptr_t)reservation + alignment - 1) &
                        ~(uintptr_t)(alignment - 1));
      // Replace only pages within our own live PROT_NONE reservation.
      flags |= MAP_FIXED;
    }
    void* mapping = mmap(address, byte_length, PROT_READ | PROT_WRITE, flags,
                         descriptor, (off_t)info.map_offset);
    if (mapping == MAP_FAILED) return amdf_linux_error(errno);
    buffer->host_pointer = mapping;
    if (buffer->mapping.base == NULL) {
      buffer->mapping.base = mapping;
      buffer->mapping.byte_length = byte_length;
    }
    // mmap establishes SVA; the pre-mmap query does not yet have its address.
    if (ioctl(descriptor, DRM_IOCTL_AMDXDNA_GET_BO_INFO, &info) != 0) {
      return amdf_linux_error(errno);
    }
    if (info.vaddr != (uintptr_t)mapping ||
        (type != AMDXDNA_BO_DEV_HEAP && info.xdna_addr != (uintptr_t)mapping)) {
      return amdf_make_api_status(AMDF_STATUS_CODE_INTERNAL);
    }
  }
  buffer->device_address = info.xdna_addr;
  return AMDF_STATUS_OK;
}

amdf_status_t amdf_linux_xdna_buffer_deinitialize(
    int descriptor, amdf_linux_xdna_buffer_t* buffer) {
  if (buffer->mapping.base != NULL) {
    if (munmap(buffer->mapping.base, buffer->mapping.byte_length) != 0) {
      return amdf_linux_error(errno);
    }
    buffer->mapping.base = NULL;
    buffer->mapping.byte_length = 0;
    buffer->host_pointer = NULL;
  }
  if (buffer->handle != 0) {
    struct drm_gem_close close_buffer = {.handle = buffer->handle};
    if (ioctl(descriptor, DRM_IOCTL_GEM_CLOSE, &close_buffer) != 0) {
      return amdf_linux_error(errno);
    }
    buffer->handle = 0;
  }
  buffer->host_pointer = NULL;
  buffer->device_address = 0;
  return AMDF_STATUS_OK;
}
