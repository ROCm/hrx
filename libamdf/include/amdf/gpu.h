// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef AMDF_GPU_H_
#define AMDF_GPU_H_

#include <stdint.h>

#include "amdf/amdf.h"

#ifdef __cplusplus
extern "C" {
#endif

/// The first supported GPU extension version.
#define AMDF_GPU_EXTENSION_VERSION_1 1u

/// The most recent GPU extension version described by this header.
#define AMDF_GPU_EXTENSION_VERSION_LATEST AMDF_GPU_EXTENSION_VERSION_1

/// An `amdf_gpu_endpoint_info_t` output structure.
#define AMDF_STRUCTURE_TYPE_GPU_ENDPOINT_INFO \
  ((amdf_structure_type_t)0x00020001u)

/// An `amdf_gpu_device_create_info_t` input structure.
#define AMDF_STRUCTURE_TYPE_GPU_DEVICE_CREATE_INFO \
  ((amdf_structure_type_t)0x00020002u)

/// An `amdf_gpu_device_info_t` output structure.
#define AMDF_STRUCTURE_TYPE_GPU_DEVICE_INFO ((amdf_structure_type_t)0x00020003u)

/// An `amdf_gpu_kernel_queue_create_info_t` input structure.
#define AMDF_STRUCTURE_TYPE_GPU_KERNEL_QUEUE_CREATE_INFO \
  ((amdf_structure_type_t)0x00020004u)

/// An `amdf_gpu_kernel_queue_submission_info_t` input structure.
#define AMDF_STRUCTURE_TYPE_GPU_KERNEL_QUEUE_SUBMISSION_INFO \
  ((amdf_structure_type_t)0x00020005u)

/// Immutable target identity and compute topology of one GPU endpoint.
///
/// This record contains live provider-qualified hardware facts. It does not
/// select an executable format, process-level target features, memory policy,
/// or queue implementation.
typedef struct amdf_gpu_endpoint_info_t {
  /// Must be `AMDF_STRUCTURE_TYPE_GPU_ENDPOINT_INFO`.
  amdf_structure_type_t type;
  /// Must be at least `sizeof(amdf_gpu_endpoint_info_t)`.
  uint32_t structure_size;
  /// Optional output extension chain. No extensions are currently defined.
  void* next;
  /// Exact Graphics IP identity.
  struct {
    /// GFX IP major version.
    uint32_t major;
    /// GFX IP minor version.
    uint32_t minor;
    /// GFX IP stepping encoded in the canonical `gfxMmn` target name.
    uint32_t stepping;
  } gfx_ip;
  /// ASIC revision used for physical compiler-target selection.
  ///
  /// This is the HSA/KFD target revision and is distinct from the PCI
  /// config-space revision in `amdf_endpoint_info_t`. Gfx1250 uses values zero
  /// and one to distinguish A0 and B0 targets.
  uint32_t asic_revision;
  /// Active compute geometry and per-compute-unit limits.
  struct {
    /// Number of lanes in one hardware wavefront.
    uint32_t wavefront_size;
    /// Total active compute units across all XCCs after harvesting.
    uint32_t compute_unit_count;
    /// Maximum resident hardware waves per compute unit.
    uint32_t maximum_wave_count_per_compute_unit;
    /// Effective maximum scratch-backed waves per compute unit.
    uint32_t maximum_scratch_wave_count_per_compute_unit;
    /// Local data share capacity per compute unit in bytes.
    uint64_t local_data_share_byte_length;
  } compute;
  /// Multi-chiplet command-processor topology.
  struct {
    /// Nonzero number of active XCCs represented by the endpoint.
    uint32_t xcc_count;
    /// Uniform number of shader engines within each XCC.
    uint32_t shader_engine_count_per_xcc;
  } topology;
} amdf_gpu_endpoint_info_t;

/// Parameters used to materialize one program-independent GPU device.
///
/// A device owns one native GPU execution and address domain. Queue,
/// executable, and memory policy are selected by later operations.
typedef struct amdf_gpu_device_create_info_t {
  /// Must be `AMDF_STRUCTURE_TYPE_GPU_DEVICE_CREATE_INFO`.
  amdf_structure_type_t type;
  /// Must be at least `sizeof(amdf_gpu_device_create_info_t)`.
  uint32_t structure_size;
  /// Optional input extension chain. No extensions are currently defined.
  const void* next;
} amdf_gpu_device_create_info_t;

/// Immutable identity and reset state of one live GPU device.
typedef struct amdf_gpu_device_info_t {
  /// Must be `AMDF_STRUCTURE_TYPE_GPU_DEVICE_INFO`.
  amdf_structure_type_t type;
  /// Must be at least `sizeof(amdf_gpu_device_info_t)`.
  uint32_t structure_size;
  /// Optional output extension chain. No extensions are currently defined.
  void* next;
  /// Opaque identity of this live materialized device.
  amdf_device_id_t id;
  /// Monotonic provider epoch invalidating state after a device reset.
  uint64_t reset_epoch;
} amdf_gpu_device_info_t;

/// One already-materialized PM4 command stream.
///
/// The command bytes reside in executable memory with a stable device address
/// and may be device-local or non-host-visible. Submission resolves only that
/// address; it never reads, validates, copies, hashes, or transcribes the
/// command bytes.
typedef struct amdf_gpu_kernel_command_t {
  /// Memory attachment borrowed until the accepted submission retires.
  amdf_memory_t* memory;
  /// Dword-aligned byte offset from the attachment's stable device base.
  uint64_t byte_offset;
  /// Nonzero dword-aligned command length.
  uint64_t byte_length;
} amdf_gpu_kernel_command_t;

/// Parameters used to acquire one kernel-mediated GPU queue.
typedef struct amdf_gpu_kernel_queue_create_info_t {
  /// Must be `AMDF_STRUCTURE_TYPE_GPU_KERNEL_QUEUE_CREATE_INFO`.
  amdf_structure_type_t type;
  /// Must be at least `sizeof(amdf_gpu_kernel_queue_create_info_t)`.
  uint32_t structure_size;
  /// Optional input extension chain. No extensions are currently defined.
  const void* next;
  /// Endpoint-local PM4 family supporting kernel publication.
  uint32_t queue_family_ordinal;
  /// Reserved for compatible growth and must be zero.
  uint32_t reserved;
} amdf_gpu_kernel_queue_create_info_t;

/// One bounded kernel-mediated GPU submission.
typedef struct amdf_gpu_kernel_queue_submission_info_t {
  /// Must be `AMDF_STRUCTURE_TYPE_GPU_KERNEL_QUEUE_SUBMISSION_INFO`.
  amdf_structure_type_t type;
  /// Must be at least `sizeof(amdf_gpu_kernel_queue_submission_info_t)`.
  uint32_t structure_size;
  /// Optional input extension chain. No extensions are currently defined.
  const void* next;
  /// Number of immutable descriptors in `commands`.
  uint32_t command_count;
  /// Reserved for compatible growth and must be zero.
  uint32_t reserved;
  /// Borrowed descriptor array consumed before return. Each referenced memory
  /// attachment remains borrowed until the accepted submission retires.
  const amdf_gpu_kernel_command_t* commands;
} amdf_gpu_kernel_queue_submission_info_t;

/// Immutable entry-point table for one negotiated GPU extension version.
///
/// Tables grow only by appending fields. The table and every function pointer
/// covered by `structure_size` remain valid until the providing library is
/// unloaded.
typedef struct amdf_gpu_api_t {
  /// Size in bytes of this table version.
  uint32_t structure_size;
  /// GPU extension version implemented by this table.
  uint32_t extension_version;

  /// Copies immutable GPU properties cached while opening `endpoint`.
  ///
  /// The endpoint must have a provider-qualified GPU profile. Passing another
  /// engine or a GPU that the active provider could not qualify returns the
  /// cached qualification error without modifying the output. The operation is
  /// thread-safe and performs no system call, allocation, provider-library
  /// load, retry, sleep, or device wait. The caller initializes `out_info` and
  /// its complete extension chain.
  amdf_status_t(AMDF_CALL* endpoint_query_info)(
      amdf_endpoint_t* endpoint, amdf_gpu_endpoint_info_t* out_info);

  /// Materializes one program-independent GPU execution and address domain.
  ///
  /// `endpoint` remains query-only and may create independent devices. The
  /// returned device borrows the endpoint, which must outlive it. No queue,
  /// executable, command stream, or memory allocation is created. On failure,
  /// `out_device` is set to `NULL`.
  amdf_status_t(AMDF_CALL* device_create)(
      amdf_endpoint_t* endpoint,
      const amdf_gpu_device_create_info_t* create_info,
      amdf_device_t** out_device);

  /// Copies the identity and current reset epoch of `device`.
  ///
  /// The operation is thread-safe and performs no system call, allocation,
  /// device initialization, retry, sleep, or device wait. The caller
  /// initializes `out_info` and its complete extension chain. No output is
  /// modified when validation or engine compatibility fails.
  amdf_status_t(AMDF_CALL* device_query_info)(amdf_device_t* device,
                                              amdf_gpu_device_info_t* out_info);

  /// Acquires one kernel-mediated PM4 queue from a GPU device.
  ///
  /// The returned queue borrows `device`, which must outlive it. Creation
  /// selects an advertised `GPU_PM4 + KERNEL` family and allocates every
  /// bounded submission resource before publication. On failure, `out_queue`
  /// is set to `NULL`.
  amdf_status_t(AMDF_CALL* kernel_queue_create)(
      amdf_device_t* device,
      const amdf_gpu_kernel_queue_create_info_t* create_info,
      amdf_kernel_queue_t** out_queue);

  /// Publishes one bounded array of already-materialized PM4 command streams.
  ///
  /// Every command range must belong to the queue's device and reset epoch and
  /// have `EXECUTABLE` and `DEVICE_ADDRESS` memory flags. The call registers
  /// memory borrows before native acceptance and releases them only when queue
  /// progress later retires the returned submission. It performs no
  /// allocation, command-byte access, native-format parsing, lowering,
  /// transcription, retry, sleep, or host wait. Native rejection leaves
  /// `out_submission` unchanged.
  amdf_status_t(AMDF_CALL* kernel_queue_submit)(
      amdf_kernel_queue_t* queue,
      const amdf_gpu_kernel_queue_submission_info_t* submission_info,
      uint64_t* out_submission);
} amdf_gpu_api_t;

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AMDF_GPU_H_
