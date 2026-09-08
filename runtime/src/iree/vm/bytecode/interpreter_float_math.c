// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/vm/bytecode/interpreter_float_math.h"

#include <math.h>
#include <string.h>

#include "iree/math/exponential.h"
#include "iree/math/roots.h"
#include "iree/math/trigonometry.h"
#include "iree/vm/bytecode/wire/core.h"

static float iree_vm_bytecode_float_math_f32_from_bits(uint32_t bits) {
  float value = 0.0f;
  memcpy(&value, &bits, sizeof(value));
  return value;
}

static uint32_t iree_vm_bytecode_float_math_f32_to_bits(float value) {
  uint32_t bits = 0;
  memcpy(&bits, &value, sizeof(bits));
  return bits;
}

static double iree_vm_bytecode_float_math_f64_from_bits(uint64_t bits) {
  double value = 0.0;
  memcpy(&value, &bits, sizeof(value));
  return value;
}

static uint64_t iree_vm_bytecode_float_math_f64_to_bits(double value) {
  uint64_t bits = 0;
  memcpy(&bits, &value, sizeof(bits));
  return bits;
}

static uint32_t iree_vm_bytecode_float_math_sign_f32(uint32_t source_bits) {
  const uint32_t magnitude = source_bits & UINT32_C(0x7FFFFFFF);
  if (magnitude == 0 || magnitude > UINT32_C(0x7F800000)) return 0;
  return (source_bits & UINT32_C(0x80000000)) ? UINT32_C(0xBF800000)
                                              : UINT32_C(0x3F800000);
}

static uint64_t iree_vm_bytecode_float_math_sign_f64(uint64_t source_bits) {
  const uint64_t magnitude = source_bits & UINT64_C(0x7FFFFFFFFFFFFFFF);
  if (magnitude == 0 || magnitude > UINT64_C(0x7FF0000000000000)) return 0;
  return (source_bits & UINT64_C(0x8000000000000000))
             ? UINT64_C(0xBFF0000000000000)
             : UINT64_C(0x3FF0000000000000);
}

IREE_ATTRIBUTE_NOINLINE uint32_t
iree_vm_bytecode_float_math_unary_f32(uint8_t selector, uint32_t source_bits) {
  if (selector == IREE_VM_BYTECODE_FLOAT_MATH_F32_SIGN) {
    return iree_vm_bytecode_float_math_sign_f32(source_bits);
  }
  const float source = iree_vm_bytecode_float_math_f32_from_bits(source_bits);
  float result = 0.0f;
  switch (selector) {
    case IREE_VM_BYTECODE_FLOAT_MATH_F32_CEIL:
      result = ceilf(source);
      break;
    case IREE_VM_BYTECODE_FLOAT_MATH_F32_FLOOR:
      result = floorf(source);
      break;
    case IREE_VM_BYTECODE_FLOAT_MATH_F32_ROUND_EVEN:
      result = nearbyintf(source);
      break;
    case IREE_VM_BYTECODE_FLOAT_MATH_F32_TRUNC:
      result = truncf(source);
      break;
    case IREE_VM_BYTECODE_FLOAT_MATH_F32_EXP2_APPROX:
      result = iree_math_exp2_f32_approx(source);
      break;
    case IREE_VM_BYTECODE_FLOAT_MATH_F32_LOG2_APPROX:
      result = iree_math_log2_f32_approx(source);
      break;
    case IREE_VM_BYTECODE_FLOAT_MATH_F32_RECIPROCAL_APPROX:
      result = iree_math_reciprocal_f32_approx(source);
      break;
    case IREE_VM_BYTECODE_FLOAT_MATH_F32_RSQRT_APPROX:
      result = iree_math_rsqrt_f32_approx(source);
      break;
    case IREE_VM_BYTECODE_FLOAT_MATH_F32_SQRT_APPROX:
      result = iree_math_sqrt_f32_approx(source);
      break;
    case IREE_VM_BYTECODE_FLOAT_MATH_F32_SIN_TURNS_APPROX:
      result = iree_math_sin_turns_f32_approx(source);
      break;
    case IREE_VM_BYTECODE_FLOAT_MATH_F32_COS_TURNS_APPROX:
      result = iree_math_cos_turns_f32_approx(source);
      break;
    default:
      IREE_ASSERT_UNREACHABLE("float.math.f32 selector was not verified");
      return 0;
  }
  return iree_vm_bytecode_float_math_f32_to_bits(result);
}

IREE_ATTRIBUTE_NOINLINE uint64_t
iree_vm_bytecode_float_math_unary_f64(uint8_t selector, uint64_t source_bits) {
  if (selector == IREE_VM_BYTECODE_FLOAT_MATH_F64_SIGN) {
    return iree_vm_bytecode_float_math_sign_f64(source_bits);
  }
  const double source = iree_vm_bytecode_float_math_f64_from_bits(source_bits);
  double result = 0.0;
  switch (selector) {
    case IREE_VM_BYTECODE_FLOAT_MATH_F64_CEIL:
      result = ceil(source);
      break;
    case IREE_VM_BYTECODE_FLOAT_MATH_F64_FLOOR:
      result = floor(source);
      break;
    case IREE_VM_BYTECODE_FLOAT_MATH_F64_ROUND_EVEN:
      result = nearbyint(source);
      break;
    case IREE_VM_BYTECODE_FLOAT_MATH_F64_TRUNC:
      result = trunc(source);
      break;
    default:
      IREE_ASSERT_UNREACHABLE("float.math.f64 selector was not verified");
      return 0;
  }
  return iree_vm_bytecode_float_math_f64_to_bits(result);
}
