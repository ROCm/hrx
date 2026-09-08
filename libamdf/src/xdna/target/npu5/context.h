// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef AMDF_SRC_XDNA_TARGET_NPU5_CONTEXT_H_
#define AMDF_SRC_XDNA_TARGET_NPU5_CONTEXT_H_

// NPU5 firmware maps its transaction-interpreter heap in 64 MiB units. Both
// the mapped host base and heap extent must have this alignment.
#define AMDF_XDNA_NPU5_HEAP_BYTE_LENGTH (64u * 1024u * 1024u)

// Core tiles occupy rows two through five of the six-row physical array.
#define AMDF_XDNA_NPU5_CORE_ROW_ORIGIN 2u
#define AMDF_XDNA_NPU5_CORE_ROW_COUNT 4u

#endif  // AMDF_SRC_XDNA_TARGET_NPU5_CONTEXT_H_
