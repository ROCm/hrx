// Copyright 2026 The IREE Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef IREE_BUILD_TOOLS_MACOS_TESTS_SHADER_LIBRARY_H_
#define IREE_BUILD_TOOLS_MACOS_TESTS_SHADER_LIBRARY_H_

#import <Metal/Metal.h>

#ifdef __cplusplus
extern "C" {
#endif

// Compiles the host-generated embedded shader source using the device driver.
id<MTLLibrary> iree_macos_test_create_library(id<MTLDevice> device, NSError** out_error);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // IREE_BUILD_TOOLS_MACOS_TESTS_SHADER_LIBRARY_H_
