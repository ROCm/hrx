// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef AMDF_AMDF_H_
#define AMDF_AMDF_H_

#include <stdbool.h>
#include <stdint.h>

#if defined(_WIN32)
#define AMDF_CALL __cdecl
#if defined(AMDF_SHARED_LIBRARY)
#define AMDF_API __declspec(dllimport)
#else
#define AMDF_API
#endif
#else
#define AMDF_CALL
#if defined(__GNUC__) || defined(__clang__)
#define AMDF_API __attribute__((visibility("default")))
#else
#define AMDF_API
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/// Version number identifying a compatible public API table layout.
typedef uint32_t amdf_abi_version_t;

/// The first supported libamdf ABI version.
#define AMDF_ABI_VERSION_1 ((amdf_abi_version_t)1)

/// The most recent ABI version described by this header.
#define AMDF_ABI_VERSION_LATEST AMDF_ABI_VERSION_1

/// The unmangled symbol used to acquire the immutable API table.
#define AMDF_QUERY_API_SYMBOL "amdf_query_api"

/// An encoded status domain and domain-specific 32-bit code.
typedef uint64_t amdf_status_t;

/// Indicates successful completion.
#define AMDF_STATUS_OK ((amdf_status_t)0)

/// Status-code domain identifier.
typedef uint32_t amdf_status_domain_t;
enum amdf_status_domain_e {
  /// Portable status codes declared by this header.
  AMDF_STATUS_DOMAIN_API = 0,
  /// Windows NTSTATUS values.
  AMDF_STATUS_DOMAIN_NTSTATUS = 1,
  /// Device-firmware status values.
  AMDF_STATUS_DOMAIN_FIRMWARE = 2,
  /// POSIX errno values.
  AMDF_STATUS_DOMAIN_ERRNO = 3,
  /// Windows Win32 error values returned by `GetLastError`.
  AMDF_STATUS_DOMAIN_WIN32 = 4,
};

/// Portable status code used with `AMDF_STATUS_DOMAIN_API`.
typedef uint32_t amdf_status_code_t;
enum amdf_status_code_e {
  /// The operation completed successfully.
  AMDF_STATUS_CODE_OK = 0,
  /// An argument was malformed or violated a precondition of its type.
  AMDF_STATUS_CODE_INVALID_ARGUMENT = 1,
  /// A numeric argument was outside the accepted range.
  AMDF_STATUS_CODE_OUT_OF_RANGE = 2,
  /// The requested operation or capability is not implemented.
  AMDF_STATUS_CODE_UNSUPPORTED = 3,
  /// The requested object or capability was not found.
  AMDF_STATUS_CODE_NOT_FOUND = 4,
  /// A finite resource required by the operation was exhausted.
  AMDF_STATUS_CODE_RESOURCE_EXHAUSTED = 5,
  /// A resource is temporarily owned by another operation.
  AMDF_STATUS_CODE_BUSY = 6,
  /// The operation did not complete before its caller-provided deadline.
  AMDF_STATUS_CODE_DEADLINE_EXCEEDED = 7,
  /// The process lacks permission for the requested operation.
  AMDF_STATUS_CODE_PERMISSION_DENIED = 8,
  /// The device or its owning driver can no longer execute work.
  AMDF_STATUS_CODE_DEVICE_LOST = 9,
  /// The caller and library have no mutually supported ABI version.
  AMDF_STATUS_CODE_VERSION_MISMATCH = 10,
  /// Caller-provided storage is too small for the requested result.
  AMDF_STATUS_CODE_BUFFER_TOO_SMALL = 11,
  /// Valid arguments describe an operation that cannot begin in this state.
  AMDF_STATUS_CODE_FAILED_PRECONDITION = 12,
  /// An invariant failed inside the implementation.
  AMDF_STATUS_CODE_INTERNAL = 13,
};

/// Encodes one domain-specific status code. Code zero maps to
/// `AMDF_STATUS_OK` in every domain.
static inline amdf_status_t amdf_make_status(amdf_status_domain_t domain,
                                             uint32_t code) {
  return code == 0 ? AMDF_STATUS_OK : ((amdf_status_t)domain << 32) | code;
}

/// Encodes one portable libamdf status code.
static inline amdf_status_t amdf_make_api_status(amdf_status_code_t code) {
  return amdf_make_status(AMDF_STATUS_DOMAIN_API, code);
}

/// Returns true when status represents success.
static inline bool amdf_status_is_ok(amdf_status_t status) {
  return status == AMDF_STATUS_OK;
}

/// Returns the domain encoded in status.
static inline amdf_status_domain_t amdf_status_domain(amdf_status_t status) {
  return (amdf_status_domain_t)(status >> 32);
}

/// Returns the domain-specific code encoded in status.
static inline uint32_t amdf_status_code(amdf_status_t status) {
  return (uint32_t)status;
}

