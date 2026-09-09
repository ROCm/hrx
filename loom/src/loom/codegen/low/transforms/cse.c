// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/codegen/low/transforms/cse.h"

#include <string.h>

#include "loom/codegen/low/descriptor_traits.h"
#include "loom/codegen/low/function.h"
#include "loom/codegen/low/pipeline/pass_environment.h"
#include "loom/codegen/low/target_binding.h"
#include "loom/ir/module.h"
#include "loom/ops/op_defs.h"
#include "loom/target/function_version.h"
#include "loom/transforms/cleanup/expression_scope.h"

#define LOOM_LOW_CSE_STATISTICS(V, statistics_type)                    \
  V(statistics_type, expressions_eliminated, "expressions-eliminated", \
    "Number of redundant machine expressions removed.")

LOOM_PASS_STATISTICS_DEFINE(loom_low_cse_statistics, loom_low_cse_statistics_t,
                            LOOM_LOW_CSE_STATISTICS)

static const loom_pass_info_t loom_low_cse_pass_info_storage = {
    .name = IREE_SVL("low-cse"),
    .description = IREE_SVL("Eliminate redundant machine expressions."),
    .kind = LOOM_PASS_FUNCTION,
    .statistic_layout = &loom_low_cse_statistics_layout,
};

const loom_pass_info_t* loom_low_cse_pass_info(void) {
  return &loom_low_cse_pass_info_storage;
}

//===----------------------------------------------------------------------===//
// Architectural state and materialization policy
//===----------------------------------------------------------------------===//

enum loom_low_cse_packet_flag_bits_e {
  // Whether the packet depends on architectural state not explicit in SSA.
  LOOM_LOW_CSE_PACKET_FLAG_IMPLICIT_STATE = 1u << 0,
  // Whether the packet writes architectural state and must remain distinct.
  LOOM_LOW_CSE_PACKET_FLAG_WRITES_STATE = 1u << 1,
  // Operand-free replayable materialization into physical register storage.
  LOOM_LOW_CSE_PACKET_FLAG_MATERIALIZATION = 1u << 2,
};
typedef uint8_t loom_low_cse_packet_flags_t;

typedef struct loom_low_cse_packet_identity_t {
  // Most recent implicit-state write that prevents reuse of an older producer.
  uint32_t minimum_epoch;
  // Architectural state and physical materialization classification.
  loom_low_cse_packet_flags_t flags;
} loom_low_cse_packet_identity_t;

