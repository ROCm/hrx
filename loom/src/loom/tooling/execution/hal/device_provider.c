// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/tooling/execution/hal/device_provider.h"

#include "loom/tooling/execution/hal/runtime.h"

iree_status_t loom_device_provider_select_compatible_target(
    const loom_device_provider_t* provider,
    const struct loom_run_hal_runtime_t* runtime,
    const loom_target_facts_t* target_requirement, iree_allocator_t allocator,
    loom_device_target_t* out_target) {
  IREE_ASSERT_ARGUMENT(provider);
  IREE_ASSERT_ARGUMENT(runtime);
  IREE_ASSERT_ARGUMENT(out_target);
  *out_target = (loom_device_target_t){0};

  if (provider->select_compatible_target == NULL) {
    return iree_make_status(
        IREE_STATUS_INVALID_ARGUMENT,
        "device provider '%.*s' is missing required compatible target "
        "selection hook",
        (int)provider->artifact_provider->name.size,
        provider->artifact_provider->name.data);
  }

  return provider->select_compatible_target(
      provider, runtime, target_requirement, allocator, out_target);
}

static iree_status_t loom_device_provider_validate_profile_target(
    const loom_run_hal_runtime_t* runtime,
    const loom_target_profile_t* requested_profile,
    const loom_device_target_t* target) {
  if (target->artifact_target.target_profile != requested_profile) {
    return iree_make_status(IREE_STATUS_FAILED_PRECONDITION,
                            "device provider changed the requested target "
                            "profile");
  }

  const iree_hal_device_spec_t* device_spec =
      runtime->device ? iree_hal_device_spec(runtime->device) : NULL;
  if (device_spec == NULL) {
    return iree_make_status(IREE_STATUS_FAILED_PRECONDITION,
                            "device provider requires immutable device facts");
  }
  if (iree_hal_device_spec_executable_target_ordinal(
          device_spec, target->executable_target) == IREE_HOST_SIZE_MAX) {
    return iree_make_status(IREE_STATUS_FAILED_PRECONDITION,
                            "device provider returned a foreign executable "
                            "target");
  }
  if (!iree_string_view_equal(target->artifact_target.target_key,
                              target->executable_target->target_key)) {
    return iree_make_status(IREE_STATUS_FAILED_PRECONDITION,
                            "device provider returned mismatched artifact and "
                            "executable target keys");
  }
  if (runtime->dispatch_queue == NULL) {
    return iree_make_status(IREE_STATUS_FAILED_PRECONDITION,
                            "device provider requires a dispatch queue");
  }

  const iree_hal_queue_family_ordinal_t queue_family_ordinal =
      iree_hal_queue_family_ordinal(
          iree_hal_queue_family(runtime->dispatch_queue));
  const iree_hal_device_queue_spec_t* queue_spec =
      iree_hal_device_spec_queues(device_spec);
  if (queue_family_ordinal >= queue_spec->family_count) {
    return iree_make_status(
        IREE_STATUS_FAILED_PRECONDITION,
        "device provider dispatch queue family ordinal %u is invalid",
        queue_family_ordinal);
  }
  const iree_hal_physical_device_affinity_t queue_affinity =
      queue_spec->families[queue_family_ordinal].physical_device_affinity;
  if (!iree_all_bits_set(target->executable_target->physical_device_affinity,
                         queue_affinity)) {
    return iree_make_status(
        IREE_STATUS_INCOMPATIBLE,
        "device target does not cover dispatch queue physical device affinity");
  }
  return iree_ok_status();
}

iree_status_t loom_device_provider_select_profile_target(
    const loom_device_provider_t* provider,
    const loom_run_hal_runtime_t* runtime,
    const loom_target_profile_t* target_profile,
    loom_device_target_t* out_target) {
  IREE_ASSERT_ARGUMENT(provider);
  IREE_ASSERT_ARGUMENT(runtime);
  IREE_ASSERT_ARGUMENT(out_target);
  *out_target = (loom_device_target_t){0};

  if (target_profile == NULL) {
    return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                            "device target profile is required");
  }
  if (provider->artifact_provider == NULL ||
      provider->artifact_provider->target_profile_type == NULL) {
    return iree_make_status(IREE_STATUS_FAILED_PRECONDITION,
                            "device provider has no target profile type");
  }
  if (target_profile->type !=
      provider->artifact_provider->target_profile_type) {
    return iree_make_status(
        IREE_STATUS_INVALID_ARGUMENT,
        "device provider '%.*s' requires target family '%.*s'; got '%.*s'",
        (int)provider->artifact_provider->name.size,
        provider->artifact_provider->name.data,
        (int)provider->artifact_provider->target_profile_type->name.size,
        provider->artifact_provider->target_profile_type->name.data,
        target_profile->type ? (int)target_profile->type->name.size : 0,
        target_profile->type ? target_profile->type->name.data : "");
  }
  if (provider->select_profile_target == NULL) {
    return iree_make_status(
        IREE_STATUS_INVALID_ARGUMENT,
        "device provider '%.*s' does not support explicit target selection",
        (int)provider->artifact_provider->name.size,
        provider->artifact_provider->name.data);
  }

  iree_status_t status = provider->select_profile_target(
      provider, runtime, target_profile, out_target);
  if (iree_status_is_ok(status)) {
    status = loom_device_provider_validate_profile_target(
        runtime, target_profile, out_target);
  }
  if (!iree_status_is_ok(status)) {
    *out_target = (loom_device_target_t){0};
  }
  return status;
}

void loom_device_provider_registry_initialize_from_entries(
    const loom_device_provider_t* const* providers,
    iree_host_size_t provider_count,
    loom_device_provider_registry_t* out_registry) {
  IREE_ASSERT_ARGUMENT(out_registry);
  *out_registry = (loom_device_provider_registry_t){
      .providers = providers,
      .provider_count = provider_count,
  };
}

const loom_device_provider_t* loom_device_provider_registry_lookup_driver(
    const loom_device_provider_registry_t* registry,
    iree_string_view_t driver_name) {
  IREE_ASSERT_ARGUMENT(registry);
  for (iree_host_size_t i = 0; i < registry->provider_count; ++i) {
    const loom_device_provider_t* provider = registry->providers[i];
    if (iree_string_view_equal(provider->driver_name, driver_name)) {
      return provider;
    }
  }
  return NULL;
}
