// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Strix Halo XDNA executable-image target qualification.

#ifndef IREE_HAL_DRIVERS_AMD_XDNA_IMAGE_AIE2P_STRIX_HALO_H_
#define IREE_HAL_DRIVERS_AMD_XDNA_IMAGE_AIE2P_STRIX_HALO_H_

#include <stdint.h>

#include "iree/base/api.h"
#include "iree/hal/drivers/amd/xdna/image/aie2p/target.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Initializes the exact Strix Halo target contract for a logical context.
//
// |context_column_count| must be in the hardware-supported range [1, 8]. The
// returned descriptor contains no borrowed state and may be copied or shared
// between concurrent image construction calls.
iree_status_t iree_hal_amd_xdna_aie2p_strix_halo_target_initialize(
    uint16_t context_column_count,
    iree_hal_amd_xdna_aie2p_target_t* out_target);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // IREE_HAL_DRIVERS_AMD_XDNA_IMAGE_AIE2P_STRIX_HALO_H_
