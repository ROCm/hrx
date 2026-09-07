// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/xdna/umd/mcdm/legacy_context.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif  // WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif  // NOMINMAX
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "libamdf/src/xdna/umd/mcdm/npu5_legacy_bootstrap_image.h"

// Fixed header of the installed NPU5 legacy context-private ABI.
typedef struct amdf_windows_xdna_legacy_context_header_t {
  // UUID copied from the provider-owned compatibility image.
  uint8_t xclbin_uuid[16];
  // Unresolved legacy state required before context policy.
  uint8_t reserved_0010[0x18];
  // Context quality-of-service priority; zero selects the default.
  uint32_t quality_of_service_priority;
  // Unresolved legacy state preceding the returned command cookie.
  uint8_t reserved_002c[0x14];
  // Command-aperture cookie written by the driver during context creation.
  uint32_t command_aperture_cookie;
  // Unresolved legacy state adjoining the command-aperture cookie.
  uint32_t reserved_0044;
  // Base of the driver's context command aperture.
  uint64_t command_aperture_base;
  // Unresolved NPU5 legacy value fixed at 0x48.
  uint64_t opaque_0050;
  // Number of bytes following offset 0x80 in the complete record.
  uint64_t bytes_after_0080;
  // Identifier of the process creating the context.
  uint64_t process_id;
  // Unresolved legacy state preceding offset 0x80.
  uint8_t reserved_0068[0x18];
  // Unresolved NPU5 legacy value fixed at one.
  uint64_t opaque_0080;
  // Unresolved legacy state preceding context command storage metadata.
  uint8_t reserved_0088[0x40];
  // Size in bytes of the context command storage expected by the driver.
  uint64_t context_command_storage_size;
  // Size in bytes of the embedded compatibility image.
  uint64_t xclbin_size;
  // Number of bytes following virtual record offset 0x138.
  uint64_t bytes_after_0138;
  // Combined embedded-image and trailing-record size.
  uint64_t embedded_payload_size;
} amdf_windows_xdna_legacy_context_header_t;

// Fixed trailing record following the variably sized compatibility image.
typedef struct amdf_windows_xdna_legacy_context_tail_t {
  // Legacy kernel name and driver-required terminal discriminator.
  char kernel_name[0x40];
  // Unresolved NPU5 legacy value fixed at 0x10000.
  uint64_t opaque_0040;
  // Number of columns in the complete physical NPU5 array.
  uint64_t physical_array_column_count;
  // Unresolved legacy state preceding the kernel identifier.
  uint8_t reserved_0050[8];
  // Legacy kernel identifier used only by the compatibility envelope.
  uint64_t kernel_id;
  // Unresolved program-independent legacy context state.
  uint8_t reserved_0060[0x300];
  // Offset of context instruction storage within the command aperture.
  uint32_t context_instruction_offset;
  // Number of compute rows in each NPU5 column.
  uint32_t compute_row_count;
  // Logical column count admitted to the materialized context.
  uint32_t logical_column_count;
  // Physical origin requested for the complete array partition.
  uint32_t physical_column_origin;
  // Unresolved NPU5 legacy value fixed at two.
  uint32_t opaque_0370;
  // Unresolved NPU5 legacy value fixed at three.
  uint32_t opaque_0374;
  // Unresolved NPU5 legacy value fixed at four.
  uint32_t opaque_0378;
} amdf_windows_xdna_legacy_context_tail_t;

#define AMDF_WINDOWS_XDNA_LEGACY_CONTEXT_TAIL_SIZE                  \
  (offsetof(amdf_windows_xdna_legacy_context_tail_t, opaque_0378) + \
   sizeof(uint32_t))

_Static_assert(sizeof(amdf_windows_xdna_legacy_context_header_t) == 0xE8,
               "legacy context header layout must match the NPU5 ABI");
_Static_assert(offsetof(amdf_windows_xdna_legacy_context_header_t,
                        command_aperture_cookie) == 0x40,
               "legacy command cookie offset must match the NPU5 ABI");
