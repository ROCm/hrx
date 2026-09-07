// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/pci.h"

#include <stddef.h>

#define AMDF_PCI_VENDOR_ID_ATI 0x1002u
#define AMDF_PCI_VENDOR_ID_AMD 0x1022u

typedef struct amdf_pci_device_identity_t {
  // PCI device identifier.
  uint32_t device_id;
  // PCI revision identifier.
  uint32_t revision_id;
  // Exact XDNA model published for the identity.
  amdf_pci_xdna_model_t model;
} amdf_pci_device_identity_t;

// Exact XDNA identities published by AMD's host drivers. Classification only
// identifies the engine family; executable compatibility remains a separate
// endpoint-profile decision.
static const amdf_pci_device_identity_t amdf_pci_xdna_identities[] = {
    // NPU1.
    {0x1502u, 0x00u, AMDF_PCI_XDNA_MODEL_NPU1},
    // NPU4, NPU5, and NPU6.
    {0x17F0u, 0x10u, AMDF_PCI_XDNA_MODEL_NPU4},
    {0x17F0u, 0x11u, AMDF_PCI_XDNA_MODEL_NPU5},
    {0x17F0u, 0x20u, AMDF_PCI_XDNA_MODEL_NPU6},
    // NPU3 and NPU7 through NPU11 classic, physical, and virtual functions.
    {0x17F1u, 0x10u, AMDF_PCI_XDNA_MODEL_NPU3},
    {0x17F2u, 0x10u, AMDF_PCI_XDNA_MODEL_NPU3},
    {0x17F3u, 0x10u, AMDF_PCI_XDNA_MODEL_NPU3},
    {0x17F1u, 0x11u, AMDF_PCI_XDNA_MODEL_NPU7},
    {0x17F2u, 0x11u, AMDF_PCI_XDNA_MODEL_NPU7},
    {0x17F3u, 0x11u, AMDF_PCI_XDNA_MODEL_NPU7},
    {0x17F1u, 0x12u, AMDF_PCI_XDNA_MODEL_NPU8},
    {0x17F2u, 0x12u, AMDF_PCI_XDNA_MODEL_NPU8},
    {0x17F3u, 0x12u, AMDF_PCI_XDNA_MODEL_NPU8},
    {0x17F1u, 0x13u, AMDF_PCI_XDNA_MODEL_NPU9},
    {0x17F2u, 0x13u, AMDF_PCI_XDNA_MODEL_NPU9},
    {0x17F3u, 0x13u, AMDF_PCI_XDNA_MODEL_NPU9},
    {0x17F1u, 0x14u, AMDF_PCI_XDNA_MODEL_NPU10},
    {0x17F2u, 0x14u, AMDF_PCI_XDNA_MODEL_NPU10},
    {0x17F3u, 0x14u, AMDF_PCI_XDNA_MODEL_NPU10},
    {0x17F1u, 0x15u, AMDF_PCI_XDNA_MODEL_NPU11},
    {0x17F2u, 0x15u, AMDF_PCI_XDNA_MODEL_NPU11},
    {0x17F3u, 0x15u, AMDF_PCI_XDNA_MODEL_NPU11},
    {0x1B0Au, 0x00u, AMDF_PCI_XDNA_MODEL_NPU3},
    {0x1B0Bu, 0x00u, AMDF_PCI_XDNA_MODEL_NPU3},
    {0x1B0Cu, 0x00u, AMDF_PCI_XDNA_MODEL_NPU3},
};

amdf_pci_xdna_model_t amdf_pci_classify_xdna_model(const amdf_pci_info_t* pci) {
  if (pci->vendor_id != AMDF_PCI_VENDOR_ID_AMD) {
    return AMDF_PCI_XDNA_MODEL_UNKNOWN;
  }
  for (size_t i = 0; i < sizeof(amdf_pci_xdna_identities) /
                             sizeof(amdf_pci_xdna_identities[0]);
       ++i) {
    const amdf_pci_device_identity_t* identity = &amdf_pci_xdna_identities[i];
    if (pci->device_id == identity->device_id &&
        pci->revision_id == identity->revision_id) {
      return identity->model;
    }
  }
  return AMDF_PCI_XDNA_MODEL_UNKNOWN;
}

bool amdf_pci_is_amd(const amdf_pci_info_t* pci) {
  return pci->vendor_id == AMDF_PCI_VENDOR_ID_ATI ||
         pci->vendor_id == AMDF_PCI_VENDOR_ID_AMD;
}

amdf_engine_kind_t amdf_pci_classify_engine(const amdf_pci_info_t* pci) {
  if (pci->vendor_id == AMDF_PCI_VENDOR_ID_ATI) {
    return AMDF_ENGINE_KIND_GPU;
  }
  if (amdf_pci_classify_xdna_model(pci) != AMDF_PCI_XDNA_MODEL_UNKNOWN) {
    return AMDF_ENGINE_KIND_XDNA;
  }
  return AMDF_ENGINE_KIND_UNKNOWN;
}
