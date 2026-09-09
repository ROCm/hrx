// Copyright 2026 The IREE Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#include "build_tools/macos/tests/shader.h"

int main(void) {
  const iree_file_toc_t* source = iree_macos_test_shader_create();
  if (!source->data || !source->size) {
    fprintf(stderr, "The shared library returned no embedded shader data\n");
    return 1;
  }
  Dl_info library_info = {0};
  if (!dladdr((const void*)iree_macos_test_shader_create, &library_info)) {
    fprintf(stderr,
            "Could not find the loaded image containing the data library\n");
    return 1;
  }
  const char* extension = strrchr(library_info.dli_fname, '.');
  if (!extension || strcmp(extension, ".dylib") != 0) {
    fprintf(stderr, "Expected a shared data library, found %s\n",
            library_info.dli_fname);
    return 1;
  }
  return 0;
}