_Static_assert(AMDF_WINDOWS_XDNA_LEGACY_CONTEXT_TAIL_SIZE == 0x37C,
               "legacy context tail layout must match the NPU5 ABI");

amdf_status_t amdf_windows_xdna_legacy_context_build(
    uint32_t logical_column_count, uint32_t physical_column_origin,
    uint8_t** out_data, uint32_t* out_data_size) {
  if (out_data == NULL || out_data_size == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  *out_data = NULL;
  *out_data_size = 0;
  const size_t xclbin_uuid_offset = 0x1A0;
  if (amdf_windows_xdna_npu5_legacy_bootstrap_image_size <
          xclbin_uuid_offset + 16 ||
      memcmp(amdf_windows_xdna_npu5_legacy_bootstrap_image, "xclbin2\0", 8) !=
          0) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INTERNAL);
  }

  const size_t total_size = sizeof(amdf_windows_xdna_legacy_context_header_t) +
                            amdf_windows_xdna_npu5_legacy_bootstrap_image_size +
                            AMDF_WINDOWS_XDNA_LEGACY_CONTEXT_TAIL_SIZE;
  if (total_size > UINT32_MAX) {
    return amdf_make_api_status(AMDF_STATUS_CODE_RESOURCE_EXHAUSTED);
  }
  uint8_t* data = (uint8_t*)calloc(1, total_size);
  if (data == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_RESOURCE_EXHAUSTED);
  }

  amdf_windows_xdna_legacy_context_header_t* header =
      (amdf_windows_xdna_legacy_context_header_t*)data;
  memcpy(header->xclbin_uuid,
         amdf_windows_xdna_npu5_legacy_bootstrap_image + xclbin_uuid_offset,
         sizeof(header->xclbin_uuid));
  header->command_aperture_base = UINT64_C(0x04000000);
  header->opaque_0050 = UINT64_C(0x48);
  header->bytes_after_0080 = total_size - 0x80;
  header->process_id = GetCurrentProcessId();
  header->opaque_0080 = 1;
  header->context_command_storage_size = UINT64_C(0x1000);
  header->xclbin_size = amdf_windows_xdna_npu5_legacy_bootstrap_image_size;
  header->bytes_after_0138 = total_size - 0x138;
  header->embedded_payload_size = total_size - sizeof(*header);

  uint8_t* image_data = data + sizeof(*header);
  memcpy(image_data, amdf_windows_xdna_npu5_legacy_bootstrap_image,
         amdf_windows_xdna_npu5_legacy_bootstrap_image_size);

  amdf_windows_xdna_legacy_context_tail_t tail = {0};
  static const char kernel_name[] = "MLIR_AIE";
  memcpy(tail.kernel_name, kernel_name, sizeof(kernel_name));
  tail.kernel_name[0x3F] = '0';
  tail.opaque_0040 = UINT64_C(0x10000);
  tail.physical_array_column_count = 8;
  tail.kernel_id = UINT64_C(0x901);
  tail.context_instruction_offset = 0x800;
  tail.compute_row_count = 4;
  tail.logical_column_count = logical_column_count;
  tail.physical_column_origin = physical_column_origin;
  tail.opaque_0370 = 2;
  tail.opaque_0374 = 3;
  tail.opaque_0378 = 4;
  memcpy(image_data + amdf_windows_xdna_npu5_legacy_bootstrap_image_size, &tail,
         AMDF_WINDOWS_XDNA_LEGACY_CONTEXT_TAIL_SIZE);

  *out_data = data;
  *out_data_size = (uint32_t)total_size;
  return AMDF_STATUS_OK;
}

amdf_status_t amdf_windows_xdna_legacy_context_query_command_aperture_cookie(
    const uint8_t* data, uint32_t data_size, uint32_t* out_cookie) {
  if (data == NULL ||
      data_size < sizeof(amdf_windows_xdna_legacy_context_header_t) ||
      out_cookie == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  const amdf_windows_xdna_legacy_context_header_t* header =
      (const amdf_windows_xdna_legacy_context_header_t*)data;
  *out_cookie = header->command_aperture_cookie;
  return AMDF_STATUS_OK;
}
