// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef LOOM_CODEGEN_LOW_SCHEDULE_FIXED_STORAGE_H_
#define LOOM_CODEGEN_LOW_SCHEDULE_FIXED_STORAGE_H_

#include "loom/codegen/low/schedule/context.h"

#ifdef __cplusplus
extern "C" {
#endif

// Adds hard dependencies for explicit fixed-location reuse before policy
// scoring. Each fixed definition and every reader precede the next definition
// sharing its atomic storage. Entry values precede the block's first writer.
// Scratch is proportional to local values and explicitly bound storage units,
// not the largest physical ID or the product of instructions and registers.
iree_status_t loom_low_schedule_build_fixed_storage_dependencies(
    loom_low_schedule_build_state_t* state);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // LOOM_CODEGEN_LOW_SCHEDULE_FIXED_STORAGE_H_
