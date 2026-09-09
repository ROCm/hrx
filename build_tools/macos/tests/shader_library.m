// Copyright 2026 The IREE Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#import "build_tools/macos/tests/shader_library.h"

#include "build_tools/macos/tests/shader.h"

id<MTLLibrary> iree_macos_test_create_library(id<MTLDevice> device, NSError** out_error) {
  const iree_file_toc_t* source_file = iree_macos_test_shader_create();
  NSString* source = [[NSString alloc] initWithBytes:source_file->data
                                              length:source_file->size
                                            encoding:NSUTF8StringEncoding];
  return [device newLibraryWithSource:source options:nil error:out_error];
}
