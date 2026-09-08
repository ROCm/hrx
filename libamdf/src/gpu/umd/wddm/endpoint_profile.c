// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/gpu/umd/endpoint_profile.h"

#include <stddef.h>

#include "libamdf/src/gpu/umd/wddm/wkmi/adapter.h"
#include "libamdf/src/gpu/umd/wddm/wkmi/endpoint_properties.h"
#include "libamdf/src/platform/windows/endpoint.h"

amdf_status_t amdf_gpu_umd_query_endpoint_profile(
    const amdf_platform_endpoint_t* platform_endpoint,
    amdf_gpu_endpoint_profile_t* out_profile, bool* out_available) {
  *out_available = false;
  amdf_gpu_wddm_wkmi_adapter_t adapter = {0};
  amdf_wkmi_bridge_gpu_properties_t provider_properties = {0};
  bool provider_properties_available = false;
  amdf_status_t status = amdf_gpu_wddm_wkmi_adapter_initialize(
      platform_endpoint->adapter, platform_endpoint->physical_adapter_index,
      &adapter, &provider_properties, &provider_properties_available);

  if (amdf_status_is_ok(status) && provider_properties_available) {
    amdf_gpu_endpoint_properties_t properties = {0};
    if (amdf_gpu_wddm_wkmi_endpoint_properties_translate(&provider_properties,
                                                         &properties) &&
        amdf_gpu_endpoint_profile_initialize(&properties, out_profile)) {
      *out_available = true;
    } else {
      status = amdf_make_api_status(AMDF_STATUS_CODE_INTERNAL);
    }
  }

  if (adapter.loader.module != NULL) {
    const amdf_status_t deinitialize_status =
        amdf_gpu_wddm_wkmi_adapter_deinitialize(&adapter);
    if (!amdf_status_is_ok(deinitialize_status)) {
      *out_available = false;
      status = deinitialize_status;
    }
  }
  return status;
}
