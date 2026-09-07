// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "amdf/amdf.h"

static void amdf_example_report_status(const char* operation,
                                       amdf_status_t status) {
  fprintf(stderr, "%s failed: domain=%" PRIu32 " code=%" PRIu32 "\n", operation,
          amdf_status_domain(status), amdf_status_code(status));
}

static const char* amdf_example_engine_name(amdf_engine_kind_t engine_kind) {
  switch (engine_kind) {
    case AMDF_ENGINE_KIND_GPU:
      return "gpu";
    case AMDF_ENGINE_KIND_XDNA:
      return "xdna";
    default:
      return "unknown";
  }
}

int main(void) {
  const amdf_api_t* api = NULL;
  amdf_status_t status =
      amdf_query_api(AMDF_ABI_VERSION_1, AMDF_ABI_VERSION_LATEST, &api);
  if (!amdf_status_is_ok(status)) {
    amdf_example_report_status("amdf_query_api", status);
    return 1;
  }

  const amdf_instance_create_info_t create_info = {
      .type = AMDF_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .structure_size = sizeof(create_info),
  };
  amdf_instance_t* instance = NULL;
  status = api->instance_create(&create_info, &instance);
  if (!amdf_status_is_ok(status)) {
    amdf_example_report_status("instance_create", status);
    return 1;
  }

  uint32_t endpoint_count = 0;
  status = api->endpoint_enumerate(instance, 0, NULL, &endpoint_count);
  if (!amdf_status_is_ok(status)) {
    amdf_example_report_status("endpoint_enumerate", status);
  }
  if (amdf_status_is_ok(status)) {
    printf("Found %" PRIu32 " AMD endpoint(s).\n", endpoint_count);
  }

  amdf_endpoint_summary_t* summaries = NULL;
  if (amdf_status_is_ok(status) && endpoint_count != 0) {
    summaries =
        (amdf_endpoint_summary_t*)calloc(endpoint_count, sizeof(*summaries));
    if (summaries == NULL) {
      status = amdf_make_api_status(AMDF_STATUS_CODE_RESOURCE_EXHAUSTED);
      amdf_example_report_status("endpoint summary allocation", status);
    }
  }
  if (amdf_status_is_ok(status) && endpoint_count != 0) {
    status = api->endpoint_enumerate(instance, endpoint_count, summaries,
                                     &endpoint_count);
    if (!amdf_status_is_ok(status)) {
      amdf_example_report_status("endpoint_enumerate", status);
    }
  }

  for (uint32_t i = 0; amdf_status_is_ok(status) && i < endpoint_count; ++i) {
    amdf_endpoint_t* endpoint = NULL;
    status = api->endpoint_open(instance, &summaries[i].id, &endpoint);
    if (!amdf_status_is_ok(status)) {
      amdf_example_report_status("endpoint_open", status);
      break;
    }

    amdf_endpoint_info_t info = {
        .type = AMDF_STRUCTURE_TYPE_ENDPOINT_INFO,
        .structure_size = sizeof(info),
    };
    status = api->endpoint_query_info(endpoint, &info);
    if (amdf_status_is_ok(status)) {
      printf("[%" PRIu32 "] %s pci=%04" PRIx32 ":%04" PRIx32
             " revision=%02" PRIx32 " engine=%s flags=0x%08" PRIx32 "\n",
             i, info.name, info.pci.vendor_id, info.pci.device_id,
             info.pci.revision_id, amdf_example_engine_name(info.engine_kind),
             info.type_flags);
    } else {
      amdf_example_report_status("endpoint_query_info", status);
    }

    const amdf_status_t close_status = api->endpoint_close(endpoint);
    if (!amdf_status_is_ok(close_status)) {
      amdf_example_report_status("endpoint_close", close_status);
      status = close_status;
    }
  }

  free(summaries);
  const amdf_status_t destroy_status = api->instance_destroy(instance);
  if (!amdf_status_is_ok(destroy_status)) {
    amdf_example_report_status("instance_destroy", destroy_status);
    status = destroy_status;
  }
  return amdf_status_is_ok(status) ? 0 : 1;
}
