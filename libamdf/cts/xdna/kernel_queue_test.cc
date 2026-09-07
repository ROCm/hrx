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

class XdnaKernelQueueTest : public XdnaDeviceFixture {
 protected:
  void TearDown() override {
    if (queue_ != nullptr) {
      EXPECT_TRUE(amdf_status_is_ok(api_->kernel_queue_destroy(queue_)));
    }
    if (command_ != nullptr) {
      EXPECT_TRUE(amdf_status_is_ok(xdna_api_->command_destroy(command_)));
    }
    if (program_ != nullptr) {
      EXPECT_TRUE(amdf_status_is_ok(xdna_api_->program_destroy(program_)));
    }
    XdnaDeviceFixture::TearDown();
  }

  void CreateNoOpCommand() {
    const std::array<uint8_t, 20> transaction = MakeNoOpTransaction();
    amdf_xdna_program_component_t component = {};
    component.kind = AMDF_XDNA_PROGRAM_COMPONENT_ARRAY_CONFIGURATION;
    component.bytes = transaction.data();
    component.byte_length = transaction.size();
    amdf_xdna_program_create_info_t program_create_info = {};
    program_create_info.type = AMDF_STRUCTURE_TYPE_XDNA_PROGRAM_CREATE_INFO;
    program_create_info.structure_size = sizeof(program_create_info);
    program_create_info.component_count = 1;
    program_create_info.components = &component;
    program_create_info.footprint.coordinate_mode =
        AMDF_XDNA_COORDINATE_MODE_CONTEXT_RELATIVE;
    program_create_info.footprint.column_count = 1;
    program_create_info.footprint.row_count = 6;
    ASSERT_TRUE(amdf_status_is_ok(
        xdna_api_->program_create(device_, &program_create_info, &program_)));

    amdf_xdna_command_create_info_t command_create_info = {};
    command_create_info.type = AMDF_STRUCTURE_TYPE_XDNA_COMMAND_CREATE_INFO;
    command_create_info.structure_size = sizeof(command_create_info);
    command_create_info.control_bytes = transaction.data();
    command_create_info.control_byte_length = transaction.size();
    ASSERT_TRUE(amdf_status_is_ok(
        xdna_api_->command_create(program_, &command_create_info, &command_)));
  }

  uint32_t QueryKernelQueueFamily() {
    amdf_endpoint_info_t endpoint_info = {};
    endpoint_info.type = AMDF_STRUCTURE_TYPE_ENDPOINT_INFO;
    endpoint_info.structure_size = sizeof(endpoint_info);
    EXPECT_TRUE(amdf_status_is_ok(
        api_->endpoint_query_info(endpoint_, &endpoint_info)));
    for (uint32_t ordinal = 0; ordinal < endpoint_info.queue_family_count;
         ++ordinal) {
      amdf_queue_family_info_t family_info = {};
      family_info.type = AMDF_STRUCTURE_TYPE_QUEUE_FAMILY_INFO;
      family_info.structure_size = sizeof(family_info);
      EXPECT_TRUE(amdf_status_is_ok(api_->endpoint_query_queue_family_info(
          endpoint_, ordinal, &family_info)));
      if (family_info.command_type == AMDF_QUEUE_COMMAND_TYPE_XDNA &&
          (family_info.publication_modes &
           AMDF_QUEUE_PUBLICATION_MODE_KERNEL) != 0) {
        return ordinal;
      }
    }
    ADD_FAILURE() << "materialized XDNA device has no kernel queue family";
    return UINT32_MAX;
  }

  void CreateQueue() {
    amdf_xdna_kernel_queue_create_info_t create_info = {};
    create_info.type = AMDF_STRUCTURE_TYPE_XDNA_KERNEL_QUEUE_CREATE_INFO;
    create_info.structure_size = sizeof(create_info);
    create_info.queue_family_ordinal = QueryKernelQueueFamily();
    ASSERT_NE(create_info.queue_family_ordinal, UINT32_MAX);
    ASSERT_TRUE(amdf_status_is_ok(
        xdna_api_->kernel_queue_create(device_, &create_info, &queue_)));
  }

