// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef IREE_BUILD_TOOLS_BAZEL_TEST_EXECUTABLE_FIXTURE_LIBRARY_H_
#define IREE_BUILD_TOOLS_BAZEL_TEST_EXECUTABLE_FIXTURE_LIBRARY_H_

// Returns the value used to verify the fixture's implicit shared-library
// import.
#if defined(_WIN32) && defined(IREE_EXECUTABLE_FIXTURE_EXPORTS)
__declspec(dllexport)
#elif defined(_WIN32)
__declspec(dllimport)
#endif
int executable_fixture_value();

#endif  // IREE_BUILD_TOOLS_BAZEL_TEST_EXECUTABLE_FIXTURE_LIBRARY_H_