/// Type identifier carried by every extensible API structure.
typedef uint32_t amdf_structure_type_t;
enum amdf_structure_type_e {
  /// No structure type. This value is never accepted by an API operation.
  AMDF_STRUCTURE_TYPE_NONE = 0,
  /// An `amdf_instance_create_info_t` input structure.
  AMDF_STRUCTURE_TYPE_INSTANCE_CREATE_INFO = 1,
  /// An `amdf_endpoint_info_t` output structure.
  AMDF_STRUCTURE_TYPE_ENDPOINT_INFO = 2,
  /// An `amdf_queue_family_info_t` output structure.
  AMDF_STRUCTURE_TYPE_QUEUE_FAMILY_INFO = 3,
  /// An `amdf_memory_create_info_t` input structure.
  AMDF_STRUCTURE_TYPE_MEMORY_CREATE_INFO = 4,
  /// An `amdf_memory_info_t` output structure.
  AMDF_STRUCTURE_TYPE_MEMORY_INFO = 5,
  /// An `amdf_memory_map_info_t` input structure.
  AMDF_STRUCTURE_TYPE_MEMORY_MAP_INFO = 6,
  /// An `amdf_host_mapping_info_t` output structure.
  AMDF_STRUCTURE_TYPE_HOST_MAPPING_INFO = 7,
  /// An `amdf_kernel_queue_info_t` output structure.
  AMDF_STRUCTURE_TYPE_KERNEL_QUEUE_INFO = 8,
  /// An `amdf_kernel_queue_status_t` output structure.
  AMDF_STRUCTURE_TYPE_KERNEL_QUEUE_STATUS = 9,
};

/// Identifier of an optional API table compiled into the providing library.
typedef uint32_t amdf_extension_id_t;
enum amdf_extension_id_e {
  /// XDNA endpoint qualification and execution services.
  AMDF_EXTENSION_XDNA = 1,
  /// GPU endpoint qualification and execution services.
  AMDF_EXTENSION_GPU = 2,
};

/// Common prefix of every extensible input structure.
typedef struct amdf_input_structure_t {
  /// Type identifying the complete structure.
  amdf_structure_type_t type;
  /// Size in bytes of the complete structure supplied by the caller.
  uint32_t structure_size;
  /// Optional pointer to the next extensible input structure.
  const void* next;
} amdf_input_structure_t;

/// Common prefix of every extensible output structure.
typedef struct amdf_output_structure_t {
  /// Type identifying the complete structure.
  amdf_structure_type_t type;
  /// Size in bytes of the complete structure supplied by the caller.
  uint32_t structure_size;
  /// Optional pointer to the next extensible output structure.
  void* next;
} amdf_output_structure_t;

/// Explicit provider instance owning loaded modules and native API state.
typedef struct amdf_instance_t amdf_instance_t;

/// Query-only handle to one independently selectable execution endpoint.
typedef struct amdf_endpoint_t amdf_endpoint_t;

/// Live engine context and address domain materialized from an endpoint.
typedef struct amdf_device_t amdf_device_t;

/// Physical backing and one stable attachment to a materialized device.
typedef struct amdf_memory_t amdf_memory_t;

/// Explicit host access to one range of host-visible memory.
typedef struct amdf_host_mapping_t amdf_host_mapping_t;

/// Kernel-mediated publication and retirement of native commands.
typedef struct amdf_kernel_queue_t amdf_kernel_queue_t;

/// Parameters used to create an independent provider instance.
typedef struct amdf_instance_create_info_t {
  /// Must be `AMDF_STRUCTURE_TYPE_INSTANCE_CREATE_INFO`.
  amdf_structure_type_t type;
  /// Must be at least `sizeof(amdf_instance_create_info_t)`.
  uint32_t structure_size;
  /// Optional input extension chain. No extensions are defined in ABI v1.
  const void* next;
} amdf_instance_create_info_t;

/// Opaque provider identity used to reopen one enumerated endpoint.
///
/// The value is meaningful only on the machine and provider implementation
/// that produced it. It is not a persistent machine identifier and may become
/// stale after device removal, reset, disable/enable, or driver replacement.
typedef struct amdf_endpoint_id_t {
  /// Provider-defined identity words.
  uint64_t words[2];
} amdf_endpoint_id_t;

/// Returns true when two endpoint identities contain the same opaque value.
static inline bool amdf_endpoint_id_is_equal(const amdf_endpoint_id_t* lhs,
                                             const amdf_endpoint_id_t* rhs) {
  return lhs->words[0] == rhs->words[0] && lhs->words[1] == rhs->words[1];
}

/// Opaque identity of one live materialized device.
///
/// The value is meaningful only while the device and its provider instance
/// remain live. It is intended for correlation and compatibility checks, not
/// persistence or native-handle recovery.
typedef struct amdf_device_id_t {
  /// Provider-defined identity words.
  uint64_t words[2];
} amdf_device_id_t;

/// Returns true when two device identities contain the same opaque value.
static inline bool amdf_device_id_is_equal(const amdf_device_id_t* lhs,
                                           const amdf_device_id_t* rhs) {
  return lhs->words[0] == rhs->words[0] && lhs->words[1] == rhs->words[1];
}

