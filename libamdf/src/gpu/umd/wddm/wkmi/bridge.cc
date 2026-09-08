// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <vector>

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

struct amdf_wkmi_bridge_gpu_adapter_t {
  Wkmi::DeviceInfo device_info = {};

  ~amdf_wkmi_bridge_gpu_adapter_t() { std::free(device_info.adapter_info); }
};

namespace {

constexpr uint64_t kGpuPageSize = 4ull * 1024;
constexpr uint64_t kMaximumNativeAllocationByteLength =
    2ull * 1024 * 1024 * 1024;

bool IsUnsupportedAdapterStatus(NTSTATUS status) {
  return status == STATUS_OBJECT_NAME_NOT_FOUND ||
         status == STATUS_REVISION_MISMATCH || status == STATUS_NOT_SUPPORTED;
}

void PopulateGpuProperties(const Wkmi::DeviceInfo& device_info,
                           amdf_wkmi_bridge_gpu_properties_t* out_properties) {
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
}

amdf_wkmi_bridge_result_t GpuAdapterOpenImpl(
    uint32_t adapter_handle, amdf_wkmi_bridge_gpu_adapter_t** out_adapter,
    amdf_wkmi_bridge_gpu_properties_t* out_properties,
    uint32_t* out_native_status) {
  std::unique_ptr<amdf_wkmi_bridge_gpu_adapter_t> adapter(
      new (std::nothrow) amdf_wkmi_bridge_gpu_adapter_t());
  if (adapter == nullptr) {
    return AMDF_WKMI_BRIDGE_RESULT_RESOURCE_EXHAUSTED;
  }
  const NTSTATUS native_status = Wkmi::ParseAdapterInfo(
      static_cast<D3DKMT_HANDLE>(adapter_handle), &adapter->device_info);
  if (IsUnsupportedAdapterStatus(native_status)) {
    return AMDF_WKMI_BRIDGE_RESULT_UNSUPPORTED;
  }
  if (native_status != STATUS_SUCCESS) {
    *out_native_status = static_cast<uint32_t>(native_status);
    return AMDF_WKMI_BRIDGE_RESULT_NATIVE_FAILURE;
  }
  PopulateGpuProperties(adapter->device_info, out_properties);
  *out_adapter = adapter.release();
  return AMDF_WKMI_BRIDGE_RESULT_SUCCESS;
}

amdf_wkmi_bridge_result_t AMDF_WKMI_BRIDGE_CALL
GpuAdapterOpen(uint32_t adapter_handle, uint32_t physical_adapter_index,
               amdf_wkmi_bridge_gpu_adapter_t** out_adapter,
               amdf_wkmi_bridge_gpu_properties_t* out_properties,
               uint32_t* out_native_status) noexcept {
  if (adapter_handle == 0 || out_adapter == nullptr ||
      out_properties == nullptr || out_native_status == nullptr) {
    return AMDF_WKMI_BRIDGE_RESULT_INVALID_ARGUMENT;
  }
  *out_adapter = nullptr;
  *out_properties = {};
  *out_native_status = 0;
  if (physical_adapter_index != 0) {
    return AMDF_WKMI_BRIDGE_RESULT_UNSUPPORTED;
  }

  try {
    return GpuAdapterOpenImpl(adapter_handle, out_adapter, out_properties,
                              out_native_status);
  } catch (const std::bad_alloc&) {
    return AMDF_WKMI_BRIDGE_RESULT_RESOURCE_EXHAUSTED;
  } catch (...) {
    return AMDF_WKMI_BRIDGE_RESULT_INTERNAL;
  }
}

void AMDF_WKMI_BRIDGE_CALL
GpuAdapterClose(amdf_wkmi_bridge_gpu_adapter_t* adapter) noexcept {
  delete adapter;
}

amdf_wkmi_bridge_result_t QueryAllocationCount(uint64_t byte_length,
                                               uint32_t* out_allocation_count) {
  if (byte_length == 0 || (byte_length & (kGpuPageSize - 1)) != 0) {
    return AMDF_WKMI_BRIDGE_RESULT_INVALID_ARGUMENT;
  }
  const uint64_t allocation_count =
      (byte_length - 1) / kMaximumNativeAllocationByteLength + 1;
  if (allocation_count > std::numeric_limits<uint32_t>::max()) {
    return AMDF_WKMI_BRIDGE_RESULT_OUT_OF_RANGE;
  }
  *out_allocation_count = static_cast<uint32_t>(allocation_count);
  return AMDF_WKMI_BRIDGE_RESULT_SUCCESS;
}

amdf_wkmi_bridge_result_t AMDF_WKMI_BRIDGE_CALL GpuAllocationQueryLayout(
    amdf_wkmi_bridge_gpu_adapter_t* adapter, uint64_t byte_length,
    uint32_t* out_allocation_count,
    uint64_t* out_maximum_allocation_byte_length) noexcept {
  if (adapter == nullptr || out_allocation_count == nullptr ||
      out_maximum_allocation_byte_length == nullptr) {
    return AMDF_WKMI_BRIDGE_RESULT_INVALID_ARGUMENT;
  }
  *out_allocation_count = 0;
  *out_maximum_allocation_byte_length = 0;
  const amdf_wkmi_bridge_result_t result =
      QueryAllocationCount(byte_length, out_allocation_count);
  if (result == AMDF_WKMI_BRIDGE_RESULT_SUCCESS) {
    *out_maximum_allocation_byte_length = kMaximumNativeAllocationByteLength;
  }
  return result;
}

Wkmi::AllocDomain ToWkmiAllocationDomain(
    amdf_wkmi_bridge_gpu_allocation_domain_t domain) {
  switch (domain) {
    case AMDF_WKMI_BRIDGE_GPU_ALLOCATION_DOMAIN_LOCAL:
      return Wkmi::kLocal;
    case AMDF_WKMI_BRIDGE_GPU_ALLOCATION_DOMAIN_REGISTERED_HOST:
      return Wkmi::kUserMemory;
    default:
      return Wkmi::kSystem;
  }
}

uint32_t ToWkmiAllocationFlags(amdf_wkmi_bridge_gpu_allocation_flags_t flags) {
  uint32_t wkmi_flags = 0;
  if ((flags & AMDF_WKMI_BRIDGE_GPU_ALLOCATION_FLAG_FINE_GRAIN) != 0) {
    wkmi_flags |= Wkmi::kFineGrain;
  }
  if ((flags & AMDF_WKMI_BRIDGE_GPU_ALLOCATION_FLAG_QUEUE_STORAGE) != 0) {
    wkmi_flags |= Wkmi::kQueueObject;
  }
  return wkmi_flags;
}

NTSTATUS DestroyAllocation(uint32_t device_handle, uint32_t resource_handle,
                           const std::vector<D3DKMT_HANDLE>& handles) {
  if (resource_handle == 0 && handles.empty()) {
    return STATUS_SUCCESS;
  }
  D3DKMT_DESTROYALLOCATION2 destroy = {};
  destroy.hDevice = static_cast<D3DKMT_HANDLE>(device_handle);
  destroy.hResource = static_cast<D3DKMT_HANDLE>(resource_handle);
  if (resource_handle == 0) {
    destroy.phAllocationList = handles.data();
    destroy.AllocationCount = static_cast<uint32_t>(handles.size());
  }
  destroy.Flags.AssumeNotInUse = 1;
  return D3DKMTDestroyAllocation2(&destroy);
}

amdf_wkmi_bridge_result_t GpuAllocationCreateImpl(
    amdf_wkmi_bridge_gpu_adapter_t* adapter,
    const amdf_wkmi_bridge_gpu_allocation_create_info_t& create_info,
    uint32_t allocation_handle_capacity, uint32_t* out_allocation_handles,
    uint32_t* out_resource_handle, uint32_t* out_allocation_count,
    uint32_t* out_native_status) {
  uint32_t allocation_count = 0;
  amdf_wkmi_bridge_result_t result =
      QueryAllocationCount(create_info.byte_length, &allocation_count);
  if (result != AMDF_WKMI_BRIDGE_RESULT_SUCCESS) {
    return result;
  }
  if (allocation_handle_capacity < allocation_count) {
    *out_allocation_count = allocation_count;
    return AMDF_WKMI_BRIDGE_RESULT_BUFFER_TOO_SMALL;
  }

  int driver_private_size = 0;
  int allocation_private_size = 0;
  Wkmi::GetAllocPrivDataSize(&driver_private_size, &allocation_private_size);
  if (driver_private_size <= 0 || allocation_private_size <= 0) {
    return AMDF_WKMI_BRIDGE_RESULT_VERSION_MISMATCH;
  }
  if (static_cast<uint64_t>(allocation_private_size) >
      std::numeric_limits<size_t>::max() / allocation_count) {
    return AMDF_WKMI_BRIDGE_RESULT_OUT_OF_RANGE;
  }

  std::vector<uint8_t> driver_private(static_cast<size_t>(driver_private_size));
  std::vector<uint8_t> allocation_private(
      static_cast<size_t>(allocation_private_size) * allocation_count);
  std::vector<D3DDDI_ALLOCATIONINFO2> allocation_infos(allocation_count);
  std::vector<D3DKMT_HANDLE> allocation_handles(allocation_count);
  Wkmi::FillinAllocPrivDrvData(driver_private.data(), allocation_private_size);

  uint64_t remaining_byte_length = create_info.byte_length;
  uint64_t byte_offset = 0;
  for (uint32_t i = 0; i < allocation_count; ++i) {
    const uint64_t chunk_byte_length =
        std::min(remaining_byte_length, kMaximumNativeAllocationByteLength);
    void* private_data = allocation_private.data() +
                         static_cast<size_t>(allocation_private_size) * i;
    const uint64_t placement_device_address =
        create_info.domain == AMDF_WKMI_BRIDGE_GPU_ALLOCATION_DOMAIN_LOCAL
            ? create_info.placement_device_address + byte_offset
            : 0;
    Wkmi::SetAllocationInfo(private_data, chunk_byte_length,
                            ToWkmiAllocationDomain(create_info.domain),
                            placement_device_address,
                            ToWkmiAllocationFlags(create_info.flags),
                            Wkmi::KCOMPUTE0, adapter->device_info);

    D3DDDI_ALLOCATIONINFO2& allocation_info = allocation_infos[i];
    if (create_info.host_pointer != nullptr) {
      allocation_info.pSystemMem =
          static_cast<uint8_t*>(create_info.host_pointer) + byte_offset;
    }
    allocation_info.pPrivateDriverData = private_data;
    allocation_info.PrivateDriverDataSize = allocation_private_size;
    allocation_info.VidPnSourceId = D3DDDI_ID_UNINITIALIZED;
    remaining_byte_length -= chunk_byte_length;
    byte_offset += chunk_byte_length;
  }

  D3DKMT_CREATEALLOCATION create = {};
  create.hDevice = static_cast<D3DKMT_HANDLE>(create_info.device_handle);
  create.pPrivateDriverData = driver_private.data();
  create.PrivateDriverDataSize = driver_private_size;
  create.NumAllocations = allocation_count;
  create.pAllocationInfo2 = allocation_infos.data();
  const NTSTATUS native_status = D3DKMTCreateAllocation2(&create);
  if (native_status != STATUS_SUCCESS) {
    *out_native_status = static_cast<uint32_t>(native_status);
    return AMDF_WKMI_BRIDGE_RESULT_NATIVE_FAILURE;
  }

  bool handles_valid = true;
  size_t valid_handle_count = 0;
  for (uint32_t i = 0; i < allocation_count; ++i) {
    const D3DKMT_HANDLE handle = allocation_infos[i].hAllocation;
    if (handle == 0) {
      handles_valid = false;
    } else {
      allocation_handles[valid_handle_count++] = handle;
    }
  }
  if (!handles_valid) {
    allocation_handles.resize(valid_handle_count);
    const NTSTATUS destroy_status = DestroyAllocation(
        create_info.device_handle, create.hResource, allocation_handles);
    *out_native_status = static_cast<uint32_t>(destroy_status == STATUS_SUCCESS
                                                   ? STATUS_INVALID_HANDLE
                                                   : destroy_status);
    return AMDF_WKMI_BRIDGE_RESULT_NATIVE_FAILURE;
  }

  for (uint32_t i = 0; i < allocation_count; ++i) {
    out_allocation_handles[i] = allocation_infos[i].hAllocation;
  }
  *out_resource_handle = create.hResource;
  *out_allocation_count = allocation_count;
  return AMDF_WKMI_BRIDGE_RESULT_SUCCESS;
}

amdf_wkmi_bridge_result_t AMDF_WKMI_BRIDGE_CALL GpuAllocationCreate(
    amdf_wkmi_bridge_gpu_adapter_t* adapter,
    const amdf_wkmi_bridge_gpu_allocation_create_info_t* create_info,
    uint32_t allocation_handle_capacity, uint32_t* out_allocation_handles,
    uint32_t* out_resource_handle, uint32_t* out_allocation_count,
    uint32_t* out_native_status) noexcept {
  if (adapter == nullptr || create_info == nullptr ||
      create_info->structure_size < sizeof(*create_info) ||
      create_info->device_handle == 0 || out_allocation_handles == nullptr ||
      out_resource_handle == nullptr || out_allocation_count == nullptr ||
      out_native_status == nullptr) {
    return AMDF_WKMI_BRIDGE_RESULT_INVALID_ARGUMENT;
  }
  *out_resource_handle = 0;
  *out_allocation_count = 0;
  *out_native_status = 0;
  const amdf_wkmi_bridge_gpu_allocation_flags_t known_flags =
      AMDF_WKMI_BRIDGE_GPU_ALLOCATION_FLAG_FINE_GRAIN |
      AMDF_WKMI_BRIDGE_GPU_ALLOCATION_FLAG_QUEUE_STORAGE;
  const bool is_local =
      create_info->domain == AMDF_WKMI_BRIDGE_GPU_ALLOCATION_DOMAIN_LOCAL;
  const bool has_host_pointer = create_info->host_pointer != nullptr;
  const uintptr_t host_address =
      reinterpret_cast<uintptr_t>(create_info->host_pointer);
  if (create_info->domain < AMDF_WKMI_BRIDGE_GPU_ALLOCATION_DOMAIN_SYSTEM ||
      create_info->domain >
          AMDF_WKMI_BRIDGE_GPU_ALLOCATION_DOMAIN_REGISTERED_HOST ||
      (create_info->flags & ~known_flags) != 0 ||
      (is_local != (create_info->placement_device_address != 0)) ||
      (is_local && has_host_pointer) || (!is_local && !has_host_pointer) ||
      (!is_local && create_info->byte_length >
                        std::numeric_limits<uintptr_t>::max() - host_address) ||
      (is_local &&
       create_info->byte_length > std::numeric_limits<uint64_t>::max() -
                                      create_info->placement_device_address)) {
    return AMDF_WKMI_BRIDGE_RESULT_INVALID_ARGUMENT;
  }

  try {
    return GpuAllocationCreateImpl(adapter, *create_info,
                                   allocation_handle_capacity,
                                   out_allocation_handles, out_resource_handle,
                                   out_allocation_count, out_native_status);
  } catch (const std::bad_alloc&) {
    return AMDF_WKMI_BRIDGE_RESULT_RESOURCE_EXHAUSTED;
  } catch (...) {
    return AMDF_WKMI_BRIDGE_RESULT_INTERNAL;
  }
}

const amdf_wkmi_bridge_api_t kBridgeApiV1 = {
    sizeof(amdf_wkmi_bridge_api_t),
    AMDF_WKMI_BRIDGE_ABI_VERSION_1,
    GpuAdapterOpen,
    GpuAdapterClose,
    GpuAllocationQueryLayout,
    GpuAllocationCreate,
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
