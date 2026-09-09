// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <stddef.h>
#include <stdint.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "iree/base/api.h"
#include "iree/vm/bytecode/assembler.h"
#include "iree/vm/bytecode/disassembler.h"
#include "iree/vm/bytecode/execution_testdata.h"
#include "iree/vm/bytecode/launch_config_testdata.h"
#include "iree/vm/bytecode/verifier.h"

namespace {

constexpr size_t kMaximumInputSize = 1024 * 1024;

std::string g_canonical_sources[2];

iree_status_t AppendText(void* user_data, iree_string_view_t fragment) {
  auto* source = static_cast<std::string*>(user_data);
  source->append(fragment.data, fragment.size);
  return iree_ok_status();
}

void Fail(iree_status_t status) {
  iree_status_fprint(stderr, status);
  iree_status_free(status);
  std::abort();
}

iree_byte_span_t CloneSequence(iree_byte_sequence_t* sequence) {
  iree_byte_span_t contents = iree_byte_span_empty();
  iree_status_t status =
      iree_byte_sequence_clone(sequence, iree_allocator_system(), &contents);
  if (!iree_status_is_ok(status)) Fail(status);
  return contents;
}

void AssembleAndVerify(iree_string_view_t source) {
  iree_byte_sequence_t* sequence = nullptr;
  iree_status_t status = iree_vm_bytecode_assemble_module(
      source, iree_allocator_system(), &sequence);
  if (!iree_status_is_ok(status)) {
    // Rejection is expected for arbitrary source and is fully handled at this
    // terminal fuzz boundary.
    iree_status_free(status);
    return;
  }

  iree_byte_span_t contents = CloneSequence(sequence);
  iree_byte_sequence_release(sequence);

  iree_vm_bytecode_module_plan_t plan = {};
  status = iree_vm_bytecode_verify_module(iree_const_cast_byte_span(contents),
                                          iree_allocator_system(), &plan);
  if (!iree_status_is_ok(status)) Fail(status);

  std::string canonical_source;
  const iree_vm_bytecode_disassembler_write_callback_t callback = {
      AppendText, &canonical_source};
  status = iree_vm_bytecode_disassemble_module(
      iree_const_cast_byte_span(contents), callback, iree_allocator_system());
  if (!iree_status_is_ok(status)) Fail(status);

  iree_byte_sequence_t* roundtrip_sequence = nullptr;
  status = iree_vm_bytecode_assemble_module(
      iree_make_string_view(canonical_source.data(), canonical_source.size()),
      iree_allocator_system(), &roundtrip_sequence);
  if (!iree_status_is_ok(status)) Fail(status);
  iree_byte_span_t roundtrip_contents = CloneSequence(roundtrip_sequence);
  iree_byte_sequence_release(roundtrip_sequence);
  if (contents.data_length != roundtrip_contents.data_length ||
      std::memcmp(contents.data, roundtrip_contents.data,
                  contents.data_length) != 0) {
    std::abort();
  }

  iree_allocator_free(iree_allocator_system(), roundtrip_contents.data);
  iree_allocator_free(iree_allocator_system(), contents.data);
}

void MutateCanonicalSource(const uint8_t* data, size_t size) {
  const size_t source_ordinal = size == 0 ? 0 : data[0] & 1;
  std::string source = g_canonical_sources[source_ordinal];
  if (source.empty()) return;

  const size_t program_size =
      iree_min(size, static_cast<size_t>(source.size() * 3));
  for (size_t i = 1; i + 2 < program_size; i += 3) {
    const size_t offset = (static_cast<size_t>(data[i]) |
                           (static_cast<size_t>(data[i + 1]) << 8)) %
                          source.size();
    source[offset] ^= static_cast<char>(data[i + 2]);
  }
  AssembleAndVerify(iree_make_string_view(source.data(), source.size()));
}

}  // namespace

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv) {
  (void)argc;
  (void)argv;
  const iree_file_toc_t* sources[] = {
      iree_vm_bytecode_execution_testdata_create(),
      iree_vm_bytecode_launch_config_testdata_create(),
  };
  for (size_t i = 0; i < IREE_ARRAYSIZE(sources); ++i) {
    g_canonical_sources[i].assign(
        reinterpret_cast<const char*>(sources[i][0].data), sources[i][0].size);
  }
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size > kMaximumInputSize) return 0;
  AssembleAndVerify(
      iree_make_string_view(reinterpret_cast<const char*>(data), size));
  MutateCanonicalSource(data, size);
  return 0;
}