/// Opaque identity of live physical backing within one provider instance.
///
/// Equal nonzero identities prove that two attachments name the same physical
/// backing. An all-zero identity means that the provider cannot establish
/// physical identity. The value is not persistent and is never a native
/// handle.
typedef struct amdf_physical_memory_id_t {
  /// Provider-defined identity words.
  uint64_t words[2];
} amdf_physical_memory_id_t;

/// Returns true when a physical-memory identity is available.
static inline bool amdf_physical_memory_id_is_valid(
    const amdf_physical_memory_id_t* id) {
  return (id->words[0] | id->words[1]) != 0;
}

/// Returns true when two physical-memory identities contain the same value.
static inline bool amdf_physical_memory_id_is_equal(
    const amdf_physical_memory_id_t* lhs,
    const amdf_physical_memory_id_t* rhs) {
  return lhs->words[0] == rhs->words[0] && lhs->words[1] == rhs->words[1];
}

/// Broad execution-engine class of an endpoint.
typedef uint32_t amdf_engine_kind_t;
enum amdf_engine_kind_e {
  /// The provider cannot classify the endpoint without engine qualification.
  AMDF_ENGINE_KIND_UNKNOWN = 0,
  /// An AMD GPU endpoint, including RDNA and CDNA targets.
  AMDF_ENGINE_KIND_GPU = 1,
  /// An AMD XDNA/AIE endpoint.
  AMDF_ENGINE_KIND_XDNA = 2,
};

/// Standard operating-system properties of an endpoint.
typedef uint32_t amdf_endpoint_type_flags_t;
enum amdf_endpoint_type_flag_bits_e {
  /// The endpoint can drive a display.
  AMDF_ENDPOINT_TYPE_FLAG_DISPLAY_SUPPORTED = 1u << 0,
  /// The endpoint supports graphics rendering.
  AMDF_ENDPOINT_TYPE_FLAG_RENDER_SUPPORTED = 1u << 1,
  /// The operating system reports a compute-only adapter.
  AMDF_ENDPOINT_TYPE_FLAG_COMPUTE_ONLY = 1u << 2,
  /// The endpoint is implemented entirely in software.
  AMDF_ENDPOINT_TYPE_FLAG_SOFTWARE_DEVICE = 1u << 3,
};

/// Native command representation accepted by a queue family.
typedef uint32_t amdf_queue_command_type_t;
enum amdf_queue_command_type_e {
  /// No command representation. Advertised families never use this value.
  AMDF_QUEUE_COMMAND_TYPE_UNKNOWN = 0,
  /// Native AMD GPU PM4 command streams.
  AMDF_QUEUE_COMMAND_TYPE_GPU_PM4 = 1,
  /// Native AMD GPU SDMA command streams.
  AMDF_QUEUE_COMMAND_TYPE_GPU_SDMA = 2,
  /// Native AMD GPU AQL packets reaching hardware without CPU translation.
  AMDF_QUEUE_COMMAND_TYPE_GPU_AQL = 3,
  /// Native AMD XDNA execution commands.
  AMDF_QUEUE_COMMAND_TYPE_XDNA = 4,
};

/// Queue publication mechanisms implemented by a provider.
typedef uint32_t amdf_queue_publication_modes_t;
enum amdf_queue_publication_mode_bits_e {
  /// Commands are published directly through caller-mapped queue state.
  AMDF_QUEUE_PUBLICATION_MODE_USER = 1u << 0,
  /// Commands are accepted through a bounded provider call.
  AMDF_QUEUE_PUBLICATION_MODE_KERNEL = 1u << 1,
};

/// Capacity in bytes of a NUL-terminated endpoint diagnostic name.
#define AMDF_ENDPOINT_NAME_CAPACITY 128u

/// Immutable PCI identity of an endpoint, when reported by the platform.
typedef struct amdf_pci_info_t {
  /// PCI vendor identifier, or zero when unavailable.
  uint32_t vendor_id;
  /// PCI device identifier, or zero when unavailable.
  uint32_t device_id;
  /// PCI subsystem vendor identifier, or zero when unavailable.
  uint32_t subsystem_vendor_id;
  /// PCI subsystem device identifier, or zero when unavailable.
  uint32_t subsystem_device_id;
  /// PCI revision identifier, or zero when unavailable.
  uint32_t revision_id;
} amdf_pci_info_t;

/// Fixed-stride endpoint identity returned by `endpoint_enumerate`.
///
/// This structure is immutable for ABI v1. It contains no pointers, extension
/// chain, or provider-owned storage so arrays always have one stable stride.
typedef struct amdf_endpoint_summary_t {
  /// Opaque identity accepted by `endpoint_open`.
  amdf_endpoint_id_t id;
  /// Broad engine class, or `AMDF_ENGINE_KIND_UNKNOWN` when not yet qualified.
  amdf_engine_kind_t engine_kind;
  /// Standard endpoint properties.
  amdf_endpoint_type_flags_t type_flags;
  /// NUL-terminated UTF-8 diagnostic name. Never use this for classification.
  char name[AMDF_ENDPOINT_NAME_CAPACITY];
} amdf_endpoint_summary_t;

