// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <cstdlib>

#include "gtest/gtest.h"
#include "util/provider.h"

int main(int argument_count, char** argument_values) {
  if (!amdf_cts_provider_initialize(&argument_count, &argument_values)) {
    return EXIT_FAILURE;
  }
  testing::InitGoogleTest(&argument_count, argument_values);
  const int result = RUN_ALL_TESTS();
  const int deinitialize_succeeded = amdf_cts_provider_deinitialize();
  return result == EXIT_SUCCESS && deinitialize_succeeded ? EXIT_SUCCESS
                                                          : EXIT_FAILURE;
}
