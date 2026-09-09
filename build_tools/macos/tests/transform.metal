// Copyright 2026 The IREE Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <metal_stdlib>

kernel void transform(device const uint* input [[buffer(0)]],
                      device uint* output [[buffer(1)]],
                      uint i [[thread_position_in_grid]]) {
  output[i] = input[i] * 3 + 7;
}