/// Immutable properties of one endpoint-local native queue family.
///
/// A family identifies one command representation independently from the
/// mechanisms available to publish it. Advertising a publication mode is a
/// promise that a later queue constructor can create that command/publication
/// pair for the opened endpoint; it is not a list of theoretical hardware
/// capabilities.
typedef struct amdf_queue_family_info_t {
  /// Must be `AMDF_STRUCTURE_TYPE_QUEUE_FAMILY_INFO`.
  amdf_structure_type_t type;
  /// Must be at least `sizeof(amdf_queue_family_info_t)`.
  uint32_t structure_size;
  /// Optional output extension chain. No extensions are defined in ABI v1.
  void* next;
  /// Dense ordinal accepted by later queue creation operations.
  uint32_t ordinal;
  /// Native command representation accepted by queues in this family.
  amdf_queue_command_type_t command_type;
  /// User- and kernel-mode publication paths implemented by the provider.
  amdf_queue_publication_modes_t publication_modes;
} amdf_queue_family_info_t;

/// An infinite timeout accepted by operations that explicitly wait.
#define AMDF_TIMEOUT_INFINITE UINT64_MAX

/// Lifecycle state of one kernel-mediated queue.
typedef uint32_t amdf_kernel_queue_state_t;
enum amdf_kernel_queue_state_e {
  /// The queue accepts new work subject to its reported capacity.
  AMDF_KERNEL_QUEUE_STATE_ACTIVE = 1,
  /// The queue encountered a terminal provider or firmware failure.
  AMDF_KERNEL_QUEUE_STATE_FAILED = 2,
  /// The queue's device can no longer execute or retire work.
  AMDF_KERNEL_QUEUE_STATE_DEVICE_LOST = 3,
};

/// Immutable properties of one kernel-mediated queue.
typedef struct amdf_kernel_queue_info_t {
  /// Must be `AMDF_STRUCTURE_TYPE_KERNEL_QUEUE_INFO`.
  amdf_structure_type_t type;
  /// Must be at least `sizeof(amdf_kernel_queue_info_t)`.
  uint32_t structure_size;
  /// Optional output extension chain. No extensions are currently defined.
  void* next;
  /// Identity of the materialized device owning this queue.
  amdf_device_id_t device_id;
  /// Device reset epoch in which this queue remains valid.
  uint64_t reset_epoch;
  /// Endpoint-local family selected when the queue was created.
  uint32_t queue_family_ordinal;
  /// Native command representation accepted by the queue.
  amdf_queue_command_type_t command_type;
  /// Maximum accepted submissions that may remain unretired.
  uint32_t maximum_pending_submission_count;
  /// Maximum commands accepted by one submission.
  uint32_t maximum_command_count;
} amdf_kernel_queue_info_t;

/// Current retirement and terminal state of one kernel-mediated queue.
typedef struct amdf_kernel_queue_status_t {
  /// Must be `AMDF_STRUCTURE_TYPE_KERNEL_QUEUE_STATUS`.
  amdf_structure_type_t type;
  /// Must be at least `sizeof(amdf_kernel_queue_status_t)`.
  uint32_t structure_size;
  /// Optional output extension chain. No extensions are currently defined.
  void* next;
  /// Greatest accepted submission whose native storage is no longer in use.
  uint64_t retired_submission;
  /// Current queue lifecycle state.
  amdf_kernel_queue_state_t state;
  /// Reserved for future use and always zero.
  uint32_t reserved;
  /// Sticky terminal failure, or `AMDF_STATUS_OK` while active.
  amdf_status_t terminal_status;
} amdf_kernel_queue_status_t;

/// Immutable properties of one opened endpoint.
typedef struct amdf_endpoint_info_t {
  /// Must be `AMDF_STRUCTURE_TYPE_ENDPOINT_INFO`.
  amdf_structure_type_t type;
  /// Must be at least `sizeof(amdf_endpoint_info_t)`.
  uint32_t structure_size;
  /// Optional output extension chain. No extensions are defined in ABI v1.
  void* next;
  /// Opaque identity used to open this endpoint.
  amdf_endpoint_id_t id;
  /// Broad engine class, or `AMDF_ENGINE_KIND_UNKNOWN` when not yet qualified.
  amdf_engine_kind_t engine_kind;
  /// Standard endpoint properties.
  amdf_endpoint_type_flags_t type_flags;
  /// Immutable PCI identity reported by the platform.
  amdf_pci_info_t pci;
  /// NUL-terminated UTF-8 diagnostic name. Never use this for classification.
  char name[AMDF_ENDPOINT_NAME_CAPACITY];
  /// Number of immutable endpoint-local native queue families.
  uint32_t queue_family_count;
} amdf_endpoint_info_t;

