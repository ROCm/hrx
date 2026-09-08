// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/platform/linux/release_list.h"

#include <stddef.h>

void amdf_linux_release_list_push(amdf_linux_release_list_t* list,
                                  amdf_linux_release_t* release) {
  amdf_linux_release_t* head = __atomic_load_n(&list->head, __ATOMIC_RELAXED);
  do {
    release->next = head;
  } while (!__atomic_compare_exchange_n(&list->head, &head, release, 0,
                                        __ATOMIC_RELEASE, __ATOMIC_RELAXED));
}

amdf_status_t amdf_linux_release_list_drain(amdf_linux_release_list_t* list) {
  amdf_status_t status = AMDF_STATUS_OK;
  amdf_linux_release_t* release =
      __atomic_exchange_n(&list->head, NULL, __ATOMIC_ACQUIRE);
  while (release != NULL && amdf_status_is_ok(status)) {
    amdf_linux_release_t* next = release->next;
    status = release->destroy(release);
    if (amdf_status_is_ok(status)) release = next;
  }
  while (release != NULL) {
    amdf_linux_release_t* next = release->next;
    amdf_linux_release_list_push(list, release);
    release = next;
  }
  return status;
}
