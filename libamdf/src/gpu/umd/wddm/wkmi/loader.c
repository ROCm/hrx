// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/gpu/umd/wddm/wkmi/loader.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define AMDF_WKMI_BRIDGE_PATH_ENVIRONMENT_VARIABLE L"AMDF_WKMI_BRIDGE_PATH"
#define AMDF_WKMI_BRIDGE_FILE_NAME L"amdf_wkmi_bridge.dll"
#define AMDF_WINDOWS_MAXIMUM_PATH_CAPACITY 32768u

static const char amdf_gpu_wddm_wkmi_module_anchor = 0;

static amdf_status_t amdf_gpu_wddm_wkmi_make_absolute_path(
    wchar_t** inout_path) {
  const DWORD required_capacity = GetFullPathNameW(*inout_path, 0, NULL, NULL);
  if (required_capacity == 0) {
    return amdf_make_status(AMDF_STATUS_DOMAIN_WIN32, GetLastError());
  }
  if (required_capacity > AMDF_WINDOWS_MAXIMUM_PATH_CAPACITY) {
    return amdf_make_status(AMDF_STATUS_DOMAIN_WIN32,
                            ERROR_FILENAME_EXCED_RANGE);
  }

  wchar_t* absolute_path =
      (wchar_t*)malloc((size_t)required_capacity * sizeof(*absolute_path));
  if (absolute_path == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_RESOURCE_EXHAUSTED);
  }
  const DWORD path_length =
      GetFullPathNameW(*inout_path, required_capacity, absolute_path, NULL);
  if (path_length == 0 || path_length >= required_capacity) {
    const DWORD error = GetLastError();
    free(absolute_path);
    return amdf_make_status(AMDF_STATUS_DOMAIN_WIN32, error == ERROR_SUCCESS
                                                          ? ERROR_INVALID_DATA
                                                          : error);
  }

  free(*inout_path);
  *inout_path = absolute_path;
  return AMDF_STATUS_OK;
}

static amdf_status_t amdf_gpu_wddm_wkmi_allocate_environment_path(
    wchar_t** out_path) {
  *out_path = NULL;
  SetLastError(ERROR_SUCCESS);
  const DWORD required_capacity = GetEnvironmentVariableW(
      AMDF_WKMI_BRIDGE_PATH_ENVIRONMENT_VARIABLE, NULL, 0);
  if (required_capacity == 0) {
    const DWORD error = GetLastError();
    return error == ERROR_ENVVAR_NOT_FOUND
               ? AMDF_STATUS_OK
               : amdf_make_status(
                     AMDF_STATUS_DOMAIN_WIN32,
                     error == ERROR_SUCCESS ? ERROR_INVALID_DATA : error);
  }

  wchar_t* path = (wchar_t*)malloc((size_t)required_capacity * sizeof(*path));
  if (path == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_RESOURCE_EXHAUSTED);
  }
  const DWORD path_length = GetEnvironmentVariableW(
      AMDF_WKMI_BRIDGE_PATH_ENVIRONMENT_VARIABLE, path, required_capacity);
  if (path_length == 0 || path_length >= required_capacity) {
    const DWORD error = GetLastError();
    free(path);
    return amdf_make_status(AMDF_STATUS_DOMAIN_WIN32, error == ERROR_SUCCESS
                                                          ? ERROR_INVALID_DATA
                                                          : error);
  }
  *out_path = path;
  return amdf_gpu_wddm_wkmi_make_absolute_path(out_path);
}

