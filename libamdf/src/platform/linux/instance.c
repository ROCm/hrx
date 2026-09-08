// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#define _GNU_SOURCE
#include "libamdf/src/platform/linux/instance.h"

#include <fcntl.h>
#include <stdlib.h>

#include "libamdf/src/platform/linux/file.h"

amdf_status_t amdf_platform_instance_create(
    amdf_platform_instance_t** out_instance) {
  *out_instance = NULL;
  amdf_platform_instance_t* instance = calloc(1, sizeof(*instance));
  if (instance == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_RESOURCE_EXHAUSTED);
  }
  instance->sysfs_descriptor = open("/sys", O_PATH | O_DIRECTORY | O_CLOEXEC);
  if (instance->sysfs_descriptor < 0) {
    const amdf_status_t status = amdf_linux_error(errno);
    free(instance);
    return status;
  }
  *out_instance = instance;
  return AMDF_STATUS_OK;
}

amdf_status_t amdf_platform_instance_destroy(
    amdf_platform_instance_t* instance) {
  const amdf_status_t status =
      amdf_linux_file_close(&instance->sysfs_descriptor);
  if (amdf_status_is_ok(status)) free(instance);
  return status;
}
