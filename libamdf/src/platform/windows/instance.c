// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/platform/windows/instance.h"

#include <stddef.h>
#include <stdlib.h>

#include "libamdf/src/platform/windows/endpoint_snapshot.h"

amdf_status_t amdf_platform_instance_create(
    amdf_platform_instance_t** out_instance) {
  *out_instance = NULL;
  amdf_platform_instance_t* instance =
      (amdf_platform_instance_t*)calloc(1, sizeof(*instance));
  if (instance == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_RESOURCE_EXHAUSTED);
  }
  const amdf_status_t status = amdf_kmt_api_initialize(&instance->kmt);
  if (amdf_status_is_ok(status)) {
    *out_instance = instance;
  } else {
    free(instance);
  }
  return status;
}

amdf_status_t amdf_platform_instance_destroy(
    amdf_platform_instance_t* instance) {
  const amdf_status_t status = amdf_kmt_api_deinitialize(&instance->kmt);
  if (amdf_status_is_ok(status)) {
    free(instance);
  }
  return status;
}

amdf_status_t amdf_platform_endpoint_enumerate(
    amdf_platform_instance_t* instance, uint32_t capacity,
    amdf_endpoint_summary_t* summaries, uint32_t* out_count) {
  return amdf_windows_endpoint_snapshot_enumerate(&instance->kmt, capacity,
                                                  summaries, out_count);
}
