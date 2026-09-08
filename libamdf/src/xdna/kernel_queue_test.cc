// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/xdna/kernel_queue.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "gtest/gtest.h"
#include "libamdf/src/device.h"
#include "libamdf/src/endpoint.h"
#include "libamdf/src/kernel_queue.h"
#include "libamdf/src/xdna/command.h"
#include "libamdf/src/xdna/device.h"
#include "libamdf/src/xdna/program.h"
#include "libamdf/src/xdna/umd/kernel_queue.h"

// Native dependencies are controlled here; submission ownership, retirement,
// public status, and wait-budget handling use the production queue code.
struct amdf_xdna_umd_kernel_queue_t {
  // Native completion frontier published independently of host retirement.
  std::atomic<uint64_t> progress{0};
  // Sticky execution failure observed only during protected retirement.
  std::atomic<amdf_status_t> terminal_status{AMDF_STATUS_OK};
  // Native wait behavior selected before a test starts observing the queue.
  enum class WaitAction {
    kTimeout,
    kComplete,
    kCompleteWithError
  } action = WaitAction::kComplete;
  // Native execution result consumed after completion proves retirement.
  amdf_status_t completion_status = AMDF_STATUS_OK;
  // Number of explicit native wait/poll calls.
  std::atomic<size_t> wait_count{0};
};

struct amdf_xdna_umd_device_t {
  // Device-owned native queue dependency leased by the production queue.
  amdf_xdna_umd_kernel_queue_t queue;
};

struct amdf_xdna_program_t {
  // Device borrowed by this command's program.
  amdf_device_t* device = nullptr;
};

struct amdf_xdna_umd_command_t {
  // Coordinates a preempted observer inside its protected retirement section.
  mutable std::mutex mutex;
  // Explicit readiness/release condition for the retirement observer.
  mutable std::condition_variable condition;
  // Retirement pause phase; all accesses occur under mutex.
  mutable enum class Phase {
    kRunning,
    kPauseRequested,
    kPaused,
    kReleased
  } phase = Phase::kRunning;
};

struct amdf_xdna_command_t {
  // Program identifying the submitting device.
  amdf_xdna_program_t program;
  // Immutable native command dependency retained by accepted submissions.
  amdf_xdna_umd_command_t native;
  // Number of outstanding production submission borrows.
  std::atomic<size_t> borrows{0};
};

namespace {

struct Device {
  // Production generic device base consumed by queue creation.
  amdf_device_t base = {};
  // Immutable device identity returned by the XDNA device dependency.
  amdf_xdna_device_info_t info = {};
  // Controlled native queue dependency.
  amdf_xdna_umd_device_t native;
};

class XdnaKernelQueueTest : public ::testing::Test {
 protected:
  void SetUp() override {
    device.base.engine_kind = AMDF_ENGINE_KIND_XDNA;
    amdf_child_tracker_initialize(&device.base.children);
    device.info.reset_epoch = 1;
    command.program.device = &device.base;
    amdf_xdna_kernel_queue_create_info_t create = {};
    create.type = AMDF_STRUCTURE_TYPE_XDNA_KERNEL_QUEUE_CREATE_INFO;
    create.structure_size = sizeof(create);
    ASSERT_EQ(amdf_xdna_kernel_queue_create(&device.base, &create, &queue),
              AMDF_STATUS_OK);
  }

  void TearDown() override {
    device.native.queue.progress = 1;
    if (queue) EXPECT_EQ(amdf_kernel_queue_destroy(queue), AMDF_STATUS_OK);
    EXPECT_EQ(command.borrows.load(), 0u);
    EXPECT_EQ(amdf_child_tracker_count(&device.base.children), 0u);
  }

  void Submit() {
    amdf_xdna_command_t* commands[] = {&command};
    amdf_xdna_kernel_queue_submission_info_t submit = {};
    submit.type = AMDF_STRUCTURE_TYPE_XDNA_KERNEL_QUEUE_SUBMISSION_INFO;
    submit.structure_size = sizeof(submit);
    submit.command_count = 1;
    submit.commands = commands;
    ASSERT_EQ(amdf_xdna_kernel_queue_submit(queue, &submit, &submission),
              AMDF_STATUS_OK);
    ASSERT_EQ(submission, 1u);
    ASSERT_EQ(command.borrows.load(), 1u);
  }

  amdf_kernel_queue_status_t Query() {
    amdf_kernel_queue_status_t status = {};
    status.type = AMDF_STRUCTURE_TYPE_KERNEL_QUEUE_STATUS;
    status.structure_size = sizeof(status);
    EXPECT_EQ(amdf_kernel_queue_query_status(queue, &status), AMDF_STATUS_OK);
    return status;
  }

