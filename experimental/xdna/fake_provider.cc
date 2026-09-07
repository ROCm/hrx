// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "experimental/xdna/fake_provider.h"

#include <cstddef>
#include <utility>

namespace iree::hal::amd::xdna::testing {

static amdf_status_t AMDF_CALL FakeProgramCreate(
    amdf_device_t* device, const amdf_xdna_program_create_info_t* create_info,
    amdf_xdna_program_t** out_program) {
  auto* provider = reinterpret_cast<FakeProvider*>(device);
  *out_program = nullptr;
  ++provider->program_create_count;
  if (!amdf_status_is_ok(provider->program_create_status)) {
    return provider->program_create_status;
  }
  if (provider->return_null_program) return AMDF_STATUS_OK;

  auto* program = new FakeProgram{
      provider,
      create_info->type,
      create_info->structure_size,
      create_info->next,
      create_info->required_flags,
      create_info->footprint,
      {},
      0,
  };
  program->components.reserve(create_info->component_count);
  for (uint32_t i = 0; i < create_info->component_count; ++i) {
    const amdf_xdna_program_component_t& source = create_info->components[i];
    const auto* begin = static_cast<const uint8_t*>(source.bytes);
    program->components.push_back(FakeProgramComponent{
        source.kind, source.reserved,
        std::vector<uint8_t>(begin,
                             begin + static_cast<size_t>(source.byte_length))});
  }
  ++provider->live_program_count;
  provider->last_program = program;
  *out_program = reinterpret_cast<amdf_xdna_program_t*>(program);
  return AMDF_STATUS_OK;
}

static amdf_status_t AMDF_CALL
FakeProgramDestroy(amdf_xdna_program_t* base_program) {
  if (base_program == nullptr) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  auto* program = reinterpret_cast<FakeProgram*>(base_program);
  if (program->live_command_count != 0) {
    return amdf_make_api_status(AMDF_STATUS_CODE_BUSY);
  }
  FakeProvider* provider = program->provider;
  ++provider->program_destroy_count;
  --provider->live_program_count;
  if (provider->last_program == program) provider->last_program = nullptr;
  delete program;
  return AMDF_STATUS_OK;
}

static amdf_status_t AMDF_CALL
FakeCommandCreate(amdf_xdna_program_t* base_program,
                  const amdf_xdna_command_create_info_t* create_info,
                  amdf_xdna_command_t** out_command) {
  auto* program = reinterpret_cast<FakeProgram*>(base_program);
  FakeProvider* provider = program->provider;
  *out_command = nullptr;
  ++provider->command_create_count;
  if (!amdf_status_is_ok(provider->command_create_status)) {
    return provider->command_create_status;
  }
  if (provider->return_null_command) return AMDF_STATUS_OK;

  const auto* control_begin =
      static_cast<const uint8_t*>(create_info->control_bytes);
  std::vector<amdf_xdna_command_binding_t> bindings;
  if (create_info->binding_count != 0) {
    bindings.assign(create_info->bindings,
                    create_info->bindings + create_info->binding_count);
  }
  auto* command = new FakeCommand{
      program,
      create_info->type,
      create_info->structure_size,
      create_info->next,
      create_info->reserved,
      std::vector<uint8_t>(
          control_begin, control_begin + static_cast<size_t>(
                                             create_info->control_byte_length)),
      std::move(bindings),
  };
  ++program->live_command_count;
  ++provider->live_command_count;
  provider->last_command = command;
  *out_command = reinterpret_cast<amdf_xdna_command_t*>(command);
  return AMDF_STATUS_OK;
}

static amdf_status_t AMDF_CALL
FakeCommandDestroy(amdf_xdna_command_t* base_command) {
  if (base_command == nullptr) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  auto* command = reinterpret_cast<FakeCommand*>(base_command);
  FakeProvider* provider = command->program->provider;
  ++provider->command_destroy_count;
  if (!amdf_status_is_ok(provider->command_destroy_status)) {
    return provider->command_destroy_status;
  }
  --command->program->live_command_count;
  --provider->live_command_count;
  if (provider->last_command == command) provider->last_command = nullptr;
  delete command;
  return AMDF_STATUS_OK;
}

FakeProvider::FakeProvider() {
  api.structure_size =
      offsetof(amdf_xdna_api_t, command_destroy) + sizeof(api.command_destroy);
  api.extension_version = AMDF_XDNA_EXTENSION_VERSION_1;
  api.program_create = FakeProgramCreate;
  api.program_destroy = FakeProgramDestroy;
  api.command_create = FakeCommandCreate;
  api.command_destroy = FakeCommandDestroy;
}

}  // namespace iree::hal::amd::xdna::testing
