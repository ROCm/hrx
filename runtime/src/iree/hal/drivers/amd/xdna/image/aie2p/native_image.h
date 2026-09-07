// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// AIE2P qualified-image lowering to native transaction 0.1 streams.

#ifndef IREE_HAL_DRIVERS_AMD_XDNA_IMAGE_AIE2P_NATIVE_IMAGE_H_
#define IREE_HAL_DRIVERS_AMD_XDNA_IMAGE_AIE2P_NATIVE_IMAGE_H_

#include "iree/base/api.h"
#include "iree/hal/drivers/amd/xdna/image/aie2p/target.h"
#include "iree/hal/drivers/amd/xdna/image/image.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Native transaction streams selected by one executable entry.
//
// Byte spans borrow immutable storage from their native image and remain valid
// until that image is destroyed.
typedef struct iree_hal_amd_xdna_aie2p_native_entry_t {
  // Dense native ARRAY ordinal used by this entry.
  uint32_t array_ordinal;
  // Invocation CONTROL transaction with entry-relative address patches.
  iree_const_byte_span_t control;
} iree_hal_amd_xdna_aie2p_native_entry_t;

// Complete immutable AIE2P native lowering of one qualified XDNA image.
typedef struct iree_hal_amd_xdna_aie2p_native_image_t
    iree_hal_amd_xdna_aie2p_native_image_t;

// Lowers every ARRAY and executable entry in |image| to transaction 0.1.
//
// |image| must have been qualified against |target|. Both inputs are borrowed
// only for the duration of the call; all resulting native bytes are owned by
// the returned image. Distinct entries sharing an ARRAY realization share one
// native ARRAY transaction, while each entry receives an independently lowered
// CONTROL transaction with dense entry-relative buffer-argument indices.
iree_status_t iree_hal_amd_xdna_aie2p_native_image_create(
    const iree_hal_amd_xdna_image_t* image,
    const iree_hal_amd_xdna_aie2p_target_t* target,
    iree_allocator_t host_allocator,
    iree_hal_amd_xdna_aie2p_native_image_t** out_native_image);

// Destroys |native_image| and all native transaction storage.
void iree_hal_amd_xdna_aie2p_native_image_destroy(
    iree_hal_amd_xdna_aie2p_native_image_t* native_image);

// Returns the number of distinct native ARRAY transactions.
iree_host_size_t iree_hal_amd_xdna_aie2p_native_image_array_count(
    const iree_hal_amd_xdna_aie2p_native_image_t* native_image);

// Copies the native ARRAY transaction at |ordinal| into |out_transaction|.
//
// The returned span borrows immutable storage from |native_image|.
iree_status_t iree_hal_amd_xdna_aie2p_native_image_query_array_configuration(
    const iree_hal_amd_xdna_aie2p_native_image_t* native_image,
    iree_host_size_t ordinal, iree_const_byte_span_t* out_transaction);

// Returns the number of executable entries in the native image.
iree_host_size_t iree_hal_amd_xdna_aie2p_native_image_entry_count(
    const iree_hal_amd_xdna_aie2p_native_image_t* native_image);

// Copies the native entry at |ordinal| into |out_entry|.
iree_status_t iree_hal_amd_xdna_aie2p_native_image_query_entry(
    const iree_hal_amd_xdna_aie2p_native_image_t* native_image,
    iree_host_size_t ordinal,
    iree_hal_amd_xdna_aie2p_native_entry_t* out_entry);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // IREE_HAL_DRIVERS_AMD_XDNA_IMAGE_AIE2P_NATIVE_IMAGE_H_
