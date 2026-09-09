// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef IREE_VM_BYTECODE_MODULE_FIXTURE_H_
#define IREE_VM_BYTECODE_MODULE_FIXTURE_H_

#include "iree/base/api.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Test-owned contiguous bytes assembled from one canonical module source.
typedef struct iree_vm_bytecode_module_fixture_t {
  // Allocator owning |contents|.
  iree_allocator_t host_allocator;
  // Aligned contiguous module storage consumed by bytecode runtime APIs.
  iree_byte_span_t contents;
} iree_vm_bytecode_module_fixture_t;

// Assembles canonical |source| and clones it into aligned contiguous storage.
//
// The initialized fixture owns |contents| until deinitialized. On failure
// |out_fixture| is reset and owns no storage.
iree_status_t iree_vm_bytecode_module_fixture_initialize(
    iree_string_view_t source, iree_allocator_t host_allocator,
    iree_vm_bytecode_module_fixture_t* out_fixture);

// Releases all storage owned by |fixture|.
void iree_vm_bytecode_module_fixture_deinitialize(
    iree_vm_bytecode_module_fixture_t* fixture);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // IREE_VM_BYTECODE_MODULE_FIXTURE_H_
