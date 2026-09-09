// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "iree/base/api.h"
#include "iree/base/tooling/flags.h"
#include "iree/io/file_contents.h"
#include "iree/io/stdio_stream.h"
#include "iree/io/stdio_util.h"
#include "iree/vm/bytecode/assembler.h"

IREE_FLAG(string, output, "-",
          "Output path. Use '-' or the empty string for stdout.");

static const char kVmAsUsage[] =
    "Assembles canonical VM text into a bytecode module.\n"
    "\n"
    "Usage:\n"
    "  vm-as [--output=<output.vm>] [<input.vmasm>|-]\n"
    "\n"
    "With no input path, or with '-', source text is read from stdin. Output\n"
    "defaults to stdout. Assembly is exact: declarations are not reordered,\n"
    "instructions are not selected, and no compiler lowering is performed.\n";

typedef struct vm_as_output_t {
  // Open destination stream.
  FILE* file;
  // True when |file| must be closed instead of flushed.
  bool owns_file;
  // Borrowed path used for diagnostics.
  iree_string_view_t path;
} vm_as_output_t;

static bool vm_as_output_path_is_stdout(iree_string_view_t path) {
  return iree_string_view_is_empty(path) ||
         iree_string_view_equal(path, IREE_SV("-"));
}

static iree_status_t vm_as_output_error(const vm_as_output_t* output,
                                        const char* action) {
  const int error_number = errno;
  const iree_status_code_t status_code =
      error_number == 0 ? IREE_STATUS_DATA_LOSS
                        : iree_status_code_from_errno(error_number);
  return iree_make_status(status_code, "failed to %s output '%.*s' (%d)",
                          action, (int)output->path.size, output->path.data,
                          error_number);
}

static iree_status_t vm_as_output_open(iree_string_view_t path,
                                       iree_allocator_t host_allocator,
                                       vm_as_output_t* out_output) {
  *out_output = (vm_as_output_t){.path = path};
  if (vm_as_output_path_is_stdout(path)) {
#if defined(IREE_PLATFORM_WINDOWS)
    if (IREE_IO_SET_BINARY_MODE(stdout) == -1) {
      return vm_as_output_error(out_output, "set binary mode on");
    }
#endif  // IREE_PLATFORM_WINDOWS
    out_output->file = stdout;
    return iree_ok_status();
  }
  iree_status_t status =
      iree_io_stdio_file_open(path, "wb", host_allocator, &out_output->file);
  if (iree_status_is_ok(status)) {
    out_output->owns_file = true;
  }
  return status;
}

static iree_status_t vm_as_output_close(vm_as_output_t* output) {
  if (!output->file) return iree_ok_status();
  const int result =
      output->owns_file ? fclose(output->file) : fflush(output->file);
  output->file = NULL;
  return result == 0 ? iree_ok_status() : vm_as_output_error(output, "finish");
}

static iree_status_t vm_as_write_segment(void* user_data,
                                         iree_const_byte_span_t segment) {
  vm_as_output_t* output = (vm_as_output_t*)user_data;
  if (fwrite(segment.data, 1, segment.data_length, output->file) !=
      segment.data_length) {
    return vm_as_output_error(output, "write");
  }
  return iree_ok_status();
}

static iree_status_t vm_as_read_input(iree_string_view_t path,
                                      iree_allocator_t host_allocator,
                                      iree_io_file_contents_t** out_contents) {
  if (iree_string_view_equal(path, IREE_SV("-"))) {
    return iree_io_file_contents_read_stdin(host_allocator, out_contents);
  }
  return iree_io_file_contents_map(path, IREE_IO_FILE_ACCESS_READ,
                                   host_allocator, out_contents);
}

static iree_status_t vm_as_run(iree_string_view_t input_path,
                               iree_string_view_t output_path,
                               iree_allocator_t host_allocator) {
  iree_io_file_contents_t* input_contents = NULL;
  IREE_RETURN_IF_ERROR(
      vm_as_read_input(input_path, host_allocator, &input_contents));

  const iree_string_view_t source =
      iree_make_string_view((const char*)input_contents->const_buffer.data,
                            input_contents->const_buffer.data_length);
  iree_byte_sequence_t* output_contents = NULL;
  iree_status_t status = iree_vm_bytecode_assemble_module(
      source, host_allocator, &output_contents);
  iree_io_file_contents_free(input_contents);

  vm_as_output_t output = {0};
  if (iree_status_is_ok(status)) {
    status = vm_as_output_open(output_path, host_allocator, &output);
  }
  if (iree_status_is_ok(status)) {
    const iree_byte_sequence_segment_callback_t write_callback = {
        .fn = vm_as_write_segment,
        .user_data = &output,
    };
    status = iree_byte_sequence_enumerate(output_contents, write_callback);
  }
  status = iree_status_join(status, vm_as_output_close(&output));

  iree_byte_sequence_release(output_contents);
  return status;
}

int main(int argc, char** argv) {
  IREE_TRACE_APP_ENTER();
  IREE_TRACE_ZONE_BEGIN(z0);

  iree_flags_set_usage("vm-as", kVmAsUsage);
  iree_flags_parse_checked(IREE_FLAGS_PARSE_MODE_DEFAULT, &argc, &argv);

  iree_status_t status = iree_ok_status();
  if (argc > 2) {
    status = iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                              "expected zero or one input path");
  }
  if (iree_status_is_ok(status)) {
    const iree_string_view_t input_path =
        argc == 2 ? iree_make_cstring_view(argv[1]) : IREE_SV("-");
    status = vm_as_run(input_path, iree_make_cstring_view(FLAG_output),
                       iree_allocator_system());
  }

  int exit_code = EXIT_SUCCESS;
  if (!iree_status_is_ok(status)) {
    iree_status_fprint(stderr, status);
    iree_status_free(status);
    exit_code = EXIT_FAILURE;
  }
  fflush(stderr);

  IREE_TRACE_ZONE_END(z0);
  IREE_TRACE_APP_EXIT(exit_code);
  return exit_code;
}
