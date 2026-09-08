// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef AMDF_SRC_PLATFORM_LINUX_ENDPOINT_H_
#define AMDF_SRC_PLATFORM_LINUX_ENDPOINT_H_

#include "libamdf/src/platform/endpoint.h"
#include "libamdf/src/platform/linux/instance.h"
#include "libamdf/src/platform/linux/release_list.h"

struct amdf_platform_endpoint_t {
  // Instance borrowed for fresh device-file identity validation.
  amdf_platform_instance_t* instance;
  // Query-only DRM file; materialized devices open independent files.
  int descriptor;
  // Immutable identity and PCI properties established on open.
  amdf_endpoint_info_t info;
  // DRM interface version reported by the opened file.
  struct {
    // Major ABI version.
    uint32_t major_version;
    // Minor ABI revision.
    uint32_t minor_version;
  } driver;
  // Unpublished devices retained after a failed native construction rollback.
  amdf_linux_release_list_t failed_constructions;
};

#ifdef __cplusplus
extern "C" {
#endif

// Opens a fresh native file after verifying the endpoint identity. This is
// deliberately not dup: DRM GEM handle tables and HWCTX ownership are per file.
amdf_status_t amdf_linux_endpoint_open_file(
    const amdf_platform_endpoint_t* endpoint, int* out_descriptor);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AMDF_SRC_PLATFORM_LINUX_ENDPOINT_H_
