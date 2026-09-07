// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef AMDF_SRC_XDNA_ENDPOINT_PROFILE_H_
#define AMDF_SRC_XDNA_ENDPOINT_PROFILE_H_

#include "amdf/xdna.h"
#include "libamdf/src/pci.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Exact immutable implementation profile selected from one PCI identity.
typedef struct amdf_xdna_endpoint_profile_t {
  // Private model identity selecting platform-specific implementation code.
  amdf_pci_xdna_model_t model;
  // Borrowed public compiler target and context-admission properties.
  const amdf_xdna_endpoint_info_t* info;
  // Target-native transaction properties fixed for this endpoint identity.
  struct {
    // AIE-RT device-generation value encoded in transaction headers.
    uint8_t device_generation;
    // Memory-tile row count encoded in transaction headers.
    uint8_t memory_tile_row_count;
  } transaction;
} amdf_xdna_endpoint_profile_t;

// Selects the exact immutable XDNA profile matching `endpoint_info`.
const amdf_xdna_endpoint_profile_t* amdf_xdna_endpoint_profile_select(
    const amdf_endpoint_info_t* endpoint_info);

// Returns the borrowed public information stored in `profile`.
const amdf_xdna_endpoint_info_t* amdf_xdna_endpoint_profile_get_info(
    const amdf_xdna_endpoint_profile_t* profile);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // AMDF_SRC_XDNA_ENDPOINT_PROFILE_H_
