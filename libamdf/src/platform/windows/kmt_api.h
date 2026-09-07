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
  // Closes an adapter handle.
  PFND3DKMT_CLOSEADAPTER close_adapter;
} amdf_kmt_api_t;

// Loads GDI and resolves the complete KMT procedure table.
amdf_status_t amdf_kmt_api_initialize(amdf_kmt_api_t* out_api);

// Unloads GDI after every KMT child handle has been closed.
amdf_status_t amdf_kmt_api_deinitialize(amdf_kmt_api_t* api);

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