/// Physical placement class requested for a memory allocation.
typedef uint32_t amdf_memory_class_t;
enum amdf_memory_class_e {
  /// No placement class. This value is never accepted by memory creation.
  AMDF_MEMORY_CLASS_UNKNOWN = 0,
  /// Provider-owned system memory accessible through a host mapping.
  AMDF_MEMORY_CLASS_SYSTEM = 1,
  /// Device-local physical memory that may not be host visible.
  AMDF_MEMORY_CLASS_LOCAL = 2,
  /// Caller-owned host memory registered with a device.
  AMDF_MEMORY_CLASS_REGISTERED_HOST = 3,
};

/// Required or achieved properties of a memory attachment.
typedef uint64_t amdf_memory_flags_t;
enum amdf_memory_flag_bits_e {
  /// The allocation can be explicitly mapped for host access.
  AMDF_MEMORY_FLAG_HOST_VISIBLE = UINT64_C(1) << 0,
  /// The physical placement is local to the attached device.
  AMDF_MEMORY_FLAG_DEVICE_LOCAL = UINT64_C(1) << 1,
  /// The physical backing can be exported and attached to another device.
  AMDF_MEMORY_FLAG_SHAREABLE = UINT64_C(1) << 2,
  /// The allocation can hold instructions executable by the device.
  AMDF_MEMORY_FLAG_EXECUTABLE = UINT64_C(1) << 3,
  /// The allocation can hold directly published user-mode queue state.
  AMDF_MEMORY_FLAG_QUEUE_STORAGE = UINT64_C(1) << 4,
  /// Host and device access requires no explicit host cache transition.
  AMDF_MEMORY_FLAG_HOST_COHERENT = UINT64_C(1) << 5,
  /// A stable device address is established before creation returns.
  AMDF_MEMORY_FLAG_DEVICE_ADDRESS = UINT64_C(1) << 6,
};

/// Parameters used to create physical backing and attach it to one device.
typedef struct amdf_memory_create_info_t {
  /// Must be `AMDF_STRUCTURE_TYPE_MEMORY_CREATE_INFO`.
  amdf_structure_type_t type;
  /// Must be at least `sizeof(amdf_memory_create_info_t)`.
  uint32_t structure_size;
  /// Optional input extension chain. No extensions are currently defined.
  const void* next;
  /// Required physical placement class.
  amdf_memory_class_t memory_class;
  /// Required properties that must all be achieved.
  amdf_memory_flags_t required_flags;
  /// Minimum usable byte length. The achieved allocation may be larger.
  uint64_t byte_length;
  /// Minimum power-of-two allocation-base alignment in every supported address
  /// space, or zero for provider policy.
  uint64_t minimum_alignment;
  /// Borrowed host base for `AMDF_MEMORY_CLASS_REGISTERED_HOST`, otherwise
  /// `NULL`. The caller keeps this address range backed by the same live pages
  /// until `memory_destroy` succeeds. Registration does not take ownership.
  void* registered_host_pointer;
} amdf_memory_create_info_t;

/// Immutable properties of one live device memory attachment.
typedef struct amdf_memory_info_t {
  /// Must be `AMDF_STRUCTURE_TYPE_MEMORY_INFO`.
  amdf_structure_type_t type;
  /// Must be at least `sizeof(amdf_memory_info_t)`.
  uint32_t structure_size;
  /// Optional output extension chain. No extensions are currently defined.
  void* next;
  /// Achieved physical placement class.
  amdf_memory_class_t memory_class;
  /// Achieved attachment properties.
  amdf_memory_flags_t flags;
  /// Physical allocation length in bytes.
  uint64_t byte_length;
  /// Guaranteed power-of-two allocation-base alignment in every supported
  /// address space.
  uint64_t alignment;
  /// Identity shared by attachments to the same physical backing, when known.
  amdf_physical_memory_id_t physical_backing_id;
  /// Stable device virtual base when `AMDF_MEMORY_FLAG_DEVICE_ADDRESS` is set.
  uint64_t device_address;
  /// Device reset epoch in which the attachment and address remain valid.
  uint64_t reset_epoch;
} amdf_memory_info_t;

/// Host access requested for one explicit mapping.
typedef uint32_t amdf_memory_map_flags_t;
enum amdf_memory_map_flag_bits_e {
  /// Host loads are permitted from the mapped range.
  AMDF_MEMORY_MAP_FLAG_READ = 1u << 0,
  /// Host stores are permitted to the mapped range.
  AMDF_MEMORY_MAP_FLAG_WRITE = 1u << 1,
};

/// Parameters used to map a range of host-visible memory.
typedef struct amdf_memory_map_info_t {
  /// Must be `AMDF_STRUCTURE_TYPE_MEMORY_MAP_INFO`.
  amdf_structure_type_t type;
  /// Must be at least `sizeof(amdf_memory_map_info_t)`.
  uint32_t structure_size;
  /// Optional input extension chain. No extensions are currently defined.
  const void* next;
  /// Byte offset into the physical allocation.
  uint64_t byte_offset;
  /// Nonzero byte length of the mapped range.
  uint64_t byte_length;
  /// Required host read and write access.
  amdf_memory_map_flags_t flags;
} amdf_memory_map_info_t;

