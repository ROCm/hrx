// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/gpu/endpoint_profile.h"

#include <stddef.h>

bool amdf_gpu_endpoint_profile_initialize(
    const amdf_gpu_endpoint_properties_t* properties,
    amdf_gpu_endpoint_profile_t* out_profile) {
  if (properties == NULL || out_profile == NULL ||
      properties->gfx_ip.major == 0 ||
      (properties->compute.wavefront_size != 32 &&
       properties->compute.wavefront_size != 64) ||
      properties->compute.compute_unit_count == 0 ||
      properties->compute.maximum_wave_count_per_compute_unit == 0 ||
      properties->compute.maximum_scratch_wave_count_per_compute_unit == 0 ||
      properties->compute.local_data_share_byte_length == 0 ||
      properties->topology.xcc_count == 0 ||
      properties->topology.shader_engine_count_per_xcc == 0) {
    return false;
  }

  if (properties->compute.maximum_scratch_wave_count_per_compute_unit >
      properties->compute.maximum_wave_count_per_compute_unit) {
    return false;
  }

  amdf_gpu_endpoint_profile_t profile = {0};
  profile.info.type = AMDF_STRUCTURE_TYPE_GPU_ENDPOINT_INFO;
  profile.info.structure_size = sizeof(profile.info);
  profile.info.gfx_ip.major = properties->gfx_ip.major;
  profile.info.gfx_ip.minor = properties->gfx_ip.minor;
  profile.info.gfx_ip.stepping = properties->gfx_ip.stepping;
  profile.info.asic_revision = properties->asic_revision;
  profile.info.compute.wavefront_size = properties->compute.wavefront_size;
  profile.info.compute.compute_unit_count =
      properties->compute.compute_unit_count;
  profile.info.compute.maximum_wave_count_per_compute_unit =
      properties->compute.maximum_wave_count_per_compute_unit;
  profile.info.compute.maximum_scratch_wave_count_per_compute_unit =
      properties->compute.maximum_scratch_wave_count_per_compute_unit;
  profile.info.compute.local_data_share_byte_length =
      properties->compute.local_data_share_byte_length;
  profile.info.topology.xcc_count = properties->topology.xcc_count;
  profile.info.topology.shader_engine_count_per_xcc =
      properties->topology.shader_engine_count_per_xcc;
  *out_profile = profile;
  return true;
}

const amdf_gpu_endpoint_info_t* amdf_gpu_endpoint_profile_get_info(
    const amdf_gpu_endpoint_profile_t* profile) {
  return &profile->info;
}
