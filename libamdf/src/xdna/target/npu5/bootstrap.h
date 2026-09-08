// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef AMDF_SRC_XDNA_TARGET_NPU5_BOOTSTRAP_H_
#define AMDF_SRC_XDNA_TARGET_NPU5_BOOTSTRAP_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Returns the immutable provider-owned PDI initializing the NPU5 transaction
// interpreter. The caller retains any native copy through context destruction.
// This payload is independent of the operating system's admission envelope.
void amdf_xdna_npu5_bootstrap_query_pdi(const void** out_data,
                                        size_t* out_data_size);

// Returns a NOOP transaction used to admit the interpreter before user work.
// The transaction spans the physical array without touching tile state.
void amdf_xdna_npu5_bootstrap_query_transaction(const void** out_data,
                                                size_t* out_data_size);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AMDF_SRC_XDNA_TARGET_NPU5_BOOTSTRAP_H_
