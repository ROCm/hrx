// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Stateful fake libamdf XDNA provider for HAL unit tests.

#ifndef IREE_EXPERIMENTAL_XDNA_FAKE_PROVIDER_H_
#define IREE_EXPERIMENTAL_XDNA_FAKE_PROVIDER_H_

#include <cstdint>
#include <vector>

#include "amdf/xdna.h"

namespace iree::hal::amd::xdna::testing {

struct FakeProvider;

// One program component copied from a program creation request.
struct FakeProgramComponent {
  // Semantic role of the copied component.
  amdf_xdna_program_component_kind_t kind;
  // Reserved field received from the runtime.
  uint32_t reserved;
  // Complete copied component bytes.
  std::vector<uint8_t> bytes;
};

// One live program created by FakeProvider.
struct FakeProgram {
  // Provider owning this program.
  FakeProvider* provider;
  // Extensible structure type received from the runtime.
  amdf_structure_type_t create_info_type;
  // Extensible structure size received from the runtime.
  uint32_t create_info_structure_size;
  // Extension chain received from the runtime.
  const void* create_info_next;
  // Required program behavior received from the runtime.
  amdf_xdna_program_flags_t required_flags;
  // Declared context-relative program footprint.
  amdf_xdna_program_footprint_t footprint;
  // Copied target-native components.
  std::vector<FakeProgramComponent> components;
  // Number of live commands borrowing this program.
  uint32_t live_command_count;
};

// One live command created by FakeProvider.
struct FakeCommand {
  // Program borrowed by this command.
  FakeProgram* program;
  // Extensible structure type received from the runtime.
  amdf_structure_type_t create_info_type;
  // Extensible structure size received from the runtime.
  uint32_t create_info_structure_size;
  // Extension chain received from the runtime.
  const void* create_info_next;
  // Reserved field received from the runtime.
  uint32_t reserved;
  // Copied invocation-control bytes.
  std::vector<uint8_t> control_bytes;
  // Copied fixed memory bindings.
  std::vector<amdf_xdna_command_binding_t> bindings;
};

// Stateful dependency fake implementing program and command ownership.
struct FakeProvider {
  FakeProvider();

  // Returns this provider as the opaque fake device accepted by callbacks.
  amdf_device_t* device() { return reinterpret_cast<amdf_device_t*>(this); }

  // Returns the immutable XDNA API table implemented by this provider.
  const amdf_xdna_api_t* xdna_api() const { return &api; }

  // API table routed to this provider through the opaque device.
  amdf_xdna_api_t api = {};
  // Status returned by the next program creation call.
  amdf_status_t program_create_status = AMDF_STATUS_OK;
  // Status returned by the next command creation call.
  amdf_status_t command_create_status = AMDF_STATUS_OK;
  // Status returned by command destruction without mutating the command.
  amdf_status_t command_destroy_status = AMDF_STATUS_OK;
  // Whether successful program creation deliberately returns NULL.
  bool return_null_program = false;
  // Whether successful command creation deliberately returns NULL.
  bool return_null_command = false;
  // Number of program creation calls.
  uint32_t program_create_count = 0;
  // Number of program destruction calls.
  uint32_t program_destroy_count = 0;
  // Number of command creation calls.
  uint32_t command_create_count = 0;
  // Number of command destruction calls.
  uint32_t command_destroy_count = 0;
  // Number of currently live programs.
  uint32_t live_program_count = 0;
  // Number of currently live commands.
  uint32_t live_command_count = 0;
  // Most recently created live program, when any.
  FakeProgram* last_program = nullptr;
  // Most recently created live command, when any.
  FakeCommand* last_command = nullptr;
};

}  // namespace iree::hal::amd::xdna::testing

#endif  // IREE_EXPERIMENTAL_XDNA_FAKE_PROVIDER_H_
