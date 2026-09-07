// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef AMDF_SRC_PCI_H_
#define AMDF_SRC_PCI_H_

#include <stdbool.h>

#include "amdf/amdf.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Provider-private XDNA hardware model selected by an exact PCI identity.
typedef uint32_t amdf_pci_xdna_model_t;
enum amdf_pci_xdna_model_e {
  AMDF_PCI_XDNA_MODEL_UNKNOWN = 0,
  AMDF_PCI_XDNA_MODEL_NPU1 = 1,
  AMDF_PCI_XDNA_MODEL_NPU3 = 2,
  AMDF_PCI_XDNA_MODEL_NPU4 = 3,
  AMDF_PCI_XDNA_MODEL_NPU5 = 4,
  AMDF_PCI_XDNA_MODEL_NPU6 = 5,
  AMDF_PCI_XDNA_MODEL_NPU7 = 6,
  AMDF_PCI_XDNA_MODEL_NPU8 = 7,
  AMDF_PCI_XDNA_MODEL_NPU9 = 8,
  AMDF_PCI_XDNA_MODEL_NPU10 = 9,
  AMDF_PCI_XDNA_MODEL_NPU11 = 10,
};

// Returns true when `pci` identifies an AMD-owned endpoint vendor.
bool amdf_pci_is_amd(const amdf_pci_info_t* pci);

// Returns the exact provider-private XDNA model, or UNKNOWN when not XDNA.
amdf_pci_xdna_model_t amdf_pci_classify_xdna_model(const amdf_pci_info_t* pci);

// Classifies a PCI identity into its broad execution-engine kind.
amdf_engine_kind_t amdf_pci_classify_engine(const amdf_pci_info_t* pci);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // AMDF_SRC_PCI_H_
