// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef IREE_VM_BYTECODE_ASSEMBLER_H_
#define IREE_VM_BYTECODE_ASSEMBLER_H_

#include "iree/base/api.h"
#include "iree/base/byte_sequence.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Assembles one complete canonical VM module source into immutable bytes.
//
// |source| is borrowed for the duration of the call. The assembler validates
// UTF-8, syntax, physical field values, symbolic references, module structure,
// and instruction constraints before publishing output. It performs no
// lowering, inference, declaration reordering, deduplication, or branch-width
// selection: authored declaration order and instruction forms determine the
// exact encoded image.
//
// On success |out_contents| receives one immutable sequence reference that the
// caller must release with iree_byte_sequence_release. Segment boundaries are
// private storage details. On failure |out_contents| is set to NULL and no
// partial output is published.
IREE_API_EXPORT iree_status_t iree_vm_bytecode_assemble_module(
    iree_string_view_t source, iree_allocator_t host_allocator,
    iree_byte_sequence_t** out_contents);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // IREE_VM_BYTECODE_ASSEMBLER_H_
