// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <initializer_list>

#include "iree/base/internal/arena.h"
#include "iree/testing/gtest.h"
#include "iree/testing/status_matchers.h"
#include "loom/codegen/low/frame.h"
#include "loom/codegen/low/text_asm.h"
#include "loom/format/text/parser.h"
#include "loom/ir/context.h"
#include "loom/ops/low/ops.h"
#include "loom/target/test/low_registry.h"
#include "loom/testing/module_ptr.h"

namespace loom {
namespace {

class FixedStorageTest
    : public ::testing::TestWithParam<loom_low_schedule_strategy_t> {
 protected:
  void SetUp() override {
    iree_arena_block_pool_initialize(4096, iree_allocator_system(), &pool_);
    iree_arena_initialize(&pool_, &arena_);
    loom_context_initialize(iree_allocator_system(), &context_);
    iree_host_size_t count = 0;
    const auto* vtables = loom_low_dialect_vtables(&count);
    IREE_ASSERT_OK(loom_context_register_dialect(&context_, LOOM_DIALECT_LOW,
                                                 vtables, (uint16_t)count));
    IREE_ASSERT_OK(loom_context_finalize(&context_));
    loom_test_low_descriptor_registry_initialize(&registry_);
  }

  void TearDown() override {
    module_.reset();
    loom_context_deinitialize(&context_);
    iree_arena_deinitialize(&arena_);
    iree_arena_block_pool_deinitialize(&pool_);
  }

  void Parse(const char* source) {
    loom_text_parse_options_t options = {};
    options.diagnostic_sink = {loom_diagnostic_stderr_sink, nullptr};
    loom_low_descriptor_text_asm_environment_initialize(
        &registry_.registry, &options.low_asm_environment);
    loom_module_t* module = nullptr;
    IREE_CHECK_OK(loom_text_parse(iree_make_cstring_view(source),
                                  IREE_SV("fixed_storage_test.loom"), &context_,
                                  &pool_, &options, &module));
    module_.reset(module);
    function_ = loom_block_op(loom_module_block(module), 0);
    body_ = loom_region_entry_block(loom_low_func_def_body(function_));
  }

  loom_value_id_t Result(uint32_t ordinal) {
    return loom_op_const_results(loom_block_op(body_, ordinal))[0];
  }

  loom_low_allocation_fixed_value_t Fixed(loom_value_id_t value,
                                          uint32_t location,
                                          uint32_t count = 1) {
    return {value, LOOM_LOW_ALLOCATION_LOCATION_PHYSICAL_REGISTER, location,
            count};
  }

  iree_status_t Build(
      std::initializer_list<loom_low_allocation_fixed_value_t> bindings) {
    const loom_low_emission_frame_options_t options = {
        .descriptor_registry = &registry_.registry,
        .schedule_strategy = GetParam(),
        .allocation_fixed_values = bindings.begin(),
        .allocation_fixed_value_count = bindings.size(),
    };
    iree_status_t status = loom_low_emission_frame_build(
        module_.get(), function_, &options, &arena_, &frame_);
    if (iree_status_is_ok(status)) {
      EXPECT_EQ(frame_.schedule.error_count, 0u);
      EXPECT_EQ(frame_.allocation.error_count, 0u);
      EXPECT_EQ(frame_.allocation.spill_count, 0u);
    }
    return status;
  }

  bool HasStorageEdge(uint32_t producer, uint32_t consumer) {
    for (uint32_t i = 0; i < frame_.schedule.dependencies.count; ++i) {
      const auto* edge = loom_low_schedule_dependency_graph_at(
          &frame_.schedule.dependencies, i);
      if (edge->kind == LOOM_LOW_SCHEDULE_DEPENDENCY_STORAGE &&
          edge->producer_node == producer && edge->consumer_node == consumer)
        return true;
    }
    return false;
  }

  // Production parser context and shared descriptor registry.
  loom_context_t context_ = {};
  // Generated test target used by the ordinary frame builder.
  loom_target_low_descriptor_registry_t registry_ = {};
  // Storage shared by the parsed module and retained frame.
  iree_arena_block_pool_t pool_ = {};
  // Retained output arena.
  iree_arena_allocator_t arena_ = {};
  // Authored Low input.
  ::loom::testing::ModulePtr module_;
  // Sole test function in module_.
  loom_op_t* function_ = nullptr;
  // Entry block used to select authored value IDs.
  loom_block_t* body_ = nullptr;
  // Real scheduling/allocation result, not a mocked graph.
  loom_low_emission_frame_t frame_ = {};
};

TEST_P(FixedStorageTest, EveryReaderPrecedesTheNextDefinition) {
  Parse(R"(
low.func.def target<test.low.core> @readers(%seed: reg<test.phys>) -> (reg<test.phys>) asm {
  %old = test.event.write.fast.phys %seed
  test.event.read.late.phys %old
  test.event.read.late.phys %old
  %next = test.event.write.fast.phys %seed
  return %next
}
)");
  IREE_ASSERT_OK(Build(
      {Fixed(body_->arg_ids[0], 1), Fixed(Result(0), 0), Fixed(Result(3), 0)}));
  EXPECT_TRUE(HasStorageEdge(0, 3));
  EXPECT_TRUE(HasStorageEdge(1, 3));
  EXPECT_TRUE(HasStorageEdge(2, 3));
  EXPECT_LT(frame_.schedule.nodes[2].scheduled_ordinal,
            frame_.schedule.nodes[3].scheduled_ordinal);
}

TEST_P(FixedStorageTest, ReadersCanMoveBeforeAnAuthoredOverwrite) {
  Parse(R"(
low.func.def target<test.low.core> @late_reader(%seed: reg<test.phys>) -> (reg<test.phys>) asm {
  %old = test.event.write.fast.phys %seed
  %next = test.event.write.fast.phys %seed
  test.event.read.late.phys %old
  return %next
}
)");
  IREE_ASSERT_OK(Build(
      {Fixed(body_->arg_ids[0], 1), Fixed(Result(0), 0), Fixed(Result(1), 0)}));
  EXPECT_TRUE(HasStorageEdge(2, 1));
  EXPECT_LT(frame_.schedule.nodes[2].scheduled_ordinal,
            frame_.schedule.nodes[1].scheduled_ordinal);
}

TEST_P(FixedStorageTest, EntryAggregateOverlapsItsNoncontiguousPhysicalUnits) {
  Parse(R"(
low.func.def target<test.low.core> @aggregate(%wide: reg<test.explicit32 x2>, %seed: reg<test.i32>) -> (reg<test.i32 x2>, reg<test.explicit32>) asm {
  %read = copy %wide : reg<test.explicit32 x2> -> reg<test.i32 x2>
  %next = copy %seed : reg<test.i32> -> reg<test.explicit32>
  return %read, %next
}
)");
  // l0 (physical ID 4) aliases r0/r2, not the adjacent register IDs 4/5.
  IREE_ASSERT_OK(Build({Fixed(body_->arg_ids[0], 4, 2), Fixed(Result(1), 2)}));
  EXPECT_TRUE(HasStorageEdge(0, 1));
}

TEST_P(FixedStorageTest, LinearAliasClassesShareTheSameStorageNamespace) {
  Parse(R"(
low.func.def target<test.low.core> @linear_alias(%old: reg<test.alias32>, %seed: reg<test.i64>) -> (reg<test.i32>, reg<test.alias64>) asm {
  %read = copy %old : reg<test.alias32> -> reg<test.i32>
  %next = copy %seed : reg<test.i64> -> reg<test.alias64>
  return %read, %next
}
)");
  IREE_ASSERT_OK(Build({Fixed(body_->arg_ids[0], 0), Fixed(Result(1), 0)}));
  EXPECT_TRUE(HasStorageEdge(0, 1));
}

TEST_P(FixedStorageTest, DistinctLinearClassesDoNotSerializeMatchingLocations) {
  Parse(R"(
low.func.def target<test.low.core> @distinct(%old: reg<test.phys>, %seed: reg<test.i64>) -> (reg<test.i32>, reg<test.alias64>) asm {
  %read = copy %old : reg<test.phys> -> reg<test.i32>
  %next = copy %seed : reg<test.i64> -> reg<test.alias64>
  return %read, %next
}
)");
  IREE_ASSERT_OK(Build({Fixed(body_->arg_ids[0], 0), Fixed(Result(1), 0)}));
  EXPECT_FALSE(HasStorageEdge(0, 1));
}

TEST_P(FixedStorageTest, EntryReadersUseTheFirstWriterInTheirOwnBranch) {
  Parse(R"(
low.func.def target<test.low.core> @branches(%seed: reg<test.phys>, %condition: reg<test.i32>) -> (reg<test.phys>) asm {
  %old = test.event.write.fast.phys %seed
  low.cond_br %condition, ^left, ^right : reg<test.i32>
^left:
  test.event.read.late.phys %old
  %left = test.event.write.fast.phys %seed
  low.br ^exit(%left: reg<test.phys>)
^right:
  test.event.read.late.phys %old
  %right = test.event.write.fast.phys %seed
  low.br ^exit(%right: reg<test.phys>)
^exit(%selected: reg<test.phys>):
  return %selected
}
)");
  auto* region = loom_low_func_def_body(function_);
  const auto left =
      loom_op_const_results(loom_block_op(loom_region_block(region, 1), 1))[0];
  const auto right =
      loom_op_const_results(loom_block_op(loom_region_block(region, 2), 1))[0];
  IREE_ASSERT_OK(Build({Fixed(body_->arg_ids[0], 1), Fixed(Result(0), 0),
                        Fixed(left, 0), Fixed(right, 0)}));
  EXPECT_TRUE(HasStorageEdge(2, 3));
  EXPECT_TRUE(HasStorageEdge(5, 6));
  EXPECT_FALSE(HasStorageEdge(2, 6));
  EXPECT_FALSE(HasStorageEdge(5, 3));
}

TEST_P(FixedStorageTest, SuccessiveGenerationsRetainOnlyTheirNextOverwrite) {
  Parse(R"(
low.func.def target<test.low.core> @generations(%seed: reg<test.phys>) -> (reg<test.phys>) asm {
  %first = test.event.write.fast.phys %seed
  test.event.read.late.phys %first
  %second = test.event.write.fast.phys %seed
  test.event.read.late.phys %second
  %third = test.event.write.fast.phys %seed
  return %third
}
)");
  IREE_ASSERT_OK(Build({Fixed(body_->arg_ids[0], 1), Fixed(Result(0), 0),
                        Fixed(Result(2), 0), Fixed(Result(4), 0)}));
  EXPECT_TRUE(HasStorageEdge(1, 2));
  EXPECT_TRUE(HasStorageEdge(3, 4));
  EXPECT_FALSE(HasStorageEdge(1, 4));
}

TEST_P(FixedStorageTest, RejectsInvalidPhysicalViewsBeforeIndexing) {
  Parse(R"(
low.func.def target<test.low.core> @invalid(%wide: reg<test.explicit32 x2>) -> (reg<test.explicit32 x2>) asm {
  return %wide
}
)");
  IREE_EXPECT_STATUS_IS(IREE_STATUS_INVALID_ARGUMENT,
                        Build({Fixed(body_->arg_ids[0], 999, 2)}));
}

TEST_P(FixedStorageTest, RejectsDuplicateBindingsBeforeIndexing) {
  Parse(R"(
low.func.def target<test.low.core> @duplicate(%value: reg<test.phys>) -> (reg<test.phys>) asm {
  return %value
}
)");
  IREE_EXPECT_STATUS_IS(
      IREE_STATUS_INVALID_ARGUMENT,
      Build({Fixed(body_->arg_ids[0], 0), Fixed(body_->arg_ids[0], 1)}));
}

INSTANTIATE_TEST_SUITE_P(
    AllPolicies, FixedStorageTest,
    ::testing::Values(LOOM_LOW_SCHEDULE_STRATEGY_SOURCE_PRIORITY,
                      LOOM_LOW_SCHEDULE_STRATEGY_PRESSURE,
                      LOOM_LOW_SCHEDULE_STRATEGY_LATENCY_HIDING,
                      LOOM_LOW_SCHEDULE_STRATEGY_RESOURCE_STALL));

}  // namespace
}  // namespace loom