  // Device dependency retained until production queue teardown.
  Device device;
  // Command dependency whose borrow count witnesses ownership.
  amdf_xdna_command_t command;
  // Production queue under test.
  amdf_kernel_queue_t* queue = nullptr;
  // Accepted public submission identity.
  uint64_t submission = 0;
};

TEST_F(XdnaKernelQueueTest, ZeroTimeoutRefreshesNativeProgress) {
  ASSERT_NO_FATAL_FAILURE(Submit());
  EXPECT_EQ(Query().retired_submission, 0u);
  EXPECT_EQ(device.native.queue.wait_count.load(), 0u);
  EXPECT_EQ(amdf_kernel_queue_wait(queue, submission, 0, 0), AMDF_STATUS_OK);
  EXPECT_EQ(device.native.queue.wait_count.load(), 1u);
  EXPECT_EQ(command.borrows.load(), 0u);
  EXPECT_EQ(Query().retired_submission, submission);
}

TEST_F(XdnaKernelQueueTest, TimeoutRetainsAcceptedCommand) {
  device.native.queue.action =
      amdf_xdna_umd_kernel_queue_t::WaitAction::kTimeout;
  ASSERT_NO_FATAL_FAILURE(Submit());
  EXPECT_EQ(amdf_kernel_queue_wait(queue, submission, 0, 0),
            amdf_make_api_status(AMDF_STATUS_CODE_DEADLINE_EXCEEDED));
  EXPECT_EQ(device.native.queue.wait_count.load(), 1u);
  EXPECT_EQ(command.borrows.load(), 1u);
  EXPECT_EQ(Query().retired_submission, 0u);
  EXPECT_EQ(amdf_kernel_queue_destroy(queue),
            amdf_make_api_status(AMDF_STATUS_CODE_BUSY));
  device.native.queue.progress = 1;
  EXPECT_EQ(Query().retired_submission, submission);
  EXPECT_EQ(command.borrows.load(), 0u);
}

TEST_F(XdnaKernelQueueTest, FiniteWaitDoesNotBlockBehindRetirementObserver) {
  ASSERT_NO_FATAL_FAILURE(Submit());
  command.native.phase = amdf_xdna_umd_command_t::Phase::kPauseRequested;
  device.native.queue.progress = 1;
  std::thread observer([&] { Query(); });
  {
    std::unique_lock<std::mutex> lock(command.native.mutex);
    command.native.condition.wait(lock, [&] {
      return command.native.phase == amdf_xdna_umd_command_t::Phase::kPaused;
    });
  }
  EXPECT_EQ(amdf_kernel_queue_wait(queue, submission, 0, 0),
            amdf_make_api_status(AMDF_STATUS_CODE_DEADLINE_EXCEEDED));
  EXPECT_EQ(command.borrows.load(), 1u);
  EXPECT_EQ(device.native.queue.wait_count.load(), 0u);
  {
    std::lock_guard<std::mutex> lock(command.native.mutex);
    command.native.phase = amdf_xdna_umd_command_t::Phase::kReleased;
    command.native.condition.notify_all();
  }
  observer.join();
  EXPECT_EQ(Query().retired_submission, submission);
  EXPECT_EQ(command.borrows.load(), 0u);
}

TEST_F(XdnaKernelQueueTest, CompletedFailureRetiresBeforeReportingError) {
  const auto failure = amdf_make_status(AMDF_STATUS_DOMAIN_FIRMWARE, 5);
  device.native.queue.completion_status = failure;
  ASSERT_NO_FATAL_FAILURE(Submit());
  EXPECT_EQ(amdf_kernel_queue_wait(queue, submission, AMDF_TIMEOUT_INFINITE, 0),
            failure);
  EXPECT_EQ(command.borrows.load(), 0u);
  const auto status = Query();
  EXPECT_EQ(status.retired_submission, submission);
  EXPECT_EQ(status.terminal_status, failure);
  EXPECT_EQ(status.state, AMDF_KERNEL_QUEUE_STATE_DEVICE_LOST);
}

TEST_F(XdnaKernelQueueTest, NativeWaitErrorDoesNotEraseConfirmedRetirement) {
  device.native.queue.action =
      amdf_xdna_umd_kernel_queue_t::WaitAction::kCompleteWithError;
  ASSERT_NO_FATAL_FAILURE(Submit());
  EXPECT_EQ(amdf_kernel_queue_wait(queue, submission, AMDF_TIMEOUT_INFINITE, 0),
            amdf_make_api_status(AMDF_STATUS_CODE_INTERNAL));
  EXPECT_EQ(command.borrows.load(), 0u);
  EXPECT_EQ(Query().retired_submission, submission);
  EXPECT_EQ(Query().terminal_status, AMDF_STATUS_OK);
}

}  // namespace

