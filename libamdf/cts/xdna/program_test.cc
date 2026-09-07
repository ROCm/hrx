// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <array>
#include <cstdint>

#include "amdf/amdf.h"
#include "amdf/xdna.h"
#include "gtest/gtest.h"
#include "xdna_device_fixture.h"

namespace {

std::array<uint8_t, 20> MakeNoOpTransaction() {
  return {
      0,  1, 4, 6, 1, 1, 0, 0,  // Version and AIE2P context geometry.
      1,  0, 0, 0,              // One operation.
      20, 0, 0, 0,              // Complete transaction byte length.
      5,  0, 0, 0,              // XAIE_IO_NOOP.
  };
}

class XdnaProgramTest : public XdnaDeviceFixture {
 protected:
  void TearDown() override {
    if (command_ != nullptr) {
      EXPECT_TRUE(amdf_status_is_ok(xdna_api_->command_destroy(command_)));
    }
    if (memory_ != nullptr) {
      EXPECT_TRUE(amdf_status_is_ok(api_->memory_destroy(memory_)));
    }
    if (program_ != nullptr) {
      EXPECT_TRUE(amdf_status_is_ok(xdna_api_->program_destroy(program_)));
    }
    XdnaDeviceFixture::TearDown();
  }

  amdf_xdna_program_create_info_t MakeProgramCreateInfo(const void* bytes,
                                                        uint64_t byte_length) {
    component_ = {};
    component_.kind = AMDF_XDNA_PROGRAM_COMPONENT_ARRAY_CONFIGURATION;
    component_.bytes = bytes;
    component_.byte_length = byte_length;

    amdf_xdna_program_create_info_t create_info = {};
    create_info.type = AMDF_STRUCTURE_TYPE_XDNA_PROGRAM_CREATE_INFO;
    create_info.structure_size = sizeof(create_info);
    create_info.component_count = 1;
    create_info.components = &component_;
    create_info.footprint.coordinate_mode =
        AMDF_XDNA_COORDINATE_MODE_CONTEXT_RELATIVE;
    create_info.footprint.column_count = 1;
    create_info.footprint.row_count = 6;
    return create_info;
  }

  amdf_memory_create_info_t MakeMemoryCreateInfo() {
    amdf_memory_create_info_t create_info = {};
    create_info.type = AMDF_STRUCTURE_TYPE_MEMORY_CREATE_INFO;
    create_info.structure_size = sizeof(create_info);
    create_info.memory_class = AMDF_MEMORY_CLASS_SYSTEM;
    create_info.required_flags =
        AMDF_MEMORY_FLAG_HOST_VISIBLE | AMDF_MEMORY_FLAG_DEVICE_ADDRESS;
    create_info.byte_length = 4096;
    create_info.minimum_alignment = 4096;
    return create_info;
  }

