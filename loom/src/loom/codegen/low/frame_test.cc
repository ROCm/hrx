// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/codegen/low/frame.h"

#include "iree/base/internal/arena.h"
#include "iree/testing/gtest.h"
#include "iree/testing/status_matchers.h"
#include "loom/codegen/low/rematerialization.h"
#include "loom/codegen/low/text_asm.h"
#include "loom/format/text/parser.h"
#include "loom/ir/context.h"
#include "loom/ir/module.h"
#include "loom/ops/low/ops.h"
#include "loom/target/test/descriptors.h"
#include "loom/target/test/low_registry.h"
#include "loom/testing/module_ptr.h"

namespace loom {
namespace {

using ModulePtr = ::loom::testing::ModulePtr;

constexpr loom_low_schedule_strategy_t kScheduleStrategies[] = {
    LOOM_LOW_SCHEDULE_STRATEGY_SOURCE_PRIORITY,
    LOOM_LOW_SCHEDULE_STRATEGY_PRESSURE,
    LOOM_LOW_SCHEDULE_STRATEGY_LATENCY_HIDING,
    LOOM_LOW_SCHEDULE_STRATEGY_RESOURCE_STALL,
};

class LowEmissionFrameTest : public ::testing::Test {
 protected:
  void SetUp() override {
    iree_arena_block_pool_initialize(4096, iree_allocator_system(),
                                     &block_pool_);
    loom_context_initialize(iree_allocator_system(), &context_);
    iree_host_size_t vtable_count = 0;
    const loom_op_vtable_t* const* vtables =
        loom_low_dialect_vtables(&vtable_count);
    IREE_ASSERT_OK(loom_context_register_dialect(
        &context_, LOOM_DIALECT_LOW, vtables, (uint16_t)vtable_count));
    IREE_ASSERT_OK(loom_context_finalize(&context_));
    loom_test_low_descriptor_registry_initialize(&registry_);
    iree_arena_initialize(&block_pool_, &arena_);
  }

  void TearDown() override {
    iree_arena_deinitialize(&arena_);
    loom_context_deinitialize(&context_);
    iree_arena_block_pool_deinitialize(&block_pool_);
  }

  ModulePtr ParseModule(const char* source) {
    loom_text_parse_options_t options = {};
    options.diagnostic_sink = {loom_diagnostic_stderr_sink, nullptr};
    loom_low_descriptor_text_asm_environment_initialize(
        &registry_.registry, &options.low_asm_environment);
    loom_module_t* module = nullptr;
    IREE_CHECK_OK(loom_text_parse(iree_make_cstring_view(source),
                                  IREE_SV("frame_test.loom"), &context_,
                                  &block_pool_, &options, &module));
    return ModulePtr(module);
  }

  ModulePtr ParseModule() {
    return ParseModule(R"(
low.func.def target<test.low.core> @structural_model() -> (reg<test.i32 x4>) asm {
  %storage = storage {byte_alignment = 16, byte_length = 64} : low.storage<workgroup>
  %address = storage_address %storage : low.storage<workgroup> -> reg<test.ptr>
  %value = test.load.v4i32 %address
  return %value
}
)");
  }

  iree_status_t BuildFrame(
      loom_module_t* module,
      loom_low_schedule_structural_model_list_t structural_models,
      loom_low_emission_frame_t* out_frame,
      loom_low_schedule_strategy_t schedule_strategy =
          LOOM_LOW_SCHEDULE_STRATEGY_RESOURCE_STALL) {
    loom_block_t* module_block = loom_module_block(module);
    IREE_ASSERT_EQ(module_block->op_count, 1);
    loom_low_emission_frame_options_t options = {};
    options.descriptor_registry = &registry_.registry;
    options.schedule_structural_models = structural_models;
    options.schedule_strategy = schedule_strategy;
    return loom_low_emission_frame_build(module, loom_block_op(module_block, 0),
                                         &options, &arena_, out_frame);
  }