extern "C" {

amdf_status_t amdf_endpoint_register_device(amdf_endpoint_t*) {
  return AMDF_STATUS_OK;
}
void amdf_endpoint_unregister_device(amdf_endpoint_t*) {}
amdf_status_t AMDF_CALL amdf_endpoint_query_queue_family_info(
    amdf_endpoint_t*, uint32_t ordinal, amdf_queue_family_info_t* out_info) {
  out_info->ordinal = ordinal;
  out_info->command_type = AMDF_QUEUE_COMMAND_TYPE_XDNA;
  out_info->publication_modes = AMDF_QUEUE_PUBLICATION_MODE_KERNEL;
  return AMDF_STATUS_OK;
}
const amdf_xdna_device_info_t* amdf_xdna_device_get_info(
    const amdf_device_t* device) {
  return &reinterpret_cast<const Device*>(device)->info;
}
uint64_t amdf_xdna_device_query_reset_epoch(const amdf_device_t* device) {
  return amdf_xdna_device_get_info(device)->reset_epoch;
}
amdf_xdna_umd_device_t* amdf_xdna_device_get_umd(amdf_device_t* device) {
  return &reinterpret_cast<Device*>(device)->native;
}
amdf_device_t* amdf_xdna_program_get_device(amdf_xdna_program_t* program) {
  return program->device;
}
amdf_xdna_program_t* amdf_xdna_command_get_program(
    amdf_xdna_command_t* command) {
  return &command->program;
}
const amdf_xdna_umd_command_t* amdf_xdna_command_get_umd(
    const amdf_xdna_command_t* command) {
  return &command->native;
}
amdf_status_t amdf_xdna_command_register_submission(
    amdf_xdna_command_t* command) {
  ++command->borrows;
  return AMDF_STATUS_OK;
}
void amdf_xdna_command_unregister_submission(amdf_xdna_command_t* command) {
  EXPECT_EQ(command->borrows.fetch_sub(1), 1u);
}
amdf_status_t amdf_xdna_umd_kernel_queue_create(
    amdf_xdna_umd_device_t* device, amdf_xdna_umd_kernel_queue_t** out_queue) {
  *out_queue = &device->queue;
  return AMDF_STATUS_OK;
}
amdf_status_t amdf_xdna_umd_kernel_queue_submit(amdf_xdna_umd_kernel_queue_t*,
                                                const amdf_xdna_umd_command_t*,
                                                uint64_t* out_submission) {
  *out_submission = 1;
  return AMDF_STATUS_OK;
}
uint64_t amdf_xdna_umd_kernel_queue_query_progress(
    const amdf_xdna_umd_kernel_queue_t* queue) {
  return queue->progress.load();
}
void amdf_xdna_umd_kernel_queue_retire_command(
    amdf_xdna_umd_kernel_queue_t* queue,
    const amdf_xdna_umd_command_t* command) {
  std::unique_lock<std::mutex> lock(command->mutex);
  if (command->phase == amdf_xdna_umd_command_t::Phase::kPauseRequested) {
    command->phase = amdf_xdna_umd_command_t::Phase::kPaused;
    command->condition.notify_all();
    command->condition.wait(lock, [&] {
      return command->phase == amdf_xdna_umd_command_t::Phase::kReleased;
    });
  }
  queue->terminal_status = queue->completion_status;
}
amdf_status_t amdf_xdna_umd_kernel_queue_query_terminal_status(
    const amdf_xdna_umd_kernel_queue_t* queue) {
  return queue->terminal_status.load();
}
amdf_status_t amdf_xdna_umd_kernel_queue_wait(
    amdf_xdna_umd_kernel_queue_t* queue, uint64_t submission,
    const amdf_wait_deadline_t*) {
  ++queue->wait_count;
  if (queue->action == amdf_xdna_umd_kernel_queue_t::WaitAction::kTimeout) {
    return amdf_make_api_status(AMDF_STATUS_CODE_DEADLINE_EXCEEDED);
  }
  queue->progress = submission;
  return queue->action ==
                 amdf_xdna_umd_kernel_queue_t::WaitAction::kCompleteWithError
             ? amdf_make_api_status(AMDF_STATUS_CODE_INTERNAL)
             : AMDF_STATUS_OK;
}
amdf_status_t amdf_xdna_umd_kernel_queue_destroy(
    amdf_xdna_umd_kernel_queue_t*) {
  return AMDF_STATUS_OK;
}

}  // extern "C"
