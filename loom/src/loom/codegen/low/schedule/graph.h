// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Schedule-node and dependency-graph construction.

#ifndef LOOM_CODEGEN_LOW_SCHEDULE_GRAPH_H_
#define LOOM_CODEGEN_LOW_SCHEDULE_GRAPH_H_

#include "loom/codegen/low/schedule/context.h"

#ifdef __cplusplus
extern "C" {
#endif

iree_status_t loom_low_schedule_fill_nodes(
    loom_low_schedule_build_state_t* state);

// Projects descriptor rows into packet operand order. The borrowed index is
// valid until the next call; structural packets return NULL.
const uint16_t* loom_low_schedule_index_descriptor_operands(
    loom_low_schedule_build_state_t* state,
    const loom_low_descriptor_t* descriptor, uint16_t operand_count);

// Resolves the defining result's timing attachment, or a structural endpoint.
loom_low_schedule_dependency_endpoint_t loom_low_schedule_value_write_endpoint(
    const loom_low_schedule_build_state_t* state,
    loom_value_ordinal_t value_ordinal);

// Resolves a descriptor operand's read event, or a structural endpoint.
loom_low_schedule_dependency_endpoint_t
loom_low_schedule_descriptor_operand_read_endpoint(
    const loom_low_schedule_build_state_t* state,
    const loom_low_descriptor_t* descriptor, uint16_t descriptor_operand_index);

// Adds one same-block dependency after resolving its target timing endpoints.
iree_status_t loom_low_schedule_add_dependency(
    loom_low_schedule_build_state_t* state, uint32_t producer_node,
    uint32_t consumer_node, loom_low_schedule_dependency_kind_t kind,
    uint16_t value_operand_index,
    loom_low_schedule_dependency_endpoint_t producer_endpoint,
    loom_low_schedule_dependency_endpoint_t consumer_endpoint);

iree_status_t loom_low_schedule_build_dependencies(
    loom_low_schedule_build_state_t* state);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // LOOM_CODEGEN_LOW_SCHEDULE_GRAPH_H_
