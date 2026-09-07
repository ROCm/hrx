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

/// Immutable entry-point table for one negotiated ABI version.
///
/// Tables grow only by appending fields. The table and every function pointer
/// reachable from it remain valid until the providing library is unloaded.
typedef struct amdf_api_t {
  /// Size in bytes of this table version.
  uint32_t structure_size;
  /// ABI version implemented by this table.
  amdf_abi_version_t abi_version;
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
