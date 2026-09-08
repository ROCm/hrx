// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/tooling/execution/hal/device_provider.h"

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
