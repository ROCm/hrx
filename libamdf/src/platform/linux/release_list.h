// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef AMDF_SRC_PLATFORM_LINUX_RELEASE_LIST_H_
#define AMDF_SRC_PLATFORM_LINUX_RELEASE_LIST_H_

#include "amdf/amdf.h"

typedef struct amdf_linux_release_t amdf_linux_release_t;

// Embedded ownership record for construction whose native rollback failed.
struct amdf_linux_release_t {
  // Next record owned by the same parent, accessed only by the list functions.
  amdf_linux_release_t* next;
  // Releases and frees the containing object on success; retains it on failure.
  amdf_status_t (*destroy)(amdf_linux_release_t* release);
};

// Parent-owned failed constructions. Zero initialization creates an empty list.
typedef struct amdf_linux_release_list_t {
  // Atomic intrusive stack; each record is added exactly once per ownership
  // turn.
  amdf_linux_release_t* head;
} amdf_linux_release_list_t;

#ifdef __cplusplus
extern "C" {
#endif

// Transfers an unpublished child to its still-live parent without allocation.
void amdf_linux_release_list_push(amdf_linux_release_list_t* list,
                                  amdf_linux_release_t* release);

// Releases children during externally synchronized parent teardown. A failure
// preserves the failed child and every remaining record for another attempt.
amdf_status_t amdf_linux_release_list_drain(amdf_linux_release_list_t* list);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AMDF_SRC_PLATFORM_LINUX_RELEASE_LIST_H_
