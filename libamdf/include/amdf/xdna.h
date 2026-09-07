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

/// Immutable target-native program bytes owned by one XDNA device.
typedef struct amdf_xdna_program_t amdf_xdna_program_t;

/// One immutable fixed-binding realization of an XDNA program.
typedef struct amdf_xdna_command_t amdf_xdna_command_t;

/// An `amdf_xdna_endpoint_info_t` output structure.
#define AMDF_STRUCTURE_TYPE_XDNA_ENDPOINT_INFO \
  ((amdf_structure_type_t)0x00010001u)

/// An `amdf_xdna_device_create_info_t` input structure.
#define AMDF_STRUCTURE_TYPE_XDNA_DEVICE_CREATE_INFO \
  ((amdf_structure_type_t)0x00010002u)

/// An `amdf_xdna_device_info_t` output structure.
#define AMDF_STRUCTURE_TYPE_XDNA_DEVICE_INFO \
  ((amdf_structure_type_t)0x00010003u)

/// An `amdf_xdna_program_create_info_t` input structure.
#define AMDF_STRUCTURE_TYPE_XDNA_PROGRAM_CREATE_INFO \
  ((amdf_structure_type_t)0x00010004u)

/// An `amdf_xdna_program_info_t` output structure.
#define AMDF_STRUCTURE_TYPE_XDNA_PROGRAM_INFO \
  ((amdf_structure_type_t)0x00010005u)

/// An `amdf_xdna_command_create_info_t` input structure.
#define AMDF_STRUCTURE_TYPE_XDNA_COMMAND_CREATE_INFO \
  ((amdf_structure_type_t)0x00010006u)

/// An `amdf_xdna_command_info_t` output structure.
#define AMDF_STRUCTURE_TYPE_XDNA_COMMAND_INFO \
  ((amdf_structure_type_t)0x00010007u)

/// An `amdf_xdna_kernel_queue_create_info_t` input structure.
#define AMDF_STRUCTURE_TYPE_XDNA_KERNEL_QUEUE_CREATE_INFO \
  ((amdf_structure_type_t)0x00010008u)

/// An `amdf_xdna_kernel_queue_submission_info_t` input structure.
#define AMDF_STRUCTURE_TYPE_XDNA_KERNEL_QUEUE_SUBMISSION_INFO \
  ((amdf_structure_type_t)0x00010009u)

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

/// Public target-native byte encodings accepted by an XDNA endpoint.
typedef uint32_t amdf_xdna_binary_format_t;
enum amdf_xdna_binary_format_e {
  /// No public encoding is available for this role.
  AMDF_XDNA_BINARY_FORMAT_UNKNOWN = 0,
  /// A device-generation-specific PDI image.
  AMDF_XDNA_BINARY_FORMAT_PDI = 1,
  /// A device-generation-specific AIE transaction stream.
  AMDF_XDNA_BINARY_FORMAT_TRANSACTION = 2,
};

/// First public contract for AIE transaction-header version 0.1.
#define AMDF_XDNA_TRANSACTION_FORMAT_VERSION_0_1 1u

/// One exact public target-native byte encoding.
typedef struct amdf_xdna_binary_format_info_t {
  /// PDI, transaction stream, or `AMDF_XDNA_BINARY_FORMAT_UNKNOWN`.
  amdf_xdna_binary_format_t format;
  /// Version defining the complete byte encoding.
  uint32_t version;
} amdf_xdna_binary_format_info_t;

/// Optional execution and lifecycle properties required from a program.
typedef uint64_t amdf_xdna_program_flags_t;
enum amdf_xdna_program_flag_bits_e {
  /// The program may remain active while servicing device-visible work.
  AMDF_XDNA_PROGRAM_FLAG_RESIDENT = UINT64_C(1) << 0,
  /// The program provides the state required for cooperative preemption.
  AMDF_XDNA_PROGRAM_FLAG_PREEMPTIBLE = UINT64_C(1) << 1,
};

