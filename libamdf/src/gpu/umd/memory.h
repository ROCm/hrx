// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef AMDF_SRC_GPU_UMD_MEMORY_H_
#define AMDF_SRC_GPU_UMD_MEMORY_H_

#include "amdf/amdf.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

typedef struct amdf_gpu_umd_memory_t amdf_gpu_umd_memory_t;
typedef struct amdf_gpu_umd_host_mapping_t amdf_gpu_umd_host_mapping_t;
typedef struct amdf_gpu_umd_device_t amdf_gpu_umd_device_t;

// Native GPU memory properties established before publication.
typedef struct amdf_gpu_umd_memory_result_t {
  // Achieved physical placement class.
  amdf_memory_class_t memory_class;
  // Achieved attachment properties.
  amdf_memory_flags_t flags;
  // Physical allocation length in bytes.
  uint64_t byte_length;
  // Guaranteed allocation-base alignment in every supported address space.
  uint64_t alignment;
  // Identity of the physical backing within the provider instance.
  amdf_physical_memory_id_t physical_backing_id;
  // Stable GPU virtual base.
  uint64_t device_address;
} amdf_gpu_umd_memory_result_t;

// Native host mapping properties established before publication.
typedef struct amdf_gpu_umd_host_mapping_result_t {
  // Achieved host access flags.
  amdf_memory_map_flags_t flags;
  // First mapped byte.
  void* pointer;
  // Mapped byte length.
  uint64_t byte_length;
  // Host cache behavior of the mapped pages.
  amdf_host_cacheability_t cacheability;
  // Host cache-line length in bytes.
  uint32_t cache_line_size;
} amdf_gpu_umd_host_mapping_result_t;

// Creates physical backing and a stable attachment to `device`.
amdf_status_t amdf_gpu_umd_memory_create(
    amdf_gpu_umd_device_t* device, const amdf_memory_create_info_t* create_info,
    amdf_gpu_umd_memory_t** out_memory,
    amdf_gpu_umd_memory_result_t* out_result);

// Releases a memory attachment and its physical backing.
amdf_status_t amdf_gpu_umd_memory_destroy(amdf_gpu_umd_memory_t* memory);

// Creates one explicit host mapping.
amdf_status_t amdf_gpu_umd_memory_map(
    amdf_gpu_umd_memory_t* memory, const amdf_memory_map_info_t* map_info,
    amdf_gpu_umd_host_mapping_t** out_mapping,
    amdf_gpu_umd_host_mapping_result_t* out_result);

// Performs one host cache ownership transition.
amdf_status_t amdf_gpu_umd_host_mapping_cache_control(
    amdf_gpu_umd_host_mapping_t* mapping, amdf_host_cache_operation_t operation,
    uint64_t byte_offset, uint64_t byte_length);

// Releases one explicit host mapping.
amdf_status_t amdf_gpu_umd_host_mapping_destroy(
    amdf_gpu_umd_host_mapping_t* mapping);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // AMDF_SRC_GPU_UMD_MEMORY_H_