/// Host cache behavior of a mapped allocation.
typedef uint32_t amdf_host_cacheability_t;
enum amdf_host_cacheability_e {
  /// The provider cannot describe the mapping's cache behavior.
  AMDF_HOST_CACHEABILITY_UNKNOWN = 0,
  /// Host and device accesses are mutually coherent without cache control.
  AMDF_HOST_CACHEABILITY_COHERENT = 1,
  /// Ordinary host write-back caching requiring explicit ownership transfer.
  AMDF_HOST_CACHEABILITY_WRITE_BACK = 2,
  /// Host write-combined caching intended for sequential stores.
  AMDF_HOST_CACHEABILITY_WRITE_COMBINED = 3,
  /// Uncached host access.
  AMDF_HOST_CACHEABILITY_UNCACHED = 4,
};

/// Immutable properties of one live host mapping.
typedef struct amdf_host_mapping_info_t {
  /// Must be `AMDF_STRUCTURE_TYPE_HOST_MAPPING_INFO`.
  amdf_structure_type_t type;
  /// Must be at least `sizeof(amdf_host_mapping_info_t)`.
  uint32_t structure_size;
  /// Optional output extension chain. No extensions are currently defined.
  void* next;
  /// Achieved host read and write access.
  amdf_memory_map_flags_t flags;
  /// Host cache behavior of the mapped pages.
  amdf_host_cacheability_t cacheability;
  /// First mapped byte borrowed until `host_mapping_destroy` succeeds.
  void* pointer;
  /// Mapped byte length.
  uint64_t byte_length;
  /// Host cache-line length in bytes, or zero when not applicable.
  uint32_t cache_line_size;
  /// Device reset epoch in which the mapping remains valid.
  uint64_t reset_epoch;
} amdf_host_mapping_info_t;

/// Direction of one explicit host cache ownership transition.
typedef uint32_t amdf_host_cache_operation_t;
enum amdf_host_cache_operation_e {
  /// Releases prior host writes for subsequent device reads.
  AMDF_HOST_CACHE_OPERATION_FLUSH = 1,
  /// Acquires prior device writes for subsequent host reads.
  AMDF_HOST_CACHE_OPERATION_INVALIDATE = 2,
};

