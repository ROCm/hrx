// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/codegen/low/schedule/fixed_storage.h"

#include <inttypes.h>
#include <string.h>

#include "loom/codegen/low/allocation/storage.h"
#include "loom/codegen/low/allocation/target_constraints.h"
#include "loom/codegen/low/schedule/graph.h"

typedef struct loom_low_schedule_fixed_storage_value_t {
  // First reference in the flattened fixed-value atomic-unit map.
  uint32_t unit_start;
  // Atomic units occupied by this binding, including register-view aliases.
  uint32_t unit_count;
  // Borrowed explicit-register atomic units, or NULL for a linear namespace.
  const uint16_t* atomic_units;
  // Descriptor storage namespace shared with allocation.
  uint32_t storage_key;
  // First location in a linear namespace; unused for explicit registers.
  uint32_t location_base;
} loom_low_schedule_fixed_storage_value_t;

// Compresses arbitrary storage keys into dense unit IDs. Eight byte-radix
// passes bound the work independently of key magnitude or ordering; uniform
// bytes need no scatter. Both index buffers are allocated at their final size.
static uint32_t loom_low_schedule_fixed_storage_index(const uint64_t* keys,
                                                      uint32_t count,
                                                      uint32_t* indices,
                                                      uint32_t* scratch) {
  for (uint32_t i = 0; i < count; ++i) scratch[i] = i;
  uint32_t* source = scratch;
  uint32_t* destination = indices;
  for (uint32_t shift = 0; shift < 64; shift += 8) {
    uint32_t counts[256] = {0};
    for (uint32_t i = 0; i < count; ++i) {
      ++counts[(keys[source[i]] >> shift) & 255];
    }
    if (counts[(keys[source[0]] >> shift) & 255] == count) continue;
    uint32_t offset = 0;
    for (uint32_t i = 0; i < IREE_ARRAYSIZE(counts); ++i) {
      const uint32_t bucket_count = counts[i];
      counts[i] = offset;
      offset += bucket_count;
    }
    for (uint32_t i = 0; i < count; ++i) {
      const uint32_t index = source[i];
      destination[counts[(keys[index] >> shift) & 255]++] = index;
    }
    uint32_t* temporary = source;
    source = destination;
    destination = temporary;
  }
  if (source != scratch) memcpy(scratch, source, count * sizeof(*scratch));
  uint32_t unit_count = 0;
  for (uint32_t i = 0; i < count; ++i) {
    if (i == 0 || keys[scratch[i]] != keys[scratch[i - 1]]) ++unit_count;
    indices[scratch[i]] = unit_count - 1;
  }
  return unit_count;
}

static iree_status_t loom_low_schedule_fixed_storage_resolve(
    const loom_low_schedule_build_state_t* state,
    const loom_low_allocation_fixed_value_t* fixed_value,
    loom_value_ordinal_t ordinal,
    loom_low_schedule_fixed_storage_value_t* out_value) {
  *out_value = (loom_low_schedule_fixed_storage_value_t){0};
  const loom_low_descriptor_set_t* descriptor_set =
      state->target.descriptor_set;
  const loom_low_schedule_value_record_t* value = &state->values[ordinal];
  if (value->register_class_id == LOOM_LOW_REG_CLASS_NONE ||
      fixed_value->location_count == 0 ||
      fixed_value->location_count != value->unit_count) {
    return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                            "fixed value %" PRIu32
                            " has incompatible register units",
                            fixed_value->value_id);
  }
  const loom_low_reg_class_t* reg_class =
      &descriptor_set->reg_classes[value->register_class_id];
  if (fixed_value->location_kind !=
      loom_low_allocation_storage_reg_class_location_kind(reg_class)) {
    return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                            "fixed value %" PRIu32
                            " has an incompatible location kind",
                            fixed_value->value_id);
  }
  out_value->storage_key =
      loom_low_reg_class_storage_key(descriptor_set, value->register_class_id);
  if (loom_low_reg_class_uses_explicit_physical_registers(reg_class)) {
    if (!loom_low_allocation_storage_explicit_physical_register_view(
            descriptor_set, value->register_class_id,
            fixed_value->location_base, fixed_value->location_count, NULL,
            NULL)) {
      return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                              "fixed value %" PRIu32
                              " has an invalid physical register view",
                              fixed_value->value_id);
    }
    const loom_low_physical_register_t* physical =
        &descriptor_set->physical_registers[fixed_value->location_base];
    out_value->atomic_units = descriptor_set->physical_register_atomic_units +
                              physical->atomic_unit_start;
    out_value->unit_count = physical->atomic_unit_count;
  } else {
    if ((uint64_t)fixed_value->location_base + fixed_value->location_count >
        UINT32_MAX) {
      return iree_make_status(IREE_STATUS_OUT_OF_RANGE,
                              "fixed value %" PRIu32
                              " exceeds the storage location domain",
                              fixed_value->value_id);
    }
    out_value->location_base = fixed_value->location_base;
    out_value->unit_count = fixed_value->location_count;
  }
  return iree_ok_status();
}