static amdf_status_t amdf_gpu_wddm_wkmi_allocate_sibling_path(
    wchar_t** out_path) {
  *out_path = NULL;
  HMODULE owner_module = NULL;
  if (!GetModuleHandleExW(
          GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
          (LPCWSTR)(const void*)&amdf_gpu_wddm_wkmi_module_anchor,
          &owner_module)) {
    return amdf_make_status(AMDF_STATUS_DOMAIN_WIN32, GetLastError());
  }

  DWORD capacity = MAX_PATH;
  wchar_t* path = NULL;
  DWORD path_length = 0;
  bool path_complete = false;
  while (capacity <= AMDF_WINDOWS_MAXIMUM_PATH_CAPACITY) {
    wchar_t* expanded_path =
        (wchar_t*)realloc(path, (size_t)capacity * sizeof(*path));
    if (expanded_path == NULL) {
      free(path);
      return amdf_make_api_status(AMDF_STATUS_CODE_RESOURCE_EXHAUSTED);
    }
    path = expanded_path;
    path_length = GetModuleFileNameW(owner_module, path, capacity);
    if (path_length == 0) {
      const DWORD error = GetLastError();
      free(path);
      return amdf_make_status(AMDF_STATUS_DOMAIN_WIN32, error);
    }
    if (path_length < capacity) {
      path_complete = true;
      break;
    }
    if (capacity == AMDF_WINDOWS_MAXIMUM_PATH_CAPACITY) {
      break;
    }
    capacity = capacity > AMDF_WINDOWS_MAXIMUM_PATH_CAPACITY / 2
                   ? AMDF_WINDOWS_MAXIMUM_PATH_CAPACITY
                   : capacity * 2;
  }
  if (!path_complete) {
    free(path);
    return amdf_make_status(AMDF_STATUS_DOMAIN_WIN32,
                            ERROR_FILENAME_EXCED_RANGE);
  }

  wchar_t* file_name = path + path_length;
  while (file_name != path && file_name[-1] != L'\\' && file_name[-1] != L'/') {
    --file_name;
  }
  const size_t directory_length = (size_t)(file_name - path);
  const size_t file_name_capacity =
      sizeof(AMDF_WKMI_BRIDGE_FILE_NAME) / sizeof(wchar_t);
  const size_t sibling_capacity = directory_length + file_name_capacity;
  wchar_t* sibling_path =
      (wchar_t*)realloc(path, sibling_capacity * sizeof(*path));
  if (sibling_path == NULL) {
    free(path);
    return amdf_make_api_status(AMDF_STATUS_CODE_RESOURCE_EXHAUSTED);
  }
  memcpy(sibling_path + directory_length, AMDF_WKMI_BRIDGE_FILE_NAME,
         file_name_capacity * sizeof(wchar_t));
  *out_path = sibling_path;
  return AMDF_STATUS_OK;
}

static amdf_status_t amdf_gpu_wddm_wkmi_allocate_bridge_path(
    wchar_t** out_path) {
  amdf_status_t status = amdf_gpu_wddm_wkmi_allocate_environment_path(out_path);
  if (amdf_status_is_ok(status) && *out_path == NULL) {
    status = amdf_gpu_wddm_wkmi_allocate_sibling_path(out_path);
  }
  return status;
}

amdf_status_t amdf_gpu_wddm_wkmi_loader_initialize(
    amdf_gpu_wddm_wkmi_loader_t* out_loader) {
  *out_loader = (amdf_gpu_wddm_wkmi_loader_t){0};
  wchar_t* bridge_path = NULL;
  amdf_status_t status = amdf_gpu_wddm_wkmi_allocate_bridge_path(&bridge_path);
  HMODULE module = NULL;
  if (amdf_status_is_ok(status)) {
    module = LoadLibraryExW(
        bridge_path, NULL,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (module == NULL) {
      status = amdf_make_status(AMDF_STATUS_DOMAIN_WIN32, GetLastError());
    }
  }
  free(bridge_path);

  amdf_wkmi_bridge_query_api_fn_t query_api = NULL;
  if (amdf_status_is_ok(status)) {
    query_api = (amdf_wkmi_bridge_query_api_fn_t)GetProcAddress(
        module, "amdf_wkmi_bridge_query_api");
    if (query_api == NULL) {
      status = amdf_make_status(AMDF_STATUS_DOMAIN_WIN32, GetLastError());
    }
  }

  const amdf_wkmi_bridge_api_t* api = NULL;
  if (amdf_status_is_ok(status)) {
    const amdf_wkmi_bridge_result_t result =
        query_api(AMDF_WKMI_BRIDGE_ABI_VERSION_1,
                  AMDF_WKMI_BRIDGE_ABI_VERSION_LATEST, &api);
    if (result == AMDF_WKMI_BRIDGE_RESULT_VERSION_MISMATCH) {
      status = amdf_make_api_status(AMDF_STATUS_CODE_VERSION_MISMATCH);
    } else if (result != AMDF_WKMI_BRIDGE_RESULT_SUCCESS || api == NULL ||
               api->structure_size < sizeof(amdf_wkmi_bridge_api_t) ||
               api->abi_version != AMDF_WKMI_BRIDGE_ABI_VERSION_1 ||
               api->gpu_adapter_open == NULL ||
               api->gpu_adapter_close == NULL ||
               api->gpu_allocation_query_layout == NULL ||
               api->gpu_allocation_create == NULL) {
      status = amdf_make_api_status(AMDF_STATUS_CODE_INTERNAL);
    }
  }

  if (amdf_status_is_ok(status)) {
    out_loader->module = module;
    out_loader->api = api;
  } else if (module != NULL) {
    if (!FreeLibrary(module)) {
      status = amdf_make_status(AMDF_STATUS_DOMAIN_WIN32, GetLastError());
    }
  }
  return status;
}

amdf_status_t amdf_gpu_wddm_wkmi_loader_deinitialize(
    amdf_gpu_wddm_wkmi_loader_t* loader) {
  if (!FreeLibrary(loader->module)) {
    return amdf_make_status(AMDF_STATUS_DOMAIN_WIN32, GetLastError());
  }
  *loader = (amdf_gpu_wddm_wkmi_loader_t){0};
  return AMDF_STATUS_OK;
}
