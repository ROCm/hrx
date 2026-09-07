// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/pci.h"

#include <iomanip>

#include "gtest/gtest.h"

namespace {

TEST(PciTest, RecognizesAmdEndpointVendors) {
  amdf_pci_info_t pci = {};
  pci.vendor_id = 0x1002u;
  EXPECT_TRUE(amdf_pci_is_amd(&pci));
  pci.vendor_id = 0x1022u;
  EXPECT_TRUE(amdf_pci_is_amd(&pci));
  pci.vendor_id = 0x8086u;
  EXPECT_FALSE(amdf_pci_is_amd(&pci));
}

TEST(PciTest, ClassifiesGpuVendor) {
  amdf_pci_info_t pci = {};
  pci.vendor_id = 0x1002u;

  EXPECT_EQ(amdf_pci_classify_xdna_model(&pci), AMDF_PCI_XDNA_MODEL_UNKNOWN);
  EXPECT_EQ(amdf_pci_classify_engine(&pci), AMDF_ENGINE_KIND_GPU);
}

TEST(PciTest, ClassifiesPublishedXdnaIdentities) {
  struct Identity {
    // PCI device identifier.
    uint32_t device_id;
    // PCI revision identifier.
    uint32_t revision_id;
    // Expected exact XDNA model.
    amdf_pci_xdna_model_t model;
  };
  constexpr Identity kIdentities[] = {
      {0x1502u, 0x00u, AMDF_PCI_XDNA_MODEL_NPU1},
      {0x17F0u, 0x10u, AMDF_PCI_XDNA_MODEL_NPU4},
      {0x17F0u, 0x11u, AMDF_PCI_XDNA_MODEL_NPU5},
      {0x17F0u, 0x20u, AMDF_PCI_XDNA_MODEL_NPU6},
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

  for (const Identity& identity : kIdentities) {
    SCOPED_TRACE(testing::Message()
                 << "device=" << std::hex << identity.device_id
                 << " revision=" << identity.revision_id);
    amdf_pci_info_t pci = {};
    pci.vendor_id = 0x1022u;
    pci.device_id = identity.device_id;
    pci.revision_id = identity.revision_id;
    EXPECT_EQ(amdf_pci_classify_xdna_model(&pci), identity.model);
    EXPECT_EQ(amdf_pci_classify_engine(&pci), AMDF_ENGINE_KIND_XDNA);
  }
}

TEST(PciTest, LeavesUnpublishedXdnaIdentitiesUnknown) {
  struct Identity {
    // PCI device identifier.
    uint32_t device_id;
    // PCI revision identifier.
    uint32_t revision_id;
  };
  constexpr Identity kIdentities[] = {
      {0x1502u, 0x01u}, {0x17F0u, 0x12u}, {0x17F0u, 0x21u}, {0x17F1u, 0x0Fu},
      {0x17F1u, 0x16u}, {0x1B0Au, 0x01u}, {0x1B0Du, 0x00u},
  };

  for (const Identity& identity : kIdentities) {
    SCOPED_TRACE(testing::Message()
                 << "device=" << std::hex << identity.device_id
                 << " revision=" << identity.revision_id);
    amdf_pci_info_t pci = {};
    pci.vendor_id = 0x1022u;
    pci.device_id = identity.device_id;
    pci.revision_id = identity.revision_id;
    EXPECT_EQ(amdf_pci_classify_xdna_model(&pci), AMDF_PCI_XDNA_MODEL_UNKNOWN);
    EXPECT_EQ(amdf_pci_classify_engine(&pci), AMDF_ENGINE_KIND_UNKNOWN);
  }

  amdf_pci_info_t wrong_vendor = {};
  wrong_vendor.vendor_id = 0x8086u;
  wrong_vendor.device_id = 0x17F0u;
  wrong_vendor.revision_id = 0x11u;
  EXPECT_EQ(amdf_pci_classify_xdna_model(&wrong_vendor),
            AMDF_PCI_XDNA_MODEL_UNKNOWN);
  EXPECT_EQ(amdf_pci_classify_engine(&wrong_vendor), AMDF_ENGINE_KIND_UNKNOWN);
}

}  // namespace