// State epochs are indexed by exact register-class identity. No candidate list
// is scanned on a write and unrelated state classes never invalidate each
// other.
static loom_low_cse_packet_identity_t loom_low_cse_observe_packet(
    const loom_low_descriptor_set_t* descriptor_set,
    const loom_expression_cursor_t* cursor, uint32_t* state_write_epochs) {
  loom_low_cse_packet_identity_t identity = {0};
  loom_low_descriptor_packet_t packet = {0};
  loom_low_descriptor_packet_initialize(descriptor_set, cursor->op, &packet);
  if (packet.kind == LOOM_LOW_DESCRIPTOR_PACKET_NONE) return identity;

  const loom_low_descriptor_t* descriptor = packet.descriptor;
  for (uint16_t i = 0; i < descriptor->operand_count; ++i) {
    const loom_low_operand_t* operand =
        &descriptor_set->operands[descriptor->operand_start + i];
    if (iree_any_bit_set(operand->flags,
                         LOOM_LOW_OPERAND_FLAG_SCHEDULE_ONLY_STATE)) {
      continue;
    }
    const loom_low_operand_flags_t state_flags =
        operand->flags &
        (LOOM_LOW_OPERAND_FLAG_STATE_READ | LOOM_LOW_OPERAND_FLAG_STATE_WRITE);
    if (!state_flags) continue;
    // Python validates exactly one register class for architectural state.
    const uint16_t class_id =
        descriptor_set->reg_class_alts[operand->reg_class_alt_start]
            .reg_class_id;
    if (iree_any_bit_set(state_flags, LOOM_LOW_OPERAND_FLAG_STATE_WRITE)) {
      state_write_epochs[class_id] = cursor->epoch;
      identity.flags |= LOOM_LOW_CSE_PACKET_FLAG_WRITES_STATE;
    }
    const bool has_explicit_packet_value =
        i >= descriptor->result_count &&
        loom_low_operand_role_is_packet_operand(operand->role);
    if (iree_any_bit_set(state_flags, LOOM_LOW_OPERAND_FLAG_STATE_READ) &&
        !has_explicit_packet_value) {
      identity.flags |= LOOM_LOW_CSE_PACKET_FLAG_IMPLICIT_STATE;
      identity.minimum_epoch =
          iree_max(identity.minimum_epoch, state_write_epochs[class_id]);
    }
  }

  if (cursor->op->operand_count == 0 && cursor->op->result_count == 1 &&
      (packet.kind == LOOM_LOW_DESCRIPTOR_PACKET_CONST ||
       packet.kind == LOOM_LOW_DESCRIPTOR_PACKET_OP) &&
      loom_low_descriptor_result_can_rematerialize(descriptor_set, descriptor,
                                                   0)) {
    const loom_low_register_type_resolver_t resolver =
        loom_low_register_type_resolver_for_descriptor_set(descriptor_set);
    if (loom_low_register_type_resolver_has_class_flags(
            &resolver,
            loom_module_value_type(cursor->module,
                                   loom_op_results(cursor->op)[0]),
            LOOM_LOW_REG_CLASS_FLAG_PHYSICAL)) {
      identity.flags |= LOOM_LOW_CSE_PACKET_FLAG_MATERIALIZATION;
    }
  }
  return identity;
}

// Physical materializations may share an already-live value, but joining
// disjoint lifetimes needlessly occupies registers between independent uses.
// Each new producer's use list is inspected once. CSE only erases the current
// operation: it never inserts or reorders operations, so this cached ordinal
// remains valid until the traversal reaches it, even when that use is erased.
// Nonlocal uses require CFG liveness and remain outside this local policy.
static uint64_t loom_low_cse_materialization_last_use(
    const loom_module_t* module, const loom_op_t* op) {
  const loom_value_id_t value_id = loom_op_results((loom_op_t*)op)[0];
  if (loom_module_value_has_type_uses(module, value_id) ||
      loom_module_value_has_predicate_attribute_uses(module, value_id)) {
    return 0;
  }
  const loom_value_t* value = loom_module_value(module, value_id);
  uint64_t last_use = 0;
  const loom_use_t* use = NULL;
  loom_value_for_each_use(value, use) {
    const loom_op_t* user_op = loom_use_user_op(*use);
    if (user_op->parent_block != op->parent_block) return 0;
    last_use = iree_max(last_use, user_op->block_ordinal);
  }
  return last_use;
}

