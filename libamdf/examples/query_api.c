// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <stdio.h>

#include "amdf/amdf.h"

int main(void) {
  const amdf_api_t* api = NULL;
  const amdf_status_t status =
      amdf_query_api(AMDF_ABI_VERSION_1, AMDF_ABI_VERSION_LATEST, &api);
  if (!amdf_status_is_ok(status)) {
    fprintf(stderr, "amdf_query_api failed: domain=%u code=%u\n",
            amdf_status_domain(status), amdf_status_code(status));
    return 1;
  }
  printf("libamdf ABI %u (table size %u)\n", api->abi_version,
         api->structure_size);
  return 0;
}
