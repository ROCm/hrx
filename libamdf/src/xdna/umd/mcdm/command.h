// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef AMDF_SRC_XDNA_UMD_MCDM_COMMAND_H_
#define AMDF_SRC_XDNA_UMD_MCDM_COMMAND_H_

#include "libamdf/src/xdna/umd/command.h"
#include "libamdf/src/xdna/umd/mcdm/kernel_execution.h"

struct amdf_xdna_umd_command_t {
  // Immutable native command record and device-owned slot lease.
  amdf_windows_xdna_kernel_command_t native;
};

#endif  // AMDF_SRC_XDNA_UMD_MCDM_COMMAND_H_
