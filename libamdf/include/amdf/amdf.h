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
} amdf_endpoint_info_t;

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
