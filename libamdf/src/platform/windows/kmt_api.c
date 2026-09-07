// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/platform/windows/kmt_api.h"

#include <stddef.h>
#include <string.h>

amdf_status_t amdf_kmt_make_status(NTSTATUS status) {
  return amdf_make_status(AMDF_STATUS_DOMAIN_NTSTATUS, (uint32_t)status);
}

static amdf_status_t amdf_kmt_resolve_procedures(amdf_kmt_api_t* api) {
  api->enumerate_adapters = (PFND3DKMT_ENUMADAPTERS3)GetProcAddress(
      api->module, "D3DKMTEnumAdapters3");
  api->open_adapter_from_luid = (PFND3DKMT_OPENADAPTERFROMLUID)GetProcAddress(
      api->module, "D3DKMTOpenAdapterFromLuid");
  api->query_adapter_info = (PFND3DKMT_QUERYADAPTERINFO)GetProcAddress(
      api->module, "D3DKMTQueryAdapterInfo");
  api->create_device =
      (PFND3DKMT_CREATEDEVICE)GetProcAddress(api->module, "D3DKMTCreateDevice");
  api->destroy_device = (PFND3DKMT_DESTROYDEVICE)GetProcAddress(
      api->module, "D3DKMTDestroyDevice");
  api->create_paging_queue = (PFND3DKMT_CREATEPAGINGQUEUE)GetProcAddress(
      api->module, "D3DKMTCreatePagingQueue");
  api->destroy_paging_queue = (PFND3DKMT_DESTROYPAGINGQUEUE)GetProcAddress(
      api->module, "D3DKMTDestroyPagingQueue");
  api->create_context_virtual = (PFND3DKMT_CREATECONTEXTVIRTUAL)GetProcAddress(
      api->module, "D3DKMTCreateContextVirtual");
  api->destroy_context = (PFND3DKMT_DESTROYCONTEXT)GetProcAddress(
      api->module, "D3DKMTDestroyContext");
  api->close_adapter =
      (PFND3DKMT_CLOSEADAPTER)GetProcAddress(api->module, "D3DKMTCloseAdapter");
  if (api->enumerate_adapters == NULL || api->open_adapter_from_luid == NULL ||
      api->query_adapter_info == NULL || api->close_adapter == NULL) {
    return amdf_make_status(AMDF_STATUS_DOMAIN_WIN32, ERROR_PROC_NOT_FOUND);
  }
  return AMDF_STATUS_OK;
}

bool amdf_kmt_api_supports_device_contexts(const amdf_kmt_api_t* api) {
  return api->create_device != NULL && api->destroy_device != NULL &&
         api->create_paging_queue != NULL &&
         api->destroy_paging_queue != NULL &&
         api->create_context_virtual != NULL && api->destroy_context != NULL;
}

amdf_status_t amdf_kmt_api_initialize(amdf_kmt_api_t* out_api) {
  memset(out_api, 0, sizeof(*out_api));
  out_api->module =
      LoadLibraryExW(L"gdi32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (out_api->module == NULL) {
    return amdf_make_status(AMDF_STATUS_DOMAIN_WIN32, GetLastError());
  }
  const amdf_status_t status = amdf_kmt_resolve_procedures(out_api);
  if (!amdf_status_is_ok(status)) {
    if (!FreeLibrary(out_api->module)) {
      return amdf_make_status(AMDF_STATUS_DOMAIN_WIN32, GetLastError());
    }
    memset(out_api, 0, sizeof(*out_api));
  }
  return status;
}

amdf_status_t amdf_kmt_api_deinitialize(amdf_kmt_api_t* api) {
  if (!FreeLibrary(api->module)) {
    return amdf_make_status(AMDF_STATUS_DOMAIN_WIN32, GetLastError());
  }
  memset(api, 0, sizeof(*api));
  return AMDF_STATUS_OK;
}

amdf_status_t amdf_kmt_query_adapter_info(const amdf_kmt_api_t* api,
                                          D3DKMT_HANDLE adapter,
                                          KMTQUERYADAPTERINFOTYPE type,
                                          void* data, uint32_t data_size) {
  D3DKMT_QUERYADAPTERINFO query = {0};
  query.hAdapter = adapter;
  query.Type = type;
  query.pPrivateDriverData = data;
  query.PrivateDriverDataSize = data_size;
  return amdf_kmt_make_status(api->query_adapter_info(&query));
}
