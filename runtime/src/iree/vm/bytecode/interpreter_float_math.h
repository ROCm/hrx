// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef IREE_VM_BYTECODE_INTERPRETER_FLOAT_MATH_H_
#define IREE_VM_BYTECODE_INTERPRETER_FLOAT_MATH_H_

#include "iree/base/api.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// These helpers consume and produce raw IEEE payloads. Invocation drive
// segments establish the architectural floating-point environment before
// reaching them. Selectors are verified before execution, and f32 results
// occupy only the low 32 bits of a value cell.

// Evaluates a verified float.math.f32 selector over a raw IEEE payload.
IREE_ATTRIBUTE_NOINLINE uint32_t
iree_vm_bytecode_float_math_unary_f32(uint8_t selector, uint32_t source_bits);

// Evaluates a verified float.math.f64 selector over a raw IEEE payload.
IREE_ATTRIBUTE_NOINLINE uint64_t
iree_vm_bytecode_float_math_unary_f64(uint8_t selector, uint64_t source_bits);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // IREE_VM_BYTECODE_INTERPRETER_FLOAT_MATH_H_
