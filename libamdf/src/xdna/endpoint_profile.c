// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/xdna/endpoint_profile.h"

#include <stddef.h>

// The first qualified NPU5 path stages native transaction bytes in the
// 32 KiB instruction window between offsets 0x8000 and 0x10000. Its ERT
// command record carries exactly five dense 64-bit memory addresses.
enum {
  AMDF_XDNA_NPU5_MAXIMUM_NATIVE_BYTE_LENGTH = 32 * 1024,
  AMDF_XDNA_NPU5_MAXIMUM_BINDING_COUNT = 5,
};

// Static profiles contain only properties that are invariant for an exact PCI
// identity. AIE4 geometry is firmware-reported and is intentionally absent.
static const amdf_xdna_endpoint_info_t amdf_xdna_npu1_endpoint_info = {
    .type = AMDF_STRUCTURE_TYPE_XDNA_ENDPOINT_INFO,
    .structure_size = sizeof(amdf_xdna_endpoint_info_t),
    .architecture = AMDF_XDNA_ARCHITECTURE_AIE2,
    .array =
        {
            .column_origin = 1,
            .column_count = 4,
            .row_count = 6,
            .column_stride = UINT64_C(1) << 25,
        },
    .context =
        {
            .scheduling_modes = AMDF_XDNA_SCHEDULING_MODE_SPATIAL |
                                AMDF_XDNA_SCHEDULING_MODE_TIME_SLICED,
            .minimum_column_count = 1,
            .maximum_column_count = 4,
            .column_count_granularity = 1,
            .maximum_live_context_count = 6,
            .maximum_hardware_context_count = 6,
        },
    .target_id = "amd.xdna.phoenix.1502_00",
};

static const amdf_xdna_endpoint_info_t amdf_xdna_npu4_endpoint_info = {
    .type = AMDF_STRUCTURE_TYPE_XDNA_ENDPOINT_INFO,
    .structure_size = sizeof(amdf_xdna_endpoint_info_t),
    .architecture = AMDF_XDNA_ARCHITECTURE_AIE2P,
    .array =
        {
            .column_origin = 0,
            .column_count = 8,
            .row_count = 6,
            .column_stride = UINT64_C(1) << 25,
        },
    .context =
        {
            .scheduling_modes = AMDF_XDNA_SCHEDULING_MODE_TIME_SLICED,
            .minimum_column_count = 1,
            .maximum_column_count = 8,
            .column_count_granularity = 1,
            .maximum_live_context_count = 32,
            .maximum_hardware_context_count = 16,
        },
    .target_id = "amd.xdna.strix.17f0_10",
};

static const amdf_xdna_endpoint_info_t amdf_xdna_npu5_endpoint_info = {
    .type = AMDF_STRUCTURE_TYPE_XDNA_ENDPOINT_INFO,
    .structure_size = sizeof(amdf_xdna_endpoint_info_t),
    .architecture = AMDF_XDNA_ARCHITECTURE_AIE2P,
    .array =
        {
            .column_origin = 0,
            .column_count = 8,
            .row_count = 6,
            .column_stride = UINT64_C(1) << 25,
        },
    .context =
        {
            .scheduling_modes = AMDF_XDNA_SCHEDULING_MODE_TIME_SLICED,
            .minimum_column_count = 1,
            .maximum_column_count = 8,
            .column_count_granularity = 1,
            .maximum_live_context_count = 32,
            .maximum_hardware_context_count = 16,
        },
    .program =
        {
            .maximum_component_count = 1,
            .maximum_component_byte_length =
                AMDF_XDNA_NPU5_MAXIMUM_NATIVE_BYTE_LENGTH,
            .maximum_total_byte_length =
                AMDF_XDNA_NPU5_MAXIMUM_NATIVE_BYTE_LENGTH,
            .component_formats =
                {
                    .array_configuration =
                        {
                            .format = AMDF_XDNA_BINARY_FORMAT_TRANSACTION,
                            .version = AMDF_XDNA_TRANSACTION_FORMAT_VERSION_0_1,
                        },
                },
        },
    .command =
        {
            .maximum_binding_count = AMDF_XDNA_NPU5_MAXIMUM_BINDING_COUNT,
            .maximum_control_byte_length =
                AMDF_XDNA_NPU5_MAXIMUM_NATIVE_BYTE_LENGTH,
            .maximum_native_byte_length =
                AMDF_XDNA_NPU5_MAXIMUM_NATIVE_BYTE_LENGTH,
            .control_format =
                {
                    .format = AMDF_XDNA_BINARY_FORMAT_TRANSACTION,
                    .version = AMDF_XDNA_TRANSACTION_FORMAT_VERSION_0_1,
                },
        },
    .target_id = "amd.xdna.strix_halo.17f0_11",
};

static const amdf_xdna_endpoint_info_t amdf_xdna_npu6_endpoint_info = {
    .type = AMDF_STRUCTURE_TYPE_XDNA_ENDPOINT_INFO,
    .structure_size = sizeof(amdf_xdna_endpoint_info_t),
    .architecture = AMDF_XDNA_ARCHITECTURE_AIE2P,
    .array =
        {
            .column_origin = 0,
            .column_count = 8,
            .row_count = 6,
            .column_stride = UINT64_C(1) << 25,
        },
    .context =
        {
            .scheduling_modes = AMDF_XDNA_SCHEDULING_MODE_TIME_SLICED,
            .minimum_column_count = 1,
            .maximum_column_count = 8,
            .column_count_granularity = 1,
            .maximum_live_context_count = 32,
            .maximum_hardware_context_count = 16,
        },
    .target_id = "amd.xdna.krackan.17f0_20",
};

static const amdf_xdna_endpoint_profile_t amdf_xdna_endpoint_profiles[] = {
    {
        .model = AMDF_PCI_XDNA_MODEL_NPU1,
        .info = &amdf_xdna_npu1_endpoint_info,
    },
    {
        .model = AMDF_PCI_XDNA_MODEL_NPU4,
        .info = &amdf_xdna_npu4_endpoint_info,
    },
    {
        .model = AMDF_PCI_XDNA_MODEL_NPU5,
        .info = &amdf_xdna_npu5_endpoint_info,
        .transaction =
            {
                .device_generation = 4,
                .memory_tile_row_count = 1,
            },
    },
    {
        .model = AMDF_PCI_XDNA_MODEL_NPU6,
        .info = &amdf_xdna_npu6_endpoint_info,
    },
};

const amdf_xdna_endpoint_profile_t* amdf_xdna_endpoint_profile_select(
    const amdf_endpoint_info_t* endpoint_info) {
  if (endpoint_info->engine_kind != AMDF_ENGINE_KIND_XDNA) {
    return NULL;
  }
  const amdf_pci_xdna_model_t model =
      amdf_pci_classify_xdna_model(&endpoint_info->pci);
  for (size_t i = 0; i < sizeof(amdf_xdna_endpoint_profiles) /
                             sizeof(amdf_xdna_endpoint_profiles[0]);
       ++i) {
    const amdf_xdna_endpoint_profile_t* profile =
        &amdf_xdna_endpoint_profiles[i];
    if (model == profile->model) {
      return profile;
    }
  }
  return NULL;
}

const amdf_xdna_endpoint_info_t* amdf_xdna_endpoint_profile_get_info(
    const amdf_xdna_endpoint_profile_t* profile) {
  return profile->info;
}