static iree_status_t loom_low_cse_region(
    loom_pass_t* pass, loom_module_t* module, loom_region_t* region,
    const loom_low_descriptor_set_t* descriptor_set,
    iree_arena_allocator_t* arena) {
  loom_expression_walk_t* walk = NULL;
  IREE_RETURN_IF_ERROR(
      loom_expression_walk_initialize(module, region, arena, &walk));
  uint32_t* state_write_epochs = NULL;
  IREE_RETURN_IF_ERROR(iree_arena_allocate_array(
      arena, descriptor_set->reg_class_count, sizeof(*state_write_epochs),
      (void**)&state_write_epochs));
  memset(state_write_epochs, 0,
         descriptor_set->reg_class_count * sizeof(*state_write_epochs));
  loom_low_cse_statistics_t* statistics = loom_low_cse_statistics(pass);
  loom_expression_barriers_t barriers = {0};
  iree_status_t status = iree_ok_status();
  while (iree_status_is_ok(status)) {
    loom_expression_cursor_t cursor;
    status = loom_expression_walk_next(walk, &cursor);
    if (!iree_status_is_ok(status) || !cursor.op) break;
    const loom_low_cse_packet_identity_t identity = loom_low_cse_observe_packet(
        descriptor_set, &cursor, state_write_epochs);
    const uint32_t minimum_epoch =
        iree_max(identity.minimum_epoch,
                 loom_expression_observe_barriers(&cursor, &barriers));
    if (iree_any_bit_set(identity.flags,
                         LOOM_LOW_CSE_PACKET_FLAG_WRITES_STATE) ||
        !loom_expression_is_reusable(&cursor))
      continue;

    const bool is_materialization = iree_any_bit_set(
        identity.flags, LOOM_LOW_CSE_PACKET_FLAG_MATERIALIZATION);
    loom_expression_entry_t candidate = {
        .op = cursor.op,
        .hash = loom_expression_hash(module, cursor.op),
        .epoch = cursor.epoch,
    };
    if (is_materialization) {
      candidate.data = loom_low_cse_materialization_last_use(module, cursor.op);
      if (!candidate.data) continue;
    }
    loom_expression_lookup_flags_t flags = 0;
    if (is_materialization) {
      flags = LOOM_EXPRESSION_LOOKUP_FLAG_LOCAL;
    } else if (iree_any_bit_set(identity.flags,
                                LOOM_LOW_CSE_PACKET_FLAG_IMPLICIT_STATE) ||
               !iree_any_bit_set(cursor.traits, LOOM_TRAIT_PURE)) {
      flags = LOOM_EXPRESSION_LOOKUP_FLAG_STATEFUL;
    }
    loom_expression_entry_t* existing =
        loom_expression_scope_find(&cursor, candidate.hash, flags);
    if (existing && existing->epoch >= minimum_epoch &&
        (!is_materialization || existing->data >= cursor.op->block_ordinal)) {
      if (is_materialization) {
        existing->data = iree_max(existing->data, candidate.data);
      }
      status = loom_expression_replace(module, cursor.op, existing->op);
      if (iree_status_is_ok(status)) {
        loom_pass_mark_changed(pass);
        ++statistics->expressions_eliminated;
      }
    } else {
      loom_expression_scope_insert(&cursor, candidate);
    }
  }
  return status;
}

iree_status_t loom_low_cse_run(loom_pass_t* pass, loom_module_t* module,
                               loom_func_like_t function) {
  if (!loom_low_function_def_isa(function.op) ||
      !loom_func_like_body(function)) {
    return iree_ok_status();
  }
  const loom_low_pass_capability_t* capability =
      loom_low_pass_capability_from_pass(pass);
  const loom_low_descriptor_registry_t* registry =
      loom_low_pass_capability_descriptor_registry(capability);
  loom_symbol_fact_table_t symbol_facts = {0};
  loom_symbol_fact_table_initialize(&symbol_facts, pass->arena);
  loom_low_resolved_target_t target = {0};
  IREE_RETURN_IF_ERROR(loom_low_resolve_function_target(
      module, &symbol_facts, function.op,
      loom_target_function_version_target_facts(pass->function_version),
      registry, pass->diagnostic_emitter, &target));
  if (!target.descriptor_set) return iree_ok_status();

  iree_arena_allocator_t arena;
  iree_arena_initialize(pass->arena->block_pool, &arena);
  iree_status_t status = iree_ok_status();
  for (uint8_t i = 0;
       i < loom_func_like_region_count(function) && iree_status_is_ok(status);
       ++i) {
    loom_region_t* region = loom_func_like_region(function, i);
    if (!region) continue;
    iree_arena_reset(&arena);
    status = loom_low_cse_region(pass, module, region, target.descriptor_set,
                                 &arena);
  }
  iree_arena_deinitialize(&arena);
  return status;
}
