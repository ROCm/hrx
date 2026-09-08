// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/hal/drivers/amdgpu/hsa_queue.h"

iree_status_t iree_hal_amdgpu_hsa_queue_create(
    const iree_hal_amdgpu_hsa_queue_params_t* params, hsa_queue_t** out_queue) {
  if (IREE_UNLIKELY((params->compute_unit_mask_bit_count == 0) !=
                    (params->compute_unit_mask == NULL))) {
    return iree_make_status(
        IREE_STATUS_INVALID_ARGUMENT,
        "native compute-unit mask count and storage must both be empty or "
        "both be present");
  }
  if (IREE_UNLIKELY(params->compute_unit_mask_bit_count % 32u != 0)) {
    return iree_make_status(
        IREE_STATUS_INVALID_ARGUMENT,
        "native compute-unit mask bit count %u is not a multiple of 32",
        params->compute_unit_mask_bit_count);
  }
  if (IREE_UNLIKELY(params->packet_count >
                    UINT32_MAX / sizeof(hsa_kernel_dispatch_packet_t))) {
    return iree_make_status(IREE_STATUS_OUT_OF_RANGE,
                            "HSA queue packet capacity %u exceeds the native "
                            "queue byte-length limit",
                            params->packet_count);
  }
  if (IREE_UNLIKELY(params->type != HSA_QUEUE_TYPE_MULTI &&
                    params->type != HSA_QUEUE_TYPE_COOPERATIVE)) {
    return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                            "unsupported native HSA queue type %u",
                            params->type);
  }
  const bool is_cooperative = params->type == HSA_QUEUE_TYPE_COOPERATIVE;
  if (IREE_UNLIKELY(is_cooperative &&
                    params->priority != HSA_AMD_QUEUE_PRIORITY_NORMAL)) {
    return iree_make_status(
        IREE_STATUS_INVALID_ARGUMENT,
        "native cooperative queues require normal scheduling priority");
  }

  hsa_amd_queue_create_desc_t descriptor = {
      .version = HSA_AMD_QUEUE_CREATE_DESC_VERSION,
      .flags = HSA_AMD_QUEUE_CREATE_SYSTEM_MEM,
      .engine_type = HSA_AMD_QUEUE_ENGINE_COMPUTE,
      .queue_size_bytes = (uint32_t)(params->packet_count *
                                     sizeof(hsa_kernel_dispatch_packet_t)),
      .priority = params->priority,
      .callback = params->error_callback,
      .callback_data = params->error_callback_data,
      .engine.compute =
          {
              // ROCr exposes one shared cooperative queue per agent. Applying
              // a mask through the descriptor would mutate that shared queue;
              // verify its achieved full-resource mask below instead.
              .cu_mask = is_cooperative ? NULL : params->compute_unit_mask,
              .type = params->type,
              .private_segment_size = HSA_AMD_PRIVATE_SEGMENT_SIZE_DEFAULT,
              .cu_mask_count =
                  is_cooperative ? 0 : params->compute_unit_mask_bit_count,
          },
  };
  iree_status_t status = iree_hsa_amd_queue_create(IREE_LIBHSA(params->libhsa),
                                                   params->agent, &descriptor,
                                                   /*num_descs=*/1);
  if (iree_status_is_ok(status) && IREE_UNLIKELY(!descriptor.queue)) {
    status = iree_make_status(
        IREE_STATUS_INTERNAL,
        "HSA reported successful queue creation without returning a queue");
  }

  uint32_t* achieved_mask = NULL;
  const iree_host_size_t mask_word_count =
      params->compute_unit_mask_bit_count / 32u;
  if (iree_status_is_ok(status) && mask_word_count) {
    status = iree_allocator_malloc_array(
        params->host_allocator, mask_word_count, sizeof(*achieved_mask),
        (void**)&achieved_mask);
  }
  if (iree_status_is_ok(status) && mask_word_count) {
    status = iree_hsa_amd_queue_cu_get_mask(
        IREE_LIBHSA(params->libhsa), descriptor.queue,
        params->compute_unit_mask_bit_count, achieved_mask);
  }
  if (iree_status_is_ok(status) && mask_word_count &&
      memcmp(params->compute_unit_mask, achieved_mask,
             mask_word_count * sizeof(*achieved_mask)) != 0) {
    status = iree_make_status(
        IREE_STATUS_FAILED_PRECONDITION,
        "HSA created a queue with a different compute-unit mask than "
        "requested");
  }
  iree_allocator_free(params->host_allocator, achieved_mask);

  if (iree_status_is_ok(status)) {
    *out_queue = descriptor.queue;
  } else if (descriptor.queue) {
    iree_hal_amdgpu_hsa_queue_destroy(params->libhsa, descriptor.queue);
  }
  return status;
}

void iree_hal_amdgpu_hsa_queue_destroy(const iree_hal_amdgpu_libhsa_t* libhsa,
                                       hsa_queue_t* queue) {
  iree_hal_amdgpu_hsa_cleanup_assert_success(
      iree_hsa_queue_destroy_raw(libhsa, queue));
}
