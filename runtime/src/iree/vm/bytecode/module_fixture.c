// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/vm/bytecode/module_fixture.h"

#include "iree/vm/bytecode/assembler.h"

iree_status_t iree_vm_bytecode_module_fixture_initialize(
    iree_string_view_t source, iree_allocator_t host_allocator,
    iree_vm_bytecode_module_fixture_t* out_fixture) {
  *out_fixture = (iree_vm_bytecode_module_fixture_t){0};
  iree_byte_sequence_t* sequence = NULL;
  IREE_RETURN_IF_ERROR(
      iree_vm_bytecode_assemble_module(source, host_allocator, &sequence));
  iree_byte_span_t contents = iree_byte_span_empty();
  iree_status_t status =
      iree_byte_sequence_clone(sequence, host_allocator, &contents);
  iree_byte_sequence_release(sequence);
  if (iree_status_is_ok(status)) {
    *out_fixture =
        (iree_vm_bytecode_module_fixture_t){host_allocator, contents};
  }
  return status;
}

void iree_vm_bytecode_module_fixture_deinitialize(
    iree_vm_bytecode_module_fixture_t* fixture) {
  iree_allocator_free(fixture->host_allocator, fixture->contents.data);
  *fixture = (iree_vm_bytecode_module_fixture_t){0};
}
