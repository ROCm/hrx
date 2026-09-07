// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef AMDF_XDNA_H_
#define AMDF_XDNA_H_

#include <stdint.h>

#include "amdf/amdf.h"

#ifdef __cplusplus
extern "C" {
#endif

/// The first supported XDNA extension version.
#define AMDF_XDNA_EXTENSION_VERSION_1 1u

/// The most recent XDNA extension version described by this header.
#define AMDF_XDNA_EXTENSION_VERSION_LATEST AMDF_XDNA_EXTENSION_VERSION_1

/// An `amdf_xdna_endpoint_info_t` output structure.
#define AMDF_STRUCTURE_TYPE_XDNA_ENDPOINT_INFO \
  ((amdf_structure_type_t)0x00010001u)

/// An `amdf_xdna_device_create_info_t` input structure.
#define AMDF_STRUCTURE_TYPE_XDNA_DEVICE_CREATE_INFO \
  ((amdf_structure_type_t)0x00010002u)

/// An `amdf_xdna_device_info_t` output structure.
#define AMDF_STRUCTURE_TYPE_XDNA_DEVICE_INFO \
  ((amdf_structure_type_t)0x00010003u)

/// Lets the provider select the physical origin of an XDNA device.
#define AMDF_XDNA_PHYSICAL_COLUMN_ORIGIN_ANY UINT32_MAX

/// Capacity in bytes of a NUL-terminated canonical XDNA target identifier.
#define AMDF_XDNA_TARGET_ID_CAPACITY 64u

/// Compiler-visible AIE instruction and array architecture.
typedef uint32_t amdf_xdna_architecture_t;
enum amdf_xdna_architecture_e {
  /// The architecture is unavailable or not represented by this API version.
  AMDF_XDNA_ARCHITECTURE_UNKNOWN = 0,
  /// The AIE2 architecture implemented by NPU1-family XDNA devices.
  AMDF_XDNA_ARCHITECTURE_AIE2 = 1,
  /// The AIE2P architecture implemented by Strix-family XDNA devices.
  AMDF_XDNA_ARCHITECTURE_AIE2P = 2,
  /// The AIE4 architecture implemented by newer XDNA devices.
  AMDF_XDNA_ARCHITECTURE_AIE4 = 3,
};

/// Context scheduling modes admitted by an XDNA endpoint.
typedef uint32_t amdf_xdna_scheduling_modes_t;
enum amdf_xdna_scheduling_mode_bits_e {
  /// A context can receive exclusive ownership of its physical placement.
  AMDF_XDNA_SCHEDULING_MODE_EXCLUSIVE = 1u << 0,
  /// Contexts can execute concurrently on disjoint spatial placements.
  AMDF_XDNA_SCHEDULING_MODE_SPATIAL = 1u << 1,
  /// Contexts can time-share one physical placement.
  AMDF_XDNA_SCHEDULING_MODE_TIME_SLICED = 1u << 2,
};

/// Immutable compiler target and context-admission properties of one endpoint.
///
/// This record describes an exact hardware/compiler profile. It does not imply
/// that any queue publication mechanism or executable format is available;
/// those capabilities belong to later queue-family and program queries.
typedef struct amdf_xdna_endpoint_info_t {
  /// Must be `AMDF_STRUCTURE_TYPE_XDNA_ENDPOINT_INFO`.
  amdf_structure_type_t type;
  /// Must be at least `sizeof(amdf_xdna_endpoint_info_t)`.
  uint32_t structure_size;
  /// Optional output extension chain. No extensions are currently defined.
  void* next;
  /// Compiler-visible AIE instruction and array architecture.
  amdf_xdna_architecture_t architecture;
  /// Physical array geometry and addressing.
  struct {
    /// Physical index of the first addressable array column.
    uint32_t column_origin;
    /// Number of contiguous addressable physical array columns.
    uint32_t column_count;
    /// Total physical rows, including shim, memory, and compute rows.
    uint32_t row_count;
    /// Device-address distance in bytes between adjacent columns.
    uint64_t column_stride;
  } array;
  /// Context resource and scheduling limits independent of current occupancy.
  struct {
    /// Exclusive, spatial, and time-sliced modes accepted by the endpoint.
    amdf_xdna_scheduling_modes_t scheduling_modes;
    /// Minimum logical column count requestable by one context.
    uint32_t minimum_column_count;
    /// Maximum logical column count requestable by one context.
    uint32_t maximum_column_count;
    /// Granularity of requestable logical column counts.
    uint32_t column_count_granularity;
    /// Maximum simultaneously live public context objects.
    uint32_t maximum_live_context_count;
    /// Maximum contexts simultaneously carrying hardware or firmware state.
    uint32_t maximum_hardware_context_count;
  } context;
  /// Exact NUL-terminated compiler target and device-profile identifier.
  char target_id[AMDF_XDNA_TARGET_ID_CAPACITY];
} amdf_xdna_endpoint_info_t;

/// Parameters used to materialize one program-independent XDNA device.
///
/// A device owns one native XDNA context and stable address domain. Creating
/// another placement from the same endpoint creates another independent
/// device; no executable or program bytes participate in this operation.
typedef struct amdf_xdna_device_create_info_t {
  /// Must be `AMDF_STRUCTURE_TYPE_XDNA_DEVICE_CREATE_INFO`.
  amdf_structure_type_t type;
  /// Must be at least `sizeof(amdf_xdna_device_create_info_t)`.
  uint32_t structure_size;
  /// Optional input extension chain. No extensions are currently defined.
  const void* next;
  /// Number of logical array columns requested for the context.
  uint32_t logical_column_count;
  /// Exact physical partition origin or
  /// `AMDF_XDNA_PHYSICAL_COLUMN_ORIGIN_ANY` to let the provider choose.
  uint32_t physical_column_origin;
  /// Nonempty set of scheduling modes acceptable to the caller.
  amdf_xdna_scheduling_modes_t acceptable_scheduling_modes;
} amdf_xdna_device_create_info_t;

/// Achieved placement and identity of one live XDNA device.
typedef struct amdf_xdna_device_info_t {
  /// Must be `AMDF_STRUCTURE_TYPE_XDNA_DEVICE_INFO`.
  amdf_structure_type_t type;
  /// Must be at least `sizeof(amdf_xdna_device_info_t)`.
  uint32_t structure_size;
  /// Optional output extension chain. No extensions are currently defined.
  void* next;
  /// Opaque identity of this live materialized device.
  amdf_device_id_t id;
  /// Monotonic provider epoch invalidating state after a device reset.
  uint64_t reset_epoch;
  /// Single scheduling mode selected from the acceptable input set.
  amdf_xdna_scheduling_modes_t scheduling_mode;
  /// Generation of the achieved placement reported below.
  uint32_t placement_generation;
  /// Achieved logical request and physical backing partition.
  struct {
    /// Logical column count admitted for programs and commands.
    uint32_t logical_count;
    /// Physical origin of the backing array partition.
    uint32_t physical_origin;
    /// Physical column count of the backing array partition.
    uint32_t physical_count;
  } columns;
  /// Number of physical rows visible within each admitted column.
  uint32_t row_count;
} amdf_xdna_device_info_t;

/// Immutable entry-point table for one negotiated XDNA extension version.
///
/// Tables grow only by appending fields. The table and every function pointer
/// covered by `structure_size` remain valid until the providing library is
/// unloaded.
typedef struct amdf_xdna_api_t {
  /// Size in bytes of this table version.
  uint32_t structure_size;
  /// XDNA extension version implemented by this table.
  uint32_t extension_version;

  /// Copies immutable XDNA properties cached while opening `endpoint`.
  ///
  /// The endpoint must have an exactly qualified XDNA profile. Passing another
  /// engine or an unqualified XDNA candidate returns
  /// `AMDF_STATUS_CODE_UNSUPPORTED`. The operation is thread-safe and performs
  /// no system call, firmware transaction, allocation, retry, sleep, or device
  /// wait. The caller initializes `out_info` and its complete extension chain;
  /// no output is modified when validation or qualification fails.
  amdf_status_t(AMDF_CALL* endpoint_query_info)(
      amdf_endpoint_t* endpoint, amdf_xdna_endpoint_info_t* out_info);

  /// Materializes one program-independent XDNA context and address domain.
  ///
  /// `endpoint` remains query-only and may create independent devices. The
  /// returned device borrows the endpoint, which must outlive it. The selected
  /// scheduling mode and achieved placement are copied by `device_query_info`.
  /// No executable, PDI, xclbin, transaction, or control bytes are accepted or
  /// parsed. On failure, `out_device` is set to `NULL`.
  amdf_status_t(AMDF_CALL* device_create)(
      amdf_endpoint_t* endpoint,
      const amdf_xdna_device_create_info_t* create_info,
      amdf_device_t** out_device);

  /// Copies the identity and current achieved placement of `device`.
  ///
  /// The operation is thread-safe and performs no system call, allocation,
  /// device initialization, retry, sleep, or device wait. The caller
  /// initializes `out_info` and its complete extension chain. No output is
  /// modified when validation or engine compatibility fails.
  amdf_status_t(AMDF_CALL* device_query_info)(
      amdf_device_t* device, amdf_xdna_device_info_t* out_info);
} amdf_xdna_api_t;

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AMDF_XDNA_H_
