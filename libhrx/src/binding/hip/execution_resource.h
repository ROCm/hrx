// Copyright 2026 The HRX Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef LIBHRX_SRC_BINDING_HIP_EXECUTION_RESOURCE_H_
#define LIBHRX_SRC_BINDING_HIP_EXECUTION_RESOURCE_H_

#include "binding/hip/api.h"
#include "common/execution_resource.h"

typedef struct iree_hal_streaming_device_t iree_hal_streaming_device_t;

#ifdef __cplusplus
extern "C" {
#endif

// Creates a copyable HIP SM resource backed by an immutable device-owned set.
//
// |resources| uses family-local HAL execution-resource ordinals and is
// interned in |device|. The public HIP counts are derived from the raw
// execution units covered by the selected resources. |out_resource| is
// unchanged on failure.
iree_status_t iree_hip_execution_resource_create_sm(
    iree_hal_streaming_device_t* device,
    const iree_hal_queue_family_t* queue_family,
    iree_hal_queue_execution_resource_list_t resources, unsigned int flags,
    hipDevResource* out_resource);

// Resolves and validates |resource| against an explicit device incarnation.
// Returns the immutable borrowed set on success. |out_set| is unchanged on
// failure.
hipError_t iree_hip_execution_resource_resolve_sm_for_device(
    const hipDevResource* resource, iree_hal_streaming_device_t* device,
    const iree_hal_streaming_execution_resource_set_t** out_set);

// Resolves and validates |resource| against the live device registry. The
// returned device and set are borrowed and remain valid while HIP is
// initialized. Outputs are unchanged on failure.
hipError_t iree_hip_execution_resource_resolve_sm(
    const hipDevResource* resource, iree_hal_streaming_device_t** out_device,
    const iree_hal_streaming_execution_resource_set_t** out_set);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // LIBHRX_SRC_BINDING_HIP_EXECUTION_RESOURCE_H_