  amdf_xdna_program_component_t component_ = {};
  amdf_xdna_program_t* program_ = nullptr;
  amdf_memory_t* memory_ = nullptr;
  amdf_xdna_command_t* command_ = nullptr;
};

TEST_F(XdnaProgramTest, OwnsFixedProgramAndCommandInputs) {
  std::array<uint8_t, 20> program_bytes = MakeNoOpTransaction();
  const amdf_xdna_program_create_info_t program_create_info =
      MakeProgramCreateInfo(program_bytes.data(), program_bytes.size());
  ASSERT_TRUE(amdf_status_is_ok(
      xdna_api_->program_create(device_, &program_create_info, &program_)));
  ASSERT_NE(program_, nullptr);

  amdf_xdna_program_info_t program_info = {};
  program_info.type = AMDF_STRUCTURE_TYPE_XDNA_PROGRAM_INFO;
  program_info.structure_size = sizeof(program_info);
  ASSERT_TRUE(amdf_status_is_ok(
      xdna_api_->program_query_info(program_, &program_info)));
  EXPECT_EQ(program_info.flags, 0u);
  EXPECT_EQ(program_info.component_count, 1u);
  EXPECT_EQ(program_info.total_byte_length, program_bytes.size());
  EXPECT_EQ(program_info.reset_epoch, 1u);
  EXPECT_EQ(program_info.footprint.coordinate_mode,
            AMDF_XDNA_COORDINATE_MODE_CONTEXT_RELATIVE);
  EXPECT_EQ(program_info.footprint.column_count, 1u);
  EXPECT_EQ(program_info.footprint.row_count, 6u);

  program_bytes.fill(0xFF);

  const amdf_memory_create_info_t memory_create_info = MakeMemoryCreateInfo();
  ASSERT_TRUE(amdf_status_is_ok(
      api_->memory_create(device_, &memory_create_info, &memory_)));
  ASSERT_NE(memory_, nullptr);

  amdf_xdna_command_binding_t binding = {};
  binding.memory = memory_;
  binding.byte_offset = 64;
  binding.byte_length = 128;
  const std::array<uint8_t, 20> control_bytes = MakeNoOpTransaction();
  amdf_xdna_command_create_info_t command_create_info = {};
  command_create_info.type = AMDF_STRUCTURE_TYPE_XDNA_COMMAND_CREATE_INFO;
  command_create_info.structure_size = sizeof(command_create_info);
  command_create_info.binding_count = 1;
  command_create_info.control_bytes = control_bytes.data();
  command_create_info.control_byte_length = control_bytes.size();
  command_create_info.bindings = &binding;
  ASSERT_TRUE(amdf_status_is_ok(
      xdna_api_->command_create(program_, &command_create_info, &command_)));
  ASSERT_NE(command_, nullptr);

  amdf_xdna_command_info_t command_info = {};
  command_info.type = AMDF_STRUCTURE_TYPE_XDNA_COMMAND_INFO;
  command_info.structure_size = sizeof(command_info);
  ASSERT_TRUE(amdf_status_is_ok(
      xdna_api_->command_query_info(command_, &command_info)));
  EXPECT_EQ(command_info.binding_count, 1u);
  EXPECT_EQ(command_info.control_byte_length, control_bytes.size());
  EXPECT_EQ(command_info.reset_epoch, program_info.reset_epoch);

  EXPECT_EQ(amdf_status_code(xdna_api_->program_destroy(program_)),
            AMDF_STATUS_CODE_BUSY);
  EXPECT_EQ(amdf_status_code(api_->memory_destroy(memory_)),
            AMDF_STATUS_CODE_BUSY);
  EXPECT_EQ(amdf_status_code(api_->device_destroy(device_)),
            AMDF_STATUS_CODE_BUSY);

  ASSERT_TRUE(amdf_status_is_ok(xdna_api_->command_destroy(command_)));
  command_ = nullptr;
  ASSERT_TRUE(amdf_status_is_ok(api_->memory_destroy(memory_)));
  memory_ = nullptr;
  ASSERT_TRUE(amdf_status_is_ok(xdna_api_->program_destroy(program_)));
  program_ = nullptr;
}

TEST_F(XdnaProgramTest, RejectsInvalidProgramWithoutPublication) {
  std::array<uint8_t, 20> transaction = MakeNoOpTransaction();
  amdf_xdna_program_create_info_t create_info =
      MakeProgramCreateInfo(transaction.data(), transaction.size());
  amdf_xdna_program_t* output =
      reinterpret_cast<amdf_xdna_program_t*>(uintptr_t{1});

  EXPECT_EQ(amdf_status_code(
                xdna_api_->program_create(nullptr, &create_info, &output)),
            AMDF_STATUS_CODE_INVALID_ARGUMENT);
  EXPECT_EQ(output, nullptr);

  transaction[2] = 3;
  EXPECT_EQ(amdf_status_code(
                xdna_api_->program_create(device_, &create_info, &output)),
            AMDF_STATUS_CODE_INVALID_ARGUMENT);
  EXPECT_EQ(output, nullptr);
  transaction[2] = 4;

  transaction[4] = 8;
  EXPECT_EQ(amdf_status_code(
                xdna_api_->program_create(device_, &create_info, &output)),
            AMDF_STATUS_CODE_INVALID_ARGUMENT);
  EXPECT_EQ(output, nullptr);
  transaction[4] = 1;

  component_.kind = AMDF_XDNA_PROGRAM_COMPONENT_PDI;
  EXPECT_EQ(amdf_status_code(
                xdna_api_->program_create(device_, &create_info, &output)),
            AMDF_STATUS_CODE_UNSUPPORTED);
  EXPECT_EQ(output, nullptr);
  component_.kind = AMDF_XDNA_PROGRAM_COMPONENT_ARRAY_CONFIGURATION;

  create_info.footprint.column_count = 0;
  EXPECT_EQ(amdf_status_code(
                xdna_api_->program_create(device_, &create_info, &output)),
            AMDF_STATUS_CODE_OUT_OF_RANGE);
  EXPECT_EQ(output, nullptr);

  create_info = MakeProgramCreateInfo(transaction.data(), transaction.size());
  create_info.required_flags = AMDF_XDNA_PROGRAM_FLAG_RESIDENT;
  EXPECT_EQ(amdf_status_code(
                xdna_api_->program_create(device_, &create_info, &output)),
            AMDF_STATUS_CODE_UNSUPPORTED);
  EXPECT_EQ(output, nullptr);
}

TEST_F(XdnaProgramTest, RejectsInvalidCommandWithoutPublication) {
  const std::array<uint8_t, 20> transaction = MakeNoOpTransaction();
  const amdf_xdna_program_create_info_t program_create_info =
      MakeProgramCreateInfo(transaction.data(), transaction.size());
  ASSERT_TRUE(amdf_status_is_ok(
      xdna_api_->program_create(device_, &program_create_info, &program_)));

  amdf_xdna_command_create_info_t create_info = {};
  create_info.type = AMDF_STRUCTURE_TYPE_XDNA_COMMAND_CREATE_INFO;
  create_info.structure_size = sizeof(create_info);
  create_info.control_bytes = transaction.data();
  create_info.control_byte_length = transaction.size();
  amdf_xdna_command_t* output =
      reinterpret_cast<amdf_xdna_command_t*>(uintptr_t{1});

  std::array<uint8_t, 20> malformed_transaction = transaction;
  malformed_transaction[12] = 16;
  create_info.control_bytes = malformed_transaction.data();
  EXPECT_EQ(amdf_status_code(
                xdna_api_->command_create(program_, &create_info, &output)),
            AMDF_STATUS_CODE_INVALID_ARGUMENT);
  EXPECT_EQ(output, nullptr);

  create_info.control_bytes = transaction.data();
  create_info.binding_count = 1;
  EXPECT_EQ(amdf_status_code(
                xdna_api_->command_create(program_, &create_info, &output)),
            AMDF_STATUS_CODE_INVALID_ARGUMENT);
  EXPECT_EQ(output, nullptr);

  const amdf_memory_create_info_t memory_create_info = MakeMemoryCreateInfo();
  ASSERT_TRUE(amdf_status_is_ok(
      api_->memory_create(device_, &memory_create_info, &memory_)));
  amdf_memory_info_t memory_info = {};
  memory_info.type = AMDF_STRUCTURE_TYPE_MEMORY_INFO;
  memory_info.structure_size = sizeof(memory_info);
  ASSERT_TRUE(
      amdf_status_is_ok(api_->memory_query_info(memory_, &memory_info)));
  amdf_xdna_command_binding_t binding = {};
  binding.memory = memory_;
  binding.byte_offset = memory_info.byte_length;
  binding.byte_length = 1;
  create_info.bindings = &binding;
  EXPECT_EQ(amdf_status_code(
                xdna_api_->command_create(program_, &create_info, &output)),
            AMDF_STATUS_CODE_INVALID_ARGUMENT);
  EXPECT_EQ(output, nullptr);
}

}  // namespace
