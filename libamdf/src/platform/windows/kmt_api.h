// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef AMDF_SRC_PLATFORM_WINDOWS_KMT_API_H_
#define AMDF_SRC_PLATFORM_WINDOWS_KMT_API_H_

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif  // WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif  // NOMINMAX
#define WIN32_NO_STATUS
#include <windows.h>
#include <winternl.h>
#undef WIN32_NO_STATUS

#include <d3dkmthk.h>
#include <stdint.h>

#include "amdf/amdf.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Dynamically resolved subset of the public Windows KMT API.
typedef struct amdf_kmt_api_t {
  // System GDI module owning every procedure in this table.
  HMODULE module;
  // Enumerates display, compute-only, and virtual adapters.
  PFND3DKMT_ENUMADAPTERS3 enumerate_adapters;
  // Opens one adapter directly from its locally stable LUID.
  PFND3DKMT_OPENADAPTERFROMLUID open_adapter_from_luid;
  // Queries immutable adapter and physical-device properties.
  PFND3DKMT_QUERYADAPTERINFO query_adapter_info;
  // Creates one logical KMT device from an opened adapter.
  PFND3DKMT_CREATEDEVICE create_device;
  // Destroys one logical KMT device.
  PFND3DKMT_DESTROYDEVICE destroy_device;
  // Creates the paging queue used by a logical device.
  PFND3DKMT_CREATEPAGINGQUEUE create_paging_queue;
  // Destroys one paging queue and its associated synchronization object.
  PFND3DKMT_DESTROYPAGINGQUEUE destroy_paging_queue;
  // Creates one virtual execution context.
  PFND3DKMT_CREATECONTEXTVIRTUAL create_context_virtual;
  // Destroys one virtual execution context.
  PFND3DKMT_DESTROYCONTEXT destroy_context;
  // Closes an adapter handle.
  PFND3DKMT_CLOSEADAPTER close_adapter;
} amdf_kmt_api_t;

// Loads GDI and resolves required and optional KMT procedures once.
amdf_status_t amdf_kmt_api_initialize(amdf_kmt_api_t* out_api);

// Unloads GDI after every KMT child handle has been closed.
amdf_status_t amdf_kmt_api_deinitialize(amdf_kmt_api_t* api);

// Returns true when the complete device/paging/context procedure set exists.
bool amdf_kmt_api_supports_device_contexts(const amdf_kmt_api_t* api);

// Queries one typed block of adapter information.
amdf_status_t amdf_kmt_query_adapter_info(const amdf_kmt_api_t* api,
                                          D3DKMT_HANDLE adapter,
                                          KMTQUERYADAPTERINFOTYPE type,
                                          void* data, uint32_t data_size);

// Converts a native KMT status without discarding its domain.
amdf_status_t amdf_kmt_make_status(NTSTATUS status);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // AMDF_SRC_PLATFORM_WINDOWS_KMT_API_H_