/// Immutable compiler target and context-admission properties of one endpoint.
///
/// This record describes an exact hardware/compiler profile. Nonzero program
/// and command limits describe the complete native construction contract
/// available through this extension. Queue publication remains a separate
/// queue-family capability.
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
  /// Target-native program construction contract.
  struct {
    /// Resident and preemptible requirements accepted by the provider.
    amdf_xdna_program_flags_t supported_flags;
    /// Maximum number of components in one program.
    uint32_t maximum_component_count;
    /// Reserved for compatible growth and always zero.
    uint32_t reserved;
    /// Maximum number of bytes in one component.
    uint64_t maximum_component_byte_length;
    /// Maximum aggregate component bytes in one program.
    uint64_t maximum_total_byte_length;
    /// Exact public encoding accepted for each semantic component role.
    struct {
      /// Encoding accepted for optional PDI components.
      amdf_xdna_binary_format_info_t pdi;
      /// Encoding accepted for array-configuration components.
      amdf_xdna_binary_format_info_t array_configuration;
      /// Encoding accepted for cooperative-save components.
      amdf_xdna_binary_format_info_t save;
      /// Encoding accepted for cooperative-restore components.
      amdf_xdna_binary_format_info_t restore;
    } component_formats;
  } program;
  /// Fixed-binding prepared-command construction contract.
  struct {
    /// Maximum immutable bindings in one command.
    uint32_t maximum_binding_count;
    /// Reserved for compatible growth and always zero.
    uint32_t reserved;
    /// Maximum invocation-control byte length.
    uint64_t maximum_control_byte_length;
    /// Maximum complete native command after program and control composition.
    uint64_t maximum_native_byte_length;
    /// Exact public encoding accepted for invocation control.
    amdf_xdna_binary_format_info_t control_format;
  } command;
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

/// Coordinate convention used by target-native program destinations.
typedef uint32_t amdf_xdna_coordinate_mode_t;
enum amdf_xdna_coordinate_mode_e {
  /// Coordinates are relative to the materialized device partition.
  AMDF_XDNA_COORDINATE_MODE_CONTEXT_RELATIVE = 1,
  /// Coordinates name physical array columns and rows.
  AMDF_XDNA_COORDINATE_MODE_PHYSICAL = 2,
};

/// Explicit array footprint of one target-native program.
typedef struct amdf_xdna_program_footprint_t {
  /// Coordinate convention used by every native destination.
  amdf_xdna_coordinate_mode_t coordinate_mode;
  /// First declared column, or zero for context-relative coordinates.
  uint32_t column_origin;
  /// Number of contiguous columns in the declared rectangle.
  uint32_t column_count;
  /// Number of contiguous rows in the declared rectangle.
  uint32_t row_count;
} amdf_xdna_program_footprint_t;

/// Semantic role of one copied target-native program component.
typedef uint32_t amdf_xdna_program_component_kind_t;
enum amdf_xdna_program_component_kind_e {
  /// Optional PDI used while activating the program image.
  AMDF_XDNA_PROGRAM_COMPONENT_PDI = 1,
  /// Array and tile configuration installed before invocation control.
  AMDF_XDNA_PROGRAM_COMPONENT_ARRAY_CONFIGURATION = 2,
  /// Cooperative state-save program.
  AMDF_XDNA_PROGRAM_COMPONENT_SAVE = 3,
  /// Cooperative state-restore program.
  AMDF_XDNA_PROGRAM_COMPONENT_RESTORE = 4,
};

/// One target-native component copied by `program_create`.
///
/// `kind` selects the corresponding format reported in
/// `amdf_xdna_endpoint_info_t::program.component_formats`. The byte storage is
/// borrowed only for the duration of the call.
typedef struct amdf_xdna_program_component_t {
  /// Semantic role of the component.
  amdf_xdna_program_component_kind_t kind;
  /// Reserved for compatible growth and must be zero.
  uint32_t reserved;
  /// Borrowed first byte of the target-native component.
  const void* bytes;
  /// Nonzero component length in bytes.
  uint64_t byte_length;
} amdf_xdna_program_component_t;

/// Parameters used to create one immutable target-native XDNA program.
typedef struct amdf_xdna_program_create_info_t {
  /// Must be `AMDF_STRUCTURE_TYPE_XDNA_PROGRAM_CREATE_INFO`.
  amdf_structure_type_t type;
  /// Must be at least `sizeof(amdf_xdna_program_create_info_t)`.
  uint32_t structure_size;
  /// Optional input extension chain. No extensions are currently defined.
  const void* next;
  /// Number of entries in `components`.
  uint32_t component_count;
  /// Reserved for compatible growth and must be zero.
  uint32_t reserved;
  /// Required resident or preemptible behavior.
  amdf_xdna_program_flags_t required_flags;
  /// Borrowed component array consumed before the call returns.
  const amdf_xdna_program_component_t* components;
  /// Native destination rectangle used for admission.
  amdf_xdna_program_footprint_t footprint;
} amdf_xdna_program_create_info_t;