/// Immutable entry-point table for one negotiated ABI version.
///
/// Tables grow only by appending fields. The table and every function pointer
/// reachable from it remain valid until the providing library is unloaded.
typedef struct amdf_api_t {
  /// Size in bytes of this table version.
  uint32_t structure_size;
  /// ABI version implemented by this table.
  amdf_abi_version_t abi_version;

  /// Creates an independent provider instance.
  ///
  /// The instance owns every dependent library reference and resolved native
  /// procedure table used by its children. Creation is thread-safe and performs
  /// bounded constant work. It performs no endpoint enumeration, device or
  /// firmware initialization, worker creation, retry, sleep, or process-global
  /// initialization. On failure, `out_instance` is set to `NULL`.
  amdf_status_t(AMDF_CALL* instance_create)(
      const amdf_instance_create_info_t* create_info,
      amdf_instance_t** out_instance);

  /// Destroys an instance after all of its children have been closed.
  ///
  /// The caller must have exclusive access. Returns
  /// `AMDF_STATUS_CODE_BUSY` without mutation while a child remains open.
  /// Destruction performs no implicit device wait.
  amdf_status_t(AMDF_CALL* instance_destroy)(amdf_instance_t* instance);

  /// Enumerates a bounded snapshot of independently selectable AMD endpoints.
  ///
  /// A zero `capacity` queries the total count and may pass `summaries` as
  /// `NULL`. A nonzero capacity requires `summaries` to reference that many
  /// elements. When capacity is insufficient, the available prefix is written,
  /// `out_count` receives the total, and `AMDF_STATUS_CODE_BUFFER_TOO_SMALL` is
  /// returned. The call creates no device, paging queue, address space,
  /// context, allocation, executable, or hardware queue. Arrival or removal may
  /// change the result of a later call.
  amdf_status_t(AMDF_CALL* endpoint_enumerate)(
      amdf_instance_t* instance, uint32_t capacity,
      amdf_endpoint_summary_t* summaries, uint32_t* out_count);

  /// Opens one endpoint identity without enumerating the machine again.
  ///
  /// The returned query-only endpoint borrows `instance`; the instance must
  /// outlive it. Opening may acquire a native query handle and cache immutable
  /// identity, but creates no schedulable device state. A stale identity fails
  /// rather than selecting another endpoint. On failure, `out_endpoint` is set
  /// to `NULL`.
  amdf_status_t(AMDF_CALL* endpoint_open)(amdf_instance_t* instance,
                                          const amdf_endpoint_id_t* id,
                                          amdf_endpoint_t** out_endpoint);

  /// Copies immutable cached properties without a system call or device wait.
  ///
  /// The operation is thread-safe. The caller initializes `out_info` and its
  /// complete extension chain before the call. No output is modified when
  /// validation fails.
  amdf_status_t(AMDF_CALL* endpoint_query_info)(amdf_endpoint_t* endpoint,
                                                amdf_endpoint_info_t* out_info);

  /// Closes a query-only endpoint after all future children are destroyed.
  ///
  /// The caller must have exclusive access. The operation performs no implicit
  /// device wait. Failure leaves the endpoint live so destruction can be
  /// retried.
  amdf_status_t(AMDF_CALL* endpoint_close)(amdf_endpoint_t* endpoint);

  /// Acquires an immutable optional API table compiled into this library.
  ///
  /// Extension availability describes the library composition and never
  /// depends on endpoint enumeration or active hardware. Hardware support is
  /// reported by operations on the returned table. `minimum_version` and
  /// `maximum_version` form an inclusive range. An unknown or excluded
  /// extension returns `AMDF_STATUS_CODE_UNSUPPORTED`; a compiled extension
  /// with no version in range returns `AMDF_STATUS_CODE_VERSION_MISMATCH`.
  /// Failure sets `out_extension_api` to `NULL` when it is non-NULL.
  ///
  /// This operation is thread-safe, bounded constant time, and inert. It
  /// performs no allocation, system call, device discovery, dependent-library
  /// load, or other observable initialization. The returned table remains
  /// valid until the providing library is unloaded.
  amdf_status_t(AMDF_CALL* query_extension)(amdf_extension_id_t extension_id,
                                            uint32_t minimum_version,
                                            uint32_t maximum_version,
                                            const void** out_extension_api);

  /// Copies one immutable queue-family record cached while opening `endpoint`.
  ///
  /// `queue_family_ordinal` must be less than the endpoint's reported family
  /// count. The operation is thread-safe and performs no system call,
  /// allocation, device initialization, queue creation, retry, sleep, or
  /// device wait. The caller initializes `out_info` and its complete extension
  /// chain. No output is modified on failure.
  amdf_status_t(AMDF_CALL* endpoint_query_queue_family_info)(
      amdf_endpoint_t* endpoint, uint32_t queue_family_ordinal,
      amdf_queue_family_info_t* out_info);

  /// Destroys a materialized device after all of its children are destroyed.
  ///
  /// The caller must have exclusive access. Returns
  /// `AMDF_STATUS_CODE_BUSY` without native mutation while a child remains
  /// live. Caller-submitted work must already be retired before its owning
  /// children are destroyed. Native teardown may wait for provider-owned
  /// initialization work retained after a failed construction. A native
  /// teardown failure leaves the device live so destruction can be retried.
  amdf_status_t(AMDF_CALL* device_destroy)(amdf_device_t* device);

  /// Creates physical backing and one stable attachment to `device`.
  ///
  /// The returned memory borrows `device`, which must outlive it. Every bit in
  /// `required_flags` is guaranteed in the copied memory info. In particular,
  /// `AMDF_MEMORY_FLAG_DEVICE_ADDRESS` means that all ordinary mapping and
  /// residency work has completed and the address is ready for any supported
  /// consumer when this cold call returns. This operation performs no queue
  /// submission, command inspection, retry, or device-wide synchronization.
  /// Registered host memory borrows the supplied pages without copying their
  /// contents. If the pointer came from a host mapping, that source mapping
  /// and its memory must outlive the registration. Independent registrations
  /// do not transfer ownership or establish execution or cache dependencies.
  /// On failure, `out_memory` is set to `NULL`.
  amdf_status_t(AMDF_CALL* memory_create)(
      amdf_device_t* device, const amdf_memory_create_info_t* create_info,
      amdf_memory_t** out_memory);

  /// Copies immutable properties cached when `memory` was created.
  ///
  /// The operation is thread-safe and performs no system call, allocation,
  /// mapping mutation, retry, sleep, or device wait. The caller initializes
  /// `out_info` and its complete extension chain. No output is modified when
  /// validation fails.
  amdf_status_t(AMDF_CALL* memory_query_info)(amdf_memory_t* memory,
                                              amdf_memory_info_t* out_info);

  /// Creates an explicit host mapping of one memory range.
  ///
  /// The returned mapping borrows `memory`, which must outlive it. Mapping does
  /// not wait for device work or transfer cache ownership. On failure,
  /// `out_mapping` is set to `NULL`.
  amdf_status_t(AMDF_CALL* memory_map)(amdf_memory_t* memory,
                                       const amdf_memory_map_info_t* map_info,
                                       amdf_host_mapping_t** out_mapping);

  /// Copies immutable properties of one live host mapping.
  ///
  /// The copied pointer is borrowed until `host_mapping_destroy` succeeds. The
  /// operation is thread-safe and performs no system call, allocation, cache
  /// transition, or device wait. No output is modified on validation failure.
  amdf_status_t(AMDF_CALL* host_mapping_query_info)(
      amdf_host_mapping_t* mapping, amdf_host_mapping_info_t* out_info);

  /// Performs one explicit host cache ownership transition over a mapped range.
  ///
  /// `byte_offset` is relative to the mapping. The implementation may touch
  /// every cache line intersecting the range; callers externally synchronize
  /// the complete intersected lines. A non-empty flush requires write access
  /// and a non-empty invalidate requires read access; otherwise the operation
  /// returns `AMDF_STATUS_CODE_FAILED_PRECONDITION`. An empty range is a no-op.
  /// This operation never waits for device execution or supplies an execution
  /// dependency.
  amdf_status_t(AMDF_CALL* host_mapping_cache_control)(
      amdf_host_mapping_t* mapping, amdf_host_cache_operation_t operation,
      uint64_t byte_offset, uint64_t byte_length);

  /// Destroys one mapping after all host access to its pointer has stopped.
  ///
  /// The caller must have exclusive access. Failure leaves the mapping live so
  /// destruction can be retried. No device wait or cache transition is implied.
  amdf_status_t(AMDF_CALL* host_mapping_destroy)(amdf_host_mapping_t* mapping);

  /// Destroys memory after all host mappings and future device uses are gone.
  ///
  /// The caller must have exclusive access. Returns `AMDF_STATUS_CODE_BUSY`
  /// without native mutation while a mapping remains live. Destruction performs
  /// no implicit device wait or cache transition. A native teardown failure
  /// leaves the memory live so destruction can be retried.
  amdf_status_t(AMDF_CALL* memory_destroy)(amdf_memory_t* memory);

  /// Copies immutable properties cached when `queue` was created.
  ///
  /// The operation is thread-safe and performs no system call, allocation,
  /// native progress query, retry, sleep, or device wait. No output is modified
  /// when validation fails.
  amdf_status_t(AMDF_CALL* kernel_queue_query_info)(
      amdf_kernel_queue_t* queue, amdf_kernel_queue_info_t* out_info);

  /// Samples retirement and observed terminal state without waiting.
  ///
  /// The operation may retire completed submissions and release their command
  /// borrows. It is thread-safe with submission and other status operations. It
  /// performs no allocation, system call, retry, sleep, or active polling. No
  /// output is modified when validation fails. ACTIVE means no terminal failure
  /// has been observed, not that a fresh native health check was performed.
  /// Rejection and timeout errors do not themselves mark a queue failed. A
  /// terminal failure remains sticky and is not itself retirement proof.
  /// Providers without a mapped completion fence report cached progress;
  /// `kernel_queue_wait`, including a zero-time wait, refreshes that progress.
  amdf_status_t(AMDF_CALL* kernel_queue_query_status)(
      amdf_kernel_queue_t* queue, amdf_kernel_queue_status_t* out_status);

  /// Waits until `submission` retires, a failure is observed, or time expires.
  ///
  /// `timeout_nanoseconds` includes host contention, active polling, and native
  /// waiting under one deadline. A zero timeout performs one nonblocking native
  /// poll when progress is not already known. `poll_duration_nanoseconds` is
  /// clipped to that timeout; zero disables active polling.
  /// `AMDF_TIMEOUT_INFINITE` requests no deadline. A
  /// timeout observes but never cancels accepted work or releases its command
  /// borrows. The operation is thread-safe with submission and status queries.
  /// A native wait error is returned even if progress concurrently advances;
  /// callers use `kernel_queue_query_status` to determine retirement and
  /// whether a terminal failure was observed before deciding to retry.
  amdf_status_t(AMDF_CALL* kernel_queue_wait)(
      amdf_kernel_queue_t* queue, uint64_t submission,
      uint64_t timeout_nanoseconds, uint64_t poll_duration_nanoseconds);

  /// Destroys one queue after every accepted submission has retired.
  ///
  /// The caller must have exclusive access. The operation samples progress once
  /// and returns `AMDF_STATUS_CODE_BUSY` without waiting while work remains. A
  /// native teardown failure leaves the queue live so destruction can be
  /// retried.
  amdf_status_t(AMDF_CALL* kernel_queue_destroy)(amdf_kernel_queue_t* queue);
} amdf_api_t;

/// Function type used to acquire the immutable API table.
typedef amdf_status_t(AMDF_CALL* amdf_query_api_fn_t)(
    amdf_abi_version_t minimum_version, amdf_abi_version_t maximum_version,
    const amdf_api_t** out_api);

/// Acquires the newest supported API table in the inclusive requested range.
///
/// On success, `out_api` receives a borrowed immutable table that remains valid
/// until the providing library is unloaded. On failure, `out_api` is set to
/// `NULL` when it is non-NULL.
///
/// This function is thread-safe, bounded constant time, and inert. It performs
/// no allocation, system call, device discovery, dependent-library load, or
/// other observable initialization.
AMDF_API amdf_status_t AMDF_CALL
amdf_query_api(amdf_abi_version_t minimum_version,
               amdf_abi_version_t maximum_version, const amdf_api_t** out_api);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AMDF_AMDF_H_
