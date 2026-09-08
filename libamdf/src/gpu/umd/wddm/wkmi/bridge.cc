// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <cstdlib>
#include <new>

#include "libamdf/src/gpu/umd/wddm/wkmi/bridge_api.h"

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
#include <ntstatus.h>

#include "wkmi.h"

namespace {

class AdapterInfoCleanup {
 public:
  explicit AdapterInfoCleanup(Wkmi::DeviceInfo* device_info)
      : device_info_(device_info) {}

  AdapterInfoCleanup(const AdapterInfoCleanup&) = delete;
  AdapterInfoCleanup& operator=(const AdapterInfoCleanup&) = delete;

  ~AdapterInfoCleanup() { std::free(device_info_->adapter_info); }

 private:
  Wkmi::DeviceInfo* device_info_;
};

amdf_wkmi_bridge_result_t QueryGpuPropertiesImpl(
    uint32_t adapter_handle, amdf_wkmi_bridge_gpu_properties_t* out_properties,
    uint32_t* out_native_status) {
  Wkmi::DeviceInfo device_info = {};
  AdapterInfoCleanup adapter_info_cleanup(&device_info);
  const NTSTATUS native_status = Wkmi::ParseAdapterInfo(
      static_cast<D3DKMT_HANDLE>(adapter_handle), &device_info);
  if (native_status == STATUS_OBJECT_NAME_NOT_FOUND ||
      native_status == STATUS_REVISION_MISMATCH) {
    return AMDF_WKMI_BRIDGE_RESULT_UNSUPPORTED;
  }
  if (native_status != STATUS_SUCCESS) {
    *out_native_status = static_cast<uint32_t>(native_status);
    return AMDF_WKMI_BRIDGE_RESULT_NATIVE_FAILURE;
  }

  amdf_wkmi_bridge_gpu_properties_t properties = {};
  properties.gfx_ip_major = device_info.major;
  properties.gfx_ip_minor = device_info.minor;
  properties.gfx_ip_stepping = device_info.stepping;
  properties.asic_revision = device_info.asic_revision;
  properties.wavefront_size = device_info.wavefront_size;
  properties.compute_unit_count = device_info.compute_unit_count;
  properties.maximum_wave_count_per_compute_unit = device_info.wave_per_cu;
  properties.maximum_scratch_wave_count_per_compute_unit =
      device_info.max_scratch_slots_per_cu;
  properties.local_data_share_byte_length = device_info.lds_size;
  properties.xcc_count = device_info.num_xcc;
  properties.shader_engine_count = device_info.num_shader_engine;
  *out_properties = properties;
  return AMDF_WKMI_BRIDGE_RESULT_SUCCESS;
}

amdf_wkmi_bridge_result_t AMDF_WKMI_BRIDGE_CALL
QueryGpuProperties(uint32_t adapter_handle, uint32_t physical_adapter_index,
                   amdf_wkmi_bridge_gpu_properties_t* out_properties,
                   uint32_t* out_native_status) noexcept {
  if (out_properties == nullptr || out_native_status == nullptr) {
    return AMDF_WKMI_BRIDGE_RESULT_INVALID_ARGUMENT;
  }
  *out_native_status = 0;
  if (physical_adapter_index != 0) {
    return AMDF_WKMI_BRIDGE_RESULT_UNSUPPORTED;
  }

  try {
    return QueryGpuPropertiesImpl(adapter_handle, out_properties,
                                  out_native_status);
  } catch (const std::bad_alloc&) {
    return AMDF_WKMI_BRIDGE_RESULT_RESOURCE_EXHAUSTED;
  } catch (...) {
    return AMDF_WKMI_BRIDGE_RESULT_INTERNAL;
  }
}

const amdf_wkmi_bridge_api_t kBridgeApiV1 = {
    sizeof(amdf_wkmi_bridge_api_t),
    AMDF_WKMI_BRIDGE_ABI_VERSION_1,
    QueryGpuProperties,
};

}  // namespace

extern "C" __declspec(dllexport) amdf_wkmi_bridge_result_t AMDF_WKMI_BRIDGE_CALL
amdf_wkmi_bridge_query_api(uint32_t minimum_version, uint32_t maximum_version,
                           const amdf_wkmi_bridge_api_t** out_api) {
  if (out_api == nullptr) {
    return AMDF_WKMI_BRIDGE_RESULT_INVALID_ARGUMENT;
  }
  *out_api = nullptr;
  if (minimum_version > AMDF_WKMI_BRIDGE_ABI_VERSION_1 ||
      maximum_version < AMDF_WKMI_BRIDGE_ABI_VERSION_1) {
    return AMDF_WKMI_BRIDGE_RESULT_VERSION_MISMATCH;
  }
  *out_api = &kBridgeApiV1;
  return AMDF_WKMI_BRIDGE_RESULT_SUCCESS;
}
