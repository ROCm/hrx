// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef IREE_HAL_REPLAY_EXECUTE_VMM_H_
#define IREE_HAL_REPLAY_EXECUTE_VMM_H_

#include "iree/hal/replay/execute_state.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Executes a successful virtual-memory lifecycle operation record.
iree_status_t iree_hal_replay_executor_replay_vmm_operation(
    iree_hal_replay_executor_t* executor,
    const iree_hal_replay_file_record_t* record);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // IREE_HAL_REPLAY_EXECUTE_VMM_H_
