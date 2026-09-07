// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/hal/drivers/amdgpu/hsa_queue.h"

iree_status_t iree_hal_amdgpu_hsa_queue_create(
    const iree_hal_amdgpu_hsa_queue_params_t* params, hsa_queue_t** out_queue) {
  hsa_queue_t* queue = NULL;
  iree_status_t status = iree_hsa_queue_create(
      IREE_LIBHSA(params->libhsa), params->agent, params->packet_count,
      params->type, params->error_callback, params->error_callback_data,
      /*private_segment_size=*/UINT32_MAX,
      /*group_segment_size=*/UINT32_MAX, &queue);
  if (iree_status_is_ok(status)) {
    *out_queue = queue;
  } else if (queue) {
    iree_hal_amdgpu_hsa_queue_destroy(params->libhsa, queue);
  }
  return status;
}

void iree_hal_amdgpu_hsa_queue_destroy(const iree_hal_amdgpu_libhsa_t* libhsa,
                                       hsa_queue_t* queue) {
  iree_hal_amdgpu_hsa_cleanup_assert_success(
      iree_hsa_queue_destroy_raw(libhsa, queue));
}
