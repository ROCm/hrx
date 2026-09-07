// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef AMDF_SRC_XDNA_UMD_KERNEL_QUEUE_H_
#define AMDF_SRC_XDNA_UMD_KERNEL_QUEUE_H_

#include <stdint.h>

#include "amdf/amdf.h"
#include "libamdf/src/xdna/umd/command.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

typedef struct amdf_xdna_umd_kernel_queue_t amdf_xdna_umd_kernel_queue_t;

// Acquires one native kernel-mediated queue from a materialized device.
amdf_status_t amdf_xdna_umd_kernel_queue_create(
    amdf_xdna_umd_device_t* device, amdf_xdna_umd_kernel_queue_t** out_queue);

// Publishes one immutable native command and returns its progress value.
amdf_status_t amdf_xdna_umd_kernel_queue_submit(
    amdf_xdna_umd_kernel_queue_t* queue, const amdf_xdna_umd_command_t* command,
    uint64_t* out_native_submission);

// Samples the native progress fence with device-to-host acquire semantics.
uint64_t amdf_xdna_umd_kernel_queue_query_progress(
    const amdf_xdna_umd_kernel_queue_t* queue);

// Waits for one native progress value with caller-selected polling.
amdf_status_t amdf_xdna_umd_kernel_queue_wait(
    amdf_xdna_umd_kernel_queue_t* queue, uint64_t native_submission,
    uint64_t timeout_nanoseconds, uint64_t poll_duration_nanoseconds);

// Releases an idle native queue lease.
amdf_status_t amdf_xdna_umd_kernel_queue_destroy(
    amdf_xdna_umd_kernel_queue_t* queue);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // AMDF_SRC_XDNA_UMD_KERNEL_QUEUE_H_