/// Immutable properties of one copied XDNA program.
typedef struct amdf_xdna_program_info_t {
  /// Must be `AMDF_STRUCTURE_TYPE_XDNA_PROGRAM_INFO`.
  amdf_structure_type_t type;
  /// Must be at least `sizeof(amdf_xdna_program_info_t)`.
  uint32_t structure_size;
  /// Optional output extension chain. No extensions are currently defined.
  void* next;
  /// Resident and preemptible properties admitted by the provider.
  amdf_xdna_program_flags_t flags;
  /// Number of copied target-native components.
  uint32_t component_count;
  /// Reserved for compatible growth and always zero.
  uint32_t reserved;
  /// Aggregate number of copied component bytes.
  uint64_t total_byte_length;
  /// Device reset epoch in which prepared commands may use this program.
  uint64_t reset_epoch;
  /// Validated native destination rectangle.
  amdf_xdna_program_footprint_t footprint;
} amdf_xdna_program_info_t;

/// One immutable memory range bound into a prepared XDNA command.
typedef struct amdf_xdna_command_binding_t {
  /// Memory attachment borrowed for the lifetime of the command.
  amdf_memory_t* memory;
  /// Byte offset from the attachment's stable device base.
  uint64_t byte_offset;
  /// Nonzero number of bound bytes.
  uint64_t byte_length;
} amdf_xdna_command_binding_t;

/// Parameters used to prepare one fixed-binding XDNA command.
typedef struct amdf_xdna_command_create_info_t {
  /// Must be `AMDF_STRUCTURE_TYPE_XDNA_COMMAND_CREATE_INFO`.
  amdf_structure_type_t type;
  /// Must be at least `sizeof(amdf_xdna_command_create_info_t)`.
  uint32_t structure_size;
  /// Optional input extension chain. No extensions are currently defined.
  const void* next;
  /// Number of immutable entries in `bindings`.
  uint32_t binding_count;
  /// Reserved for compatible growth and must be zero.
  uint32_t reserved;
  /// Borrowed invocation-control bytes consumed before the call returns.
  const void* control_bytes;
  /// Nonzero length of `control_bytes`.
  uint64_t control_byte_length;
  /// Borrowed binding array consumed before the call returns.
  const amdf_xdna_command_binding_t* bindings;
} amdf_xdna_command_create_info_t;

/// Immutable properties of one fixed-binding XDNA command.
typedef struct amdf_xdna_command_info_t {
  /// Must be `AMDF_STRUCTURE_TYPE_XDNA_COMMAND_INFO`.
  amdf_structure_type_t type;
  /// Must be at least `sizeof(amdf_xdna_command_info_t)`.
  uint32_t structure_size;
  /// Optional output extension chain. No extensions are currently defined.
  void* next;
  /// Number of immutable memory bindings.
  uint32_t binding_count;
  /// Reserved for compatible growth and always zero.
  uint32_t reserved;
  /// Number of copied invocation-control bytes.
  uint64_t control_byte_length;
  /// Device reset epoch in which this realization remains valid.
  uint64_t reset_epoch;
} amdf_xdna_command_info_t;

/// Parameters used to acquire one kernel-mediated XDNA queue.
typedef struct amdf_xdna_kernel_queue_create_info_t {
  /// Must be `AMDF_STRUCTURE_TYPE_XDNA_KERNEL_QUEUE_CREATE_INFO`.
  amdf_structure_type_t type;
  /// Must be at least `sizeof(amdf_xdna_kernel_queue_create_info_t)`.
  uint32_t structure_size;
  /// Optional input extension chain. No extensions are currently defined.
  const void* next;
  /// Endpoint-local XDNA family supporting kernel publication.
  uint32_t queue_family_ordinal;
  /// Reserved for compatible growth and must be zero.
  uint32_t reserved;
} amdf_xdna_kernel_queue_create_info_t;