  iree_status_t BuildSpillFreeFrame(loom_module_t* module,
                                    loom_low_planning_statistics_t* statistics,
                                    loom_low_emission_frame_t* out_frame) {
    loom_block_t* module_block = loom_module_block(module);
    IREE_ASSERT_EQ(module_block->op_count, 1);
    const loom_low_emission_frame_options_t frame_options = {
        .descriptor_registry = &registry_.registry,
        .schedule_strategy = LOOM_LOW_SCHEDULE_STRATEGY_SOURCE_PRIORITY,
        .emitter =
            {
                .fn =
                    [](void*, const loom_diagnostic_emission_t*) {
                      return iree_ok_status();
                    },
            },
        .statistics = statistics,
    };
    const loom_low_emission_frame_spill_free_options_t spill_free_options = {
        .materialization_options =
            {
                .has_supported_storage_spaces = true,
                .supported_storage_spaces = LOOM_LOW_STORAGE_SPACE_SET_NONE,
            },
    };
    return loom_low_emission_frame_build_spill_free(
        module, loom_block_op(module_block, 0), &frame_options,
        &spill_free_options, &arena_, out_frame);
  }

  const loom_low_schedule_node_t* FindNode(
      const loom_low_emission_frame_t& frame, loom_op_kind_t op_kind) {
    for (iree_host_size_t i = 0; i < frame.schedule.node_count; ++i) {
      if (frame.schedule.nodes[i].op->kind == op_kind) {
        return &frame.schedule.nodes[i];
      }
    }
    return nullptr;
  }