  amdf_xdna_program_t* program_ = nullptr;
  amdf_xdna_command_t* command_ = nullptr;
  amdf_kernel_queue_t* queue_ = nullptr;
};

TEST_F(XdnaKernelQueueTest, PublishesRetiresAndReusesPreparedCommand) {
  CreateNoOpCommand();
  CreateQueue();

  amdf_kernel_queue_info_t queue_info = {};
  queue_info.type = AMDF_STRUCTURE_TYPE_KERNEL_QUEUE_INFO;
  queue_info.structure_size = sizeof(queue_info);
  ASSERT_TRUE(
      amdf_status_is_ok(api_->kernel_queue_query_info(queue_, &queue_info)));
  EXPECT_EQ(queue_info.command_type, AMDF_QUEUE_COMMAND_TYPE_XDNA);
  EXPECT_EQ(queue_info.maximum_pending_submission_count, 1u);
  EXPECT_EQ(queue_info.maximum_command_count, 1u);

  amdf_kernel_queue_status_t queue_status = {};
  queue_status.type = AMDF_STRUCTURE_TYPE_KERNEL_QUEUE_STATUS;
  queue_status.structure_size = sizeof(queue_status);
  ASSERT_TRUE(amdf_status_is_ok(
      api_->kernel_queue_query_status(queue_, &queue_status)));
  EXPECT_EQ(queue_status.retired_submission, 0u);
  EXPECT_EQ(queue_status.state, AMDF_KERNEL_QUEUE_STATE_ACTIVE);
  EXPECT_TRUE(amdf_status_is_ok(queue_status.terminal_status));
  EXPECT_EQ(amdf_status_code(api_->kernel_queue_wait(queue_, 1, 0, 0)),
            AMDF_STATUS_CODE_OUT_OF_RANGE);

  amdf_xdna_command_t* commands[] = {command_};
  amdf_xdna_kernel_queue_submission_info_t submission_info = {};
  submission_info.type = AMDF_STRUCTURE_TYPE_XDNA_KERNEL_QUEUE_SUBMISSION_INFO;
  submission_info.structure_size = sizeof(submission_info);
  submission_info.command_count = 1;
  submission_info.commands = commands;

  uint64_t first_submission = 0;
  ASSERT_TRUE(amdf_status_is_ok(xdna_api_->kernel_queue_submit(
      queue_, &submission_info, &first_submission)));
  EXPECT_EQ(first_submission, 1u);
  EXPECT_EQ(amdf_status_code(xdna_api_->command_destroy(command_)),
            AMDF_STATUS_CODE_BUSY);

  uint64_t rejected_submission = UINT64_MAX;
  EXPECT_EQ(amdf_status_code(xdna_api_->kernel_queue_submit(
                queue_, &submission_info, &rejected_submission)),
            AMDF_STATUS_CODE_BUSY);
  EXPECT_EQ(rejected_submission, UINT64_MAX);
  EXPECT_EQ(amdf_status_code(api_->kernel_queue_destroy(queue_)),
            AMDF_STATUS_CODE_BUSY);

  ASSERT_TRUE(amdf_status_is_ok(api_->kernel_queue_wait(
      queue_, first_submission, UINT64_C(5000000000), UINT64_C(50000))));
  ASSERT_TRUE(amdf_status_is_ok(
      api_->kernel_queue_query_status(queue_, &queue_status)));
  EXPECT_EQ(queue_status.retired_submission, first_submission);

  uint64_t second_submission = 0;
  ASSERT_TRUE(amdf_status_is_ok(xdna_api_->kernel_queue_submit(
      queue_, &submission_info, &second_submission)));
  EXPECT_EQ(second_submission, 2u);
  ASSERT_TRUE(amdf_status_is_ok(api_->kernel_queue_wait(
      queue_, second_submission, UINT64_C(5000000000), 0)));

  ASSERT_TRUE(amdf_status_is_ok(api_->kernel_queue_destroy(queue_)));
  queue_ = nullptr;
  ASSERT_TRUE(amdf_status_is_ok(xdna_api_->command_destroy(command_)));
  command_ = nullptr;
}

}  // namespace