/// One bounded kernel-mediated XDNA submission.
typedef struct amdf_xdna_kernel_queue_submission_info_t {
  /// Must be `AMDF_STRUCTURE_TYPE_XDNA_KERNEL_QUEUE_SUBMISSION_INFO`.
  amdf_structure_type_t type;
  /// Must be at least `sizeof(amdf_xdna_kernel_queue_submission_info_t)`.
  uint32_t structure_size;
  /// Optional input extension chain. No extensions are currently defined.
  const void* next;
  /// Number of borrowed command pointers in `commands`.
  uint32_t command_count;
  /// Reserved for compatible growth and must be zero.
  uint32_t reserved;
  /// Borrowed pointer array consumed before return. Each command remains
  /// borrowed until the accepted submission retires.
  amdf_xdna_command_t* const* commands;
} amdf_xdna_kernel_queue_submission_info_t;

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

  /// Copies and owns one immutable target-native program for `device`.
  ///
  /// `device` and every component byte span are borrowed only for the call.
  /// The returned program borrows `device`, which must outlive it. Creation
  /// validates the declared footprint and exact formats cached by the endpoint
  /// but performs no queue submission or active-array mutation. On failure,
  /// `out_program` is set to `NULL`.
  amdf_status_t(AMDF_CALL* program_create)(
      amdf_device_t* device, const amdf_xdna_program_create_info_t* create_info,
      amdf_xdna_program_t** out_program);

  /// Copies immutable properties of one XDNA program.
  ///
  /// The operation is thread-safe and performs no allocation, system call,
  /// byte validation, retry, sleep, or device wait. No output is modified when
  /// validation fails.
  amdf_status_t(AMDF_CALL* program_query_info)(
      amdf_xdna_program_t* program, amdf_xdna_program_info_t* out_info);

  /// Destroys one program after all commands borrowing it are gone.
  ///
  /// Returns `AMDF_STATUS_CODE_BUSY` without mutation while a command remains
  /// live. The caller must otherwise have exclusive access.
  amdf_status_t(AMDF_CALL* program_destroy)(amdf_xdna_program_t* program);

  /// Creates one immutable fixed-binding realization of `program`.
  ///
  /// Control and binding arrays are consumed before return. Each binding must
  /// name memory attached to the same device as `program`; the command borrows
  /// every bound memory object until destruction. Device addresses are resolved
  /// once during creation and are never rediscovered by queue submission.
  /// Creation performs no queue submission or active-array mutation. On
  /// failure, `out_command` is set to `NULL`.
  amdf_status_t(AMDF_CALL* command_create)(
      amdf_xdna_program_t* program,
      const amdf_xdna_command_create_info_t* create_info,
      amdf_xdna_command_t** out_command);

  /// Copies immutable properties of one prepared XDNA command.
  ///
  /// The operation is thread-safe and performs no allocation, system call,
  /// command inspection, retry, sleep, or device wait. No output is modified
  /// when validation fails.
  amdf_status_t(AMDF_CALL* command_query_info)(
      amdf_xdna_command_t* command, amdf_xdna_command_info_t* out_info);

  /// Destroys one command after every accepted queue use has retired.
  ///
  /// The caller must have exclusive access. A provider-owned teardown failure
  /// leaves the command live so destruction can be retried.
  amdf_status_t(AMDF_CALL* command_destroy)(amdf_xdna_command_t* command);

  /// Acquires one kernel-mediated queue from an XDNA device.
  ///
  /// The returned queue borrows `device`, which must outlive it. Creation
  /// selects an advertised `XDNA + KERNEL` family and allocates all bounded
  /// submission bookkeeping before publication. On failure, `out_queue` is set
  /// to `NULL`.
  amdf_status_t(AMDF_CALL* kernel_queue_create)(
      amdf_device_t* device,
      const amdf_xdna_kernel_queue_create_info_t* create_info,
      amdf_kernel_queue_t** out_queue);

  /// Publishes one bounded array of already prepared XDNA commands.
  ///
  /// Every command must belong to the queue's device and reset epoch. The call
  /// registers command borrows before native acceptance and releases them only
  /// when queue progress later retires the returned submission. It performs no
  /// allocation, native-format parsing, lowering, binding resolution, command
  /// transcription, retry, sleep, or host wait. Native rejection leaves
  /// `out_submission` unchanged.
  amdf_status_t(AMDF_CALL* kernel_queue_submit)(
      amdf_kernel_queue_t* queue,
      const amdf_xdna_kernel_queue_submission_info_t* submission_info,
      uint64_t* out_submission);
} amdf_xdna_api_t;

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AMDF_XDNA_H_