  iree_arena_block_pool_t block_pool_ = {};
  loom_context_t context_ = {};
  loom_target_low_descriptor_registry_t registry_ = {};
  iree_arena_allocator_t arena_ = {};
};

TEST_F(LowEmissionFrameTest, EveryStrategyEnforcesIssueResourceCapacity) {
  for (const loom_low_schedule_strategy_t strategy : kScheduleStrategies) {
    SCOPED_TRACE(static_cast<int>(strategy));
    ModulePtr module = ParseModule(R"(
low.func.def target<test.low.core> @resource_capacity(%lhs: reg<test.i32>, %rhs: reg<test.i32>) -> (reg<test.i32>, reg<test.i32>) asm {
  %first = test.resource.serial.i32 %lhs, %rhs
  %second = test.resource.serial.i32 %lhs, %rhs
  return %first, %second
}
)");
    loom_low_emission_frame_t frame = {};
    IREE_ASSERT_OK(BuildFrame(module.get(), {}, &frame, strategy));
    ASSERT_GE(frame.schedule.node_count, 2u);
    EXPECT_EQ(frame.schedule.nodes[0].issue_cycle, 0u);
    EXPECT_EQ(frame.schedule.nodes[1].issue_cycle, 4u);
  }
}

TEST_F(LowEmissionFrameTest, EveryStrategyClosesSingletonBeforeReopening) {
  for (const loom_low_schedule_strategy_t strategy : kScheduleStrategies) {
    SCOPED_TRACE(static_cast<int>(strategy));
    ModulePtr module = ParseModule(R"(
low.func.def target<test.low.core> @singleton(%condition: reg<test.i32>, %lhs: reg<test.i32>, %rhs: reg<test.i32>) -> (reg<test.i32>, reg<test.i32>) asm {
  %prepared = test.mul.i32 %lhs, %rhs
  %first = copy %condition : reg<test.i32> -> reg<test.fixed.r0>
  %selected0 = test.fixed.select.i32 %prepared, %rhs, %first
  %second = copy %prepared : reg<test.i32> -> reg<test.fixed.r0>
  %selected1 = test.fixed.select.i32 %lhs, %rhs, %second
  return %selected0, %selected1
}
)");
    loom_low_emission_frame_t frame = {};
    IREE_ASSERT_OK(BuildFrame(module.get(), {}, &frame, strategy));
    EXPECT_EQ(frame.schedule.error_count, 0u);
    EXPECT_EQ(frame.allocation.error_count, 0u);
    EXPECT_EQ(frame.allocation.spill_count, 0u);
  }
}

TEST_F(LowEmissionFrameTest, EveryStrategyPreservesAggregateRegisterCapacity) {
  for (const loom_low_schedule_strategy_t strategy : kScheduleStrategies) {
    SCOPED_TRACE(static_cast<int>(strategy));
    // The two legal pairs are (r0, r2) and (r1, r3). Placing the scalars in
    // preference order (r0, r1) strands two free units in different pairs.
    ModulePtr module = ParseModule(R"(
low.func.def target<test.low.core> @aggregate(%lhs: reg<test.i32>, %rhs: reg<test.i32>, %pair: reg<test.i32 x2>) -> (reg<test.explicit32>, reg<test.explicit32>, reg<test.explicit32 x2>) asm {
  %first = copy %lhs : reg<test.i32> -> reg<test.explicit32>
  %second = copy %rhs : reg<test.i32> -> reg<test.explicit32>
  %wide = copy %pair : reg<test.i32 x2> -> reg<test.explicit32 x2>
  return %first, %second, %wide
}
)");
    loom_low_emission_frame_t frame = {};
    IREE_ASSERT_OK(BuildFrame(module.get(), {}, &frame, strategy));
    EXPECT_EQ(frame.schedule.error_count, 0u);
    EXPECT_EQ(frame.allocation.error_count, 0u);
    EXPECT_EQ(frame.allocation.spill_count, 0u);
  }
}

TEST_F(LowEmissionFrameTest, SingletonReservationsPreserveTiedStateUpdates) {
  ModulePtr module = ParseModule(R"(
low.func.def target<test.low.core> @fixed_updates(%state: reg<test.fixed.r0>, %delta: reg<test.i32>, %independent: reg<test.explicit32>) -> (reg<test.fixed.r0>, reg<test.explicit32>) asm {
  %first = test.fixed.update.i32 %state, %delta
  %second = test.fixed.update.i32 %first, %delta
  return %second, %independent
}
)");
  ASSERT_NE(module, nullptr);
  loom_low_emission_frame_t frame = {};
  IREE_ASSERT_OK(BuildFrame(module.get(), {}, &frame));
  ASSERT_EQ(frame.schedule.error_count, 0u);
  ASSERT_EQ(frame.allocation.error_count, 0u);
  EXPECT_EQ(frame.allocation.spill_count, 0u);
  EXPECT_EQ(frame.allocation.materialized_copy_count, 0u);
  uint32_t fixed_count = 0;
  for (iree_host_size_t i = 0; i < frame.allocation.assignment_count; ++i) {
    const auto& assignment = frame.allocation.assignments[i];
    if (assignment.descriptor_reg_class_id ==
        TEST_LOW_CORE_REG_CLASS_ID_TEST_EXPLICIT32) {
      EXPECT_NE(assignment.location_base, 0u);
    }
    if (assignment.descriptor_reg_class_id !=
        TEST_LOW_CORE_REG_CLASS_ID_TEST_FIXED_R0) {
      continue;
    }
    EXPECT_EQ(assignment.location_kind,
              LOOM_LOW_ALLOCATION_LOCATION_PHYSICAL_REGISTER);
    EXPECT_EQ(assignment.location_base, 0u);
    ++fixed_count;
  }
  EXPECT_EQ(fixed_count, 3u);
}

TEST_F(LowEmissionFrameTest, SingletonTiedStateCrossesDynamicLoopBackedge) {
  ModulePtr module = ParseModule(R"(
low.func.def target<test.low.core> @fixed_loop(%initial: reg<test.fixed.r0>, %count: reg<test.i32>) -> (reg<test.fixed.r0>) asm {
  %decrement = test.const.i32 -1
  low.br ^loop(%initial: reg<test.fixed.r0>, %count: reg<test.i32>)
^loop(%state: reg<test.fixed.r0>, %remaining: reg<test.i32>):
  low.cond_br %remaining, ^body, ^exit : reg<test.i32>
^body:
  %next_state = test.fixed.update.i32 %state, %decrement
  %next_remaining = test.add.i32 %remaining, %decrement
  low.br ^loop(%next_state: reg<test.fixed.r0>, %next_remaining: reg<test.i32>)
^exit:
  return %state
}
)");
  ASSERT_NE(module, nullptr);
  loom_low_emission_frame_t frame = {};
  IREE_ASSERT_OK(BuildFrame(module.get(), {}, &frame));
  ASSERT_EQ(frame.schedule.error_count, 0u);
  ASSERT_EQ(frame.allocation.error_count, 0u);
  EXPECT_EQ(frame.allocation.spill_count, 0u);
  EXPECT_EQ(frame.allocation.materialized_copy_count, 0u);
  for (iree_host_size_t i = 0; i < frame.allocation.assignment_count; ++i) {
    const auto& assignment = frame.allocation.assignments[i];
    if (assignment.descriptor_reg_class_id ==
        TEST_LOW_CORE_REG_CLASS_ID_TEST_FIXED_R0) {
      EXPECT_EQ(assignment.location_base, 0u);
    }
  }
}

TEST_F(LowEmissionFrameTest, OrderedEffectUsesDirectionalTimingEndpoints) {
  ModulePtr module = ParseModule(R"(
low.func.def target<test.low.core> @directional_effect(%address: reg<test.ptr>, %payload: reg<test.i32 x4>) -> (reg<test.i32 x4>) asm {
  test.store.v4i32 %address, %payload
  test.barrier
  %loaded = test.load.v4i32 %address
  return %loaded
}
)");
  loom_low_emission_frame_t frame = {};
  IREE_ASSERT_OK(BuildFrame(module.get(), {}, &frame,
                            LOOM_LOW_SCHEDULE_STRATEGY_SOURCE_PRIORITY));

  ASSERT_GE(frame.schedule.node_count, 3u);
  EXPECT_EQ(frame.schedule.nodes[0].issue_cycle, 0u);
  EXPECT_EQ(frame.schedule.nodes[1].issue_cycle, 2u);
  EXPECT_EQ(frame.schedule.nodes[2].issue_cycle, 4u);

  const loom_low_schedule_dependency_t* store_to_barrier = nullptr;
  const loom_low_schedule_dependency_t* barrier_to_load = nullptr;
  for (iree_host_size_t i = 0; i < frame.schedule.dependencies.count; ++i) {
    const loom_low_schedule_dependency_t* dependency =
        loom_low_schedule_dependency_graph_at(&frame.schedule.dependencies, i);
    if (dependency->kind != LOOM_LOW_SCHEDULE_DEPENDENCY_EFFECT ||
        dependency->separation_source !=
            LOOM_LOW_SCHEDULE_SEPARATION_SOURCE_EVENT_PAIR) {
      continue;
    }
    if (dependency->producer_node == 0 && dependency->consumer_node == 1) {
      store_to_barrier = dependency;
    } else if (dependency->producer_node == 1 &&
               dependency->consumer_node == 2) {
      barrier_to_load = dependency;
    }
  }
  ASSERT_NE(store_to_barrier, nullptr);
  ASSERT_NE(barrier_to_load, nullptr);
  EXPECT_EQ(store_to_barrier->minimum_issue_separation_cycles, 2);
  EXPECT_EQ(barrier_to_load->minimum_issue_separation_cycles, 2);
  EXPECT_NE(store_to_barrier->consumer_event_id,
            barrier_to_load->producer_event_id);
}

TEST_F(LowEmissionFrameTest, EffectTimingCrossesDirectCfgEdge) {
  ModulePtr module = ParseModule(R"(
low.func.def target<test.low.core> @direct_effect_edge(%address: reg<test.ptr>, %payload: reg<test.i32 x4>) -> (reg<test.i32 x4>) asm {
  test.store.v4i32 %address, %payload
  low.br ^consume
^consume:
  %loaded = test.load.v4i32 %address
  return %loaded
}
)");
  loom_low_emission_frame_t frame = {};
  IREE_ASSERT_OK(BuildFrame(module.get(), {}, &frame,
                            LOOM_LOW_SCHEDULE_STRATEGY_SOURCE_PRIORITY));

  ASSERT_EQ(frame.schedule.node_count, 4u);
  EXPECT_TRUE(loom_low_br_isa(frame.schedule.nodes[1].op));
  EXPECT_EQ(frame.schedule.nodes[0].issue_cycle, 0u);
  EXPECT_EQ(frame.schedule.nodes[1].issue_cycle, 2u);
  EXPECT_EQ(frame.schedule.nodes[2].issue_cycle, 0u);

  const loom_low_schedule_dependency_t* boundary_dependency = nullptr;
  for (iree_host_size_t i = 0; i < frame.schedule.dependencies.count; ++i) {
    const loom_low_schedule_dependency_t* dependency =
        loom_low_schedule_dependency_graph_at(&frame.schedule.dependencies, i);
    if (dependency->producer_node == 0 && dependency->consumer_node == 1 &&
        dependency->kind == LOOM_LOW_SCHEDULE_DEPENDENCY_EFFECT &&
        dependency->separation_source ==
            LOOM_LOW_SCHEDULE_SEPARATION_SOURCE_EVENT_PAIR) {
      boundary_dependency = dependency;
      break;
    }
  }
  ASSERT_NE(boundary_dependency, nullptr);
  EXPECT_EQ(boundary_dependency->minimum_issue_separation_cycles, 2);
  EXPECT_EQ(boundary_dependency->producer_attachment_kind,
            LOOM_LOW_SCHEDULE_DEPENDENCY_ATTACHMENT_EFFECT);
  EXPECT_EQ(boundary_dependency->consumer_attachment_kind,
            LOOM_LOW_SCHEDULE_DEPENDENCY_ATTACHMENT_NONE);
}

TEST_F(LowEmissionFrameTest, EffectTimingCrossesDiamondAndEmptyBlock) {
  ModulePtr module = ParseModule(R"(
low.func.def target<test.low.core> @diamond_effect_edge(%condition: reg<test.i32>, %address: reg<test.ptr>, %payload: reg<test.i32 x4>) -> (reg<test.i32 x4>) asm {
  test.store.v4i32 %address, %payload
  low.cond_br %condition, ^empty, ^write : reg<test.i32>
^empty:
  low.br ^read
^read:
  %loaded = test.load.v4i32 %address
  return %loaded
^write:
  test.store.v4i32 %address, %payload
  return %payload
}
)");
  loom_low_emission_frame_t frame = {};
  IREE_ASSERT_OK(BuildFrame(module.get(), {}, &frame,
                            LOOM_LOW_SCHEDULE_STRATEGY_SOURCE_PRIORITY));

  ASSERT_EQ(frame.schedule.node_count, 7u);
  EXPECT_TRUE(loom_low_cond_br_isa(frame.schedule.nodes[1].op));
  EXPECT_EQ(frame.schedule.nodes[0].issue_cycle, 0u);
  EXPECT_EQ(frame.schedule.nodes[1].issue_cycle, 2u);

  bool found_read_requirement = false;
  bool found_write_requirement = false;
  for (iree_host_size_t i = 0; i < frame.schedule.dependencies.count; ++i) {
    const loom_low_schedule_dependency_t* dependency =
        loom_low_schedule_dependency_graph_at(&frame.schedule.dependencies, i);
    if (dependency->producer_node != 0 || dependency->consumer_node != 1 ||
        dependency->kind != LOOM_LOW_SCHEDULE_DEPENDENCY_EFFECT ||
        dependency->separation_source !=
            LOOM_LOW_SCHEDULE_SEPARATION_SOURCE_EVENT_PAIR) {
      continue;
    }
    found_read_requirement |= dependency->minimum_issue_separation_cycles == 2;
    found_write_requirement |= dependency->minimum_issue_separation_cycles == 1;
  }
  EXPECT_TRUE(found_read_requirement);
  EXPECT_TRUE(found_write_requirement);
}

TEST_F(LowEmissionFrameTest, EffectTimingCrossesCfgBackedge) {
  ModulePtr module = ParseModule(R"(
low.func.def target<test.low.core> @loop_effect_edge(%condition: reg<test.i32>, %address: reg<test.ptr>) -> (reg<test.i32 x4>) asm {
  low.br ^loop
^loop:
  %loaded = test.load.v4i32 %address
  test.barrier
  low.cond_br %condition, ^loop, ^exit : reg<test.i32>
^exit:
  return %loaded
}
)");
  loom_low_emission_frame_t frame = {};
  IREE_ASSERT_OK(BuildFrame(module.get(), {}, &frame,
                            LOOM_LOW_SCHEDULE_STRATEGY_SOURCE_PRIORITY));

  ASSERT_EQ(frame.schedule.node_count, 5u);
  EXPECT_TRUE(loom_low_cond_br_isa(frame.schedule.nodes[3].op));
  EXPECT_EQ(frame.schedule.nodes[3].issue_cycle,
            frame.schedule.nodes[2].issue_cycle + 2u);

  const loom_low_schedule_dependency_t* backedge_dependency = nullptr;
  for (iree_host_size_t i = 0; i < frame.schedule.dependencies.count; ++i) {
    const loom_low_schedule_dependency_t* dependency =
        loom_low_schedule_dependency_graph_at(&frame.schedule.dependencies, i);
    if (dependency->producer_node == 2 && dependency->consumer_node == 3 &&
        dependency->kind == LOOM_LOW_SCHEDULE_DEPENDENCY_EFFECT &&
        dependency->separation_source ==
            LOOM_LOW_SCHEDULE_SEPARATION_SOURCE_EVENT_PAIR) {
      backedge_dependency = dependency;
      break;
    }
  }
  ASSERT_NE(backedge_dependency, nullptr);
  EXPECT_EQ(backedge_dependency->minimum_issue_separation_cycles, 2);
}

TEST_F(LowEmissionFrameTest, StructuralModelCarriesNativePacketTiming) {
  ModulePtr module = ParseModule();
  const loom_low_schedule_structural_model_t models[] = {
      {
          .op_kind = LOOM_OP_LOW_STORAGE_ADDRESS,
          .result_reg_class_id = TEST_LOW_CORE_REG_CLASS_ID_TEST_PTR,
          .schedule_descriptor_ordinal =
              TEST_LOW_CORE_DESCRIPTOR_REF_TEST_ADD_I32,
      },
  };
  loom_low_emission_frame_t frame = {};
  IREE_ASSERT_OK(
      BuildFrame(module.get(), {models, IREE_ARRAYSIZE(models)}, &frame));

  const loom_low_descriptor_set_t* descriptor_set =
      loom_test_low_core_descriptor_set();
  const loom_low_descriptor_view_t* schedule_descriptor_view =
      loom_low_descriptor_set_descriptor_view_at(
          descriptor_set, TEST_LOW_CORE_DESCRIPTOR_REF_TEST_ADD_I32);
  const loom_low_schedule_class_t* expected_schedule_class =
      &descriptor_set
           ->schedule_classes[schedule_descriptor_view->schedule_class_id];
  ASSERT_EQ(expected_schedule_class->latency_cycles, 1u);
  ASSERT_EQ(expected_schedule_class->minimum_issue_separation_cycles, 1);
  ASSERT_EQ(expected_schedule_class->issue_use_count, 1u);

  const loom_low_schedule_node_t* address_node =
      FindNode(frame, LOOM_OP_LOW_STORAGE_ADDRESS);
  const loom_low_schedule_node_t* load_node = FindNode(frame, LOOM_OP_LOW_OP);
  ASSERT_NE(address_node, nullptr);
  ASSERT_NE(load_node, nullptr);
  EXPECT_EQ(address_node->kind, LOOM_LOW_SCHEDULE_NODE_STRUCTURAL);
  EXPECT_EQ(address_node->descriptor, nullptr);
  EXPECT_EQ(address_node->source_descriptor, nullptr);
  EXPECT_EQ(address_node->schedule_class_id,
            schedule_descriptor_view->schedule_class_id);
  EXPECT_EQ(address_node->schedule_class, expected_schedule_class);
  EXPECT_GE(load_node->issue_cycle,
            address_node->issue_cycle +
                expected_schedule_class->minimum_issue_separation_cycles);

  const loom_low_schedule_model_summary_t* model_summary = nullptr;
  for (iree_host_size_t i = 0; i < frame.schedule.model_summary_count; ++i) {
    if (frame.schedule.model_summaries[i].schedule_class_id ==
        schedule_descriptor_view->schedule_class_id) {
      model_summary = &frame.schedule.model_summaries[i];
      break;
    }
  }
  ASSERT_NE(model_summary, nullptr);
  EXPECT_EQ(model_summary->use_count, 1u);

  const loom_low_issue_use_t* issue_use =
      &descriptor_set->issue_uses[expected_schedule_class->issue_use_start];
  const loom_low_schedule_resource_summary_t* resource_summary = nullptr;
  for (iree_host_size_t i = 0; i < frame.schedule.resource_summary_count; ++i) {
    if (frame.schedule.resource_summaries[i].resource_id ==
        issue_use->resource_id) {
      resource_summary = &frame.schedule.resource_summaries[i];
      break;
    }
  }
  ASSERT_NE(resource_summary, nullptr);
  EXPECT_EQ(resource_summary->use_count, 1u);
}

TEST_F(LowEmissionFrameTest, RejectsInvalidStructuralModelDescriptor) {
  ModulePtr module = ParseModule();
  const loom_low_descriptor_set_t* descriptor_set =
      loom_test_low_core_descriptor_set();
  const loom_low_schedule_structural_model_t models[] = {
      {
          .op_kind = LOOM_OP_LOW_STORAGE_ADDRESS,
          .result_reg_class_id = TEST_LOW_CORE_REG_CLASS_ID_TEST_PTR,
          .schedule_descriptor_ordinal = descriptor_set->descriptor_count,
      },
  };
  loom_low_emission_frame_t frame = {};
  IREE_EXPECT_STATUS_IS(
      IREE_STATUS_FAILED_PRECONDITION,
      BuildFrame(module.get(), {models, IREE_ARRAYSIZE(models)}, &frame));
}

TEST_F(LowEmissionFrameTest, RejectsOverlappingStructuralModels) {
  ModulePtr module = ParseModule();
  const loom_low_schedule_structural_model_t models[] = {
      {
          .op_kind = LOOM_OP_LOW_STORAGE_ADDRESS,
          .result_reg_class_id = LOOM_LOW_REG_CLASS_NONE,
          .schedule_descriptor_ordinal =
              TEST_LOW_CORE_DESCRIPTOR_REF_TEST_ADD_I32,
      },
      {
          .op_kind = LOOM_OP_LOW_STORAGE_ADDRESS,
          .result_reg_class_id = TEST_LOW_CORE_REG_CLASS_ID_TEST_PTR,
          .schedule_descriptor_ordinal =
              TEST_LOW_CORE_DESCRIPTOR_REF_TEST_CONST_I32,
      },
  };
  loom_low_emission_frame_t frame = {};
  IREE_EXPECT_STATUS_IS(
      IREE_STATUS_FAILED_PRECONDITION,
      BuildFrame(module.get(), {models, IREE_ARRAYSIZE(models)}, &frame));
}

TEST_F(LowEmissionFrameTest,
       RematerializationFreesCompleteOverlappingPhysicalView) {
  ModulePtr module = ParseModule(R"(
low.func.def target<test.low.core> @rematerialize_overlapping_view() -> (reg<test.packed.wide x2>, reg<test.i32>) asm {
  %block0 = test.const.explicit32 0
  %block1 = test.const.explicit32 1
  %block2 = test.const.explicit32 2
  %block3 = test.const.explicit32 3
  %seed = test.const.packed.narrow 4
  %wide = test.packing.expand %seed
  %use0 = copy %block0 : reg<test.explicit32> -> reg<test.i32>
  %use1 = copy %block1 : reg<test.explicit32> -> reg<test.i32>
  %use2 = copy %block2 : reg<test.explicit32> -> reg<test.i32>
  %use3 = copy %block3 : reg<test.explicit32> -> reg<test.i32>
  %sum01 = test.add.i32 %use0, %use1
  %sum23 = test.add.i32 %use2, %use3
  %sum = test.add.i32 %sum01, %sum23
  return %wide, %sum
}
)");
  loom_low_planning_statistics_t statistics = {};
  loom_low_emission_frame_t frame = {};
  IREE_ASSERT_OK(BuildSpillFreeFrame(module.get(), &statistics, &frame));

  EXPECT_EQ(frame.schedule.error_count, 0u);
  EXPECT_EQ(frame.allocation.error_count, 0u);
  EXPECT_EQ(frame.allocation.spill_count, 0u);
  EXPECT_EQ(frame.allocation.spill_plan_count, 0u);
  EXPECT_GT(statistics.repair.rematerialized_operand_count, 0u);
}

TEST_F(LowEmissionFrameTest, RematerializedClonesAreTerminalRepairProducts) {
  ModulePtr module = ParseModule(R"(
low.func.def target<test.low.core> @rematerialize_once(%a: reg<test.i32>, %b: reg<test.i32>, %c: reg<test.i32>) -> (reg<test.i32>) asm {
  %derived = test.rematerialize.i32 %c
  %ab = test.add.i32 %a, %b
  %use0 = test.add.i32 %ab, %derived
  %use1 = test.add.i32 %c, %derived
  %result = test.add.i32 %use0, %use1
  return %result
}
)");
  loom_low_emission_frame_t frame = {};
  IREE_ASSERT_OK(BuildFrame(module.get(), {}, &frame));

  const loom_string_id_t derived_name =
      loom_module_lookup_string(module.get(), IREE_SV("derived"));
  ASSERT_NE(derived_name, LOOM_STRING_ID_INVALID);
  loom_value_id_t derived_value_id = LOOM_VALUE_ID_INVALID;
  for (iree_host_size_t i = 0; i < module->values.count; ++i) {
    if (loom_module_value(module.get(), (loom_value_id_t)i)->name_id ==
        derived_name) {
      derived_value_id = (loom_value_id_t)i;
      break;
    }
  }
  ASSERT_NE(derived_value_id, LOOM_VALUE_ID_INVALID);

  const iree_host_size_t original_value_count = module->values.count;
  iree_bitmap_t candidate_values = {
      .bit_count = original_value_count,
  };
  const iree_host_size_t candidate_word_count =
      iree_bitmap_calculate_words(candidate_values.bit_count);
  IREE_ASSERT_OK(iree_arena_allocate_array(&arena_, candidate_word_count,
                                           sizeof(*candidate_values.words),
                                           (void**)&candidate_values.words));
  iree_bitmap_set_all(candidate_values);

  loom_low_value_rematerialization_result_t result = {};
  IREE_ASSERT_OK(loom_low_rematerialize_value_uses(
      module.get(), &frame.schedule.target, candidate_values, derived_value_id,
      &arena_, &result));
  EXPECT_EQ(result.value_id, derived_value_id);
  EXPECT_EQ(result.cloned_packet_count, 2u);
  EXPECT_EQ(result.rewritten_operand_count, 2u);
  ASSERT_EQ(module->values.count, original_value_count + 2);

  for (iree_host_size_t i = original_value_count; i < module->values.count;
       ++i) {
    loom_low_value_rematerialization_result_t recursive_result = {};
    IREE_ASSERT_OK(loom_low_rematerialize_value_uses(
        module.get(), &frame.schedule.target, candidate_values,
        (loom_value_id_t)i, &arena_, &recursive_result));
    EXPECT_EQ(recursive_result.value_id, LOOM_VALUE_ID_INVALID);
    EXPECT_EQ(recursive_result.rewritten_operand_count, 0u);
  }
}

}  // namespace
}  // namespace loom