static iree_status_t loom_low_schedule_fixed_storage_add_dependency(
    loom_low_schedule_build_state_t* state, uint32_t producer_node,
    uint16_t operand_index, loom_low_schedule_dependency_endpoint_t endpoint,
    loom_value_ordinal_t next_value) {
  if (next_value == LOOM_VALUE_ORDINAL_INVALID) return iree_ok_status();
  const uint32_t writer_node = state->values[next_value].producer_node;
  if (state->nodes[writer_node].block != state->nodes[producer_node].block) {
    return iree_ok_status();
  }
  return loom_low_schedule_add_dependency(
      state, producer_node, writer_node, LOOM_LOW_SCHEDULE_DEPENDENCY_STORAGE,
      operand_index, endpoint,
      loom_low_schedule_value_write_endpoint(state, next_value));
}

iree_status_t loom_low_schedule_build_fixed_storage_dependencies(
    loom_low_schedule_build_state_t* state) {
  const iree_host_size_t fixed_count = state->options->fixed_value_count;
  if (fixed_count == 0) return iree_ok_status();
  if (fixed_count >= UINT32_MAX) {
    return iree_make_status(
        IREE_STATUS_RESOURCE_EXHAUSTED,
        "fixed binding count exceeds the local index domain");
  }
  iree_arena_allocator_t* arena = state->scratch_arena;
  uint32_t* bindings = NULL;
  IREE_RETURN_IF_ERROR(
      iree_arena_allocate_array(arena, state->value_domain->value_count,
                                sizeof(*bindings), (void**)&bindings));
  memset(bindings, 0, state->value_domain->value_count * sizeof(*bindings));
  loom_low_schedule_fixed_storage_value_t* values = NULL;
  IREE_RETURN_IF_ERROR(iree_arena_allocate_array(
      arena, fixed_count, sizeof(*values), (void**)&values));
  uint32_t reference_count = 0;
  for (uint32_t i = 0; i < fixed_count; ++i) {
    const loom_low_allocation_fixed_value_t* fixed =
        &state->options->fixed_values[i];
    const loom_value_ordinal_t ordinal = loom_local_value_domain_try_ordinal(
        state->value_domain, fixed->value_id);
    if (ordinal == LOOM_VALUE_ORDINAL_INVALID) {
      return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                              "fixed value %" PRIu32
                              " is outside the scheduled function",
                              fixed->value_id);
    }
    if (bindings[ordinal] != 0) {
      return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                              "fixed bindings %" PRIu32 " and %" PRIu32
                              " name the same value %" PRIu32,
                              bindings[ordinal] - 1, i, fixed->value_id);
    }
    bindings[ordinal] = i + 1;
    IREE_RETURN_IF_ERROR(loom_low_schedule_fixed_storage_resolve(
        state, fixed, ordinal, &values[i]));
    if (values[i].unit_count > UINT32_MAX - reference_count) {
      return iree_make_status(
          IREE_STATUS_RESOURCE_EXHAUSTED,
          "fixed binding units exceed the local index domain");
    }
    values[i].unit_start = reference_count;
    reference_count += values[i].unit_count;
  }
  uint64_t* keys = NULL;
  uint32_t* units = NULL;
  uint32_t* next_writes = NULL;
  uint32_t* successors = NULL;
  IREE_RETURN_IF_ERROR(iree_arena_allocate_array(arena, reference_count,
                                                 sizeof(*keys), (void**)&keys));
  IREE_RETURN_IF_ERROR(iree_arena_allocate_array(
      arena, reference_count, sizeof(*units), (void**)&units));
  IREE_RETURN_IF_ERROR(iree_arena_allocate_array(
      arena, reference_count, sizeof(*next_writes), (void**)&next_writes));
  IREE_RETURN_IF_ERROR(iree_arena_allocate_array(
      arena, reference_count, sizeof(*successors), (void**)&successors));
  for (uint32_t i = 0; i < fixed_count; ++i) {
    const loom_low_schedule_fixed_storage_value_t* value = &values[i];
    for (uint32_t j = 0; j < value->unit_count; ++j) {
      const uint32_t location = value->atomic_units ? value->atomic_units[j]
                                                    : value->location_base + j;
      keys[value->unit_start + j] =
          ((uint64_t)value->storage_key << 32) | location;
    }
  }
  const uint32_t unit_count = loom_low_schedule_fixed_storage_index(
      keys, reference_count, units, next_writes);
  memset(next_writes, 0xFF, unit_count * sizeof(*next_writes));
  for (uint32_t block_index = 0; block_index < state->body->block_count;
       ++block_index) {
    const loom_low_schedule_block_t* block = &state->blocks[block_index];
    const uint32_t block_end = block->node_start + block->node_count;
    // Definition order establishes each value's next overwrite. Readers may
    // appear after that overwrite in authored order and still have to precede
    // it in the legal schedule, so their dependencies are added separately.
    for (uint32_t node_index = block_end; node_index > block->node_start;) {
      const loom_low_schedule_node_t* node = &state->nodes[--node_index];
      const loom_value_ordinal_t* results =
          loom_low_schedule_node_const_result_ordinals(node);
      for (uint16_t i = 0; i < node->result_count; ++i) {
        const uint32_t binding = bindings[results[i]];
        if (binding == 0) continue;
        const loom_low_schedule_fixed_storage_value_t* value =
            &values[binding - 1];
        const loom_low_schedule_dependency_endpoint_t endpoint =
            loom_low_schedule_value_write_endpoint(state, results[i]);
        for (uint32_t j = 0; j < value->unit_count; ++j) {
          const uint32_t reference = value->unit_start + j;
          successors[reference] = next_writes[units[reference]];
          IREE_RETURN_IF_ERROR(loom_low_schedule_fixed_storage_add_dependency(
              state, node_index, LOOM_LOW_ID_NONE, endpoint,
              successors[reference]));
          next_writes[units[reference]] = results[i];
        }
      }
    }
    for (uint32_t node_index = block->node_start; node_index < block_end;
         ++node_index) {
      const loom_low_schedule_node_t* node = &state->nodes[node_index];
      const loom_value_ordinal_t* operands =
          loom_low_schedule_node_const_operand_ordinals(node);
      const uint16_t* descriptor_operands =
          loom_low_schedule_index_descriptor_operands(state, node->descriptor,
                                                      node->operand_count);
      for (uint16_t i = 0; i < node->operand_count; ++i) {
        const uint32_t binding = bindings[operands[i]];
        if (binding == 0) continue;
        const loom_low_schedule_fixed_storage_value_t* value =
            &values[binding - 1];
        const uint32_t producer = state->values[operands[i]].producer_node;
        const bool is_local = producer != LOOM_LOW_SCHEDULE_NODE_NONE &&
                              state->nodes[producer].block == node->block;
        const loom_low_schedule_dependency_endpoint_t endpoint =
            loom_low_schedule_descriptor_operand_read_endpoint(
                state, node->descriptor,
                descriptor_operands ? descriptor_operands[i]
                                    : LOOM_LOW_ID_NONE);
        for (uint32_t j = 0; j < value->unit_count; ++j) {
          const uint32_t reference = value->unit_start + j;
          const loom_value_ordinal_t successor =
              is_local ? successors[reference] : next_writes[units[reference]];
          IREE_RETURN_IF_ERROR(loom_low_schedule_fixed_storage_add_dependency(
              state, node_index, i, endpoint, successor));
        }
      }
    }
  }
  return iree_ok_status();
}
