// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/src/provider/extension.h"

#include <stddef.h>

#if defined(AMDF_HAVE_XDNA)
#include "libamdf/src/xdna/extension.h"
#endif  // AMDF_HAVE_XDNA

amdf_status_t AMDF_CALL amdf_extension_query(amdf_extension_id_t extension_id,
                                             uint32_t minimum_version,
                                             uint32_t maximum_version,
                                             const void** out_extension_api) {
  if (out_extension_api == NULL) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }
  *out_extension_api = NULL;
  if (minimum_version > maximum_version) {
    return amdf_make_api_status(AMDF_STATUS_CODE_INVALID_ARGUMENT);
  }

  switch (extension_id) {
#if defined(AMDF_HAVE_XDNA)
    case AMDF_EXTENSION_XDNA:
      return amdf_xdna_extension_query(minimum_version, maximum_version,
                                       out_extension_api);
#endif  // AMDF_HAVE_XDNA
    default:
      return amdf_make_api_status(AMDF_STATUS_CODE_UNSUPPORTED);
  }
}
