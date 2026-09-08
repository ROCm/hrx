# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Cross-linkage conformance matrix for the public libamdf ABI."""

load(":cc_test.bzl", "amdf_cc_test")

def amdf_cts_test_suite(
        name,
        suites,
        tags = None,
        target_compatible_with = None,
        visibility = None):
    """Runs a CTS corpus through static, shared, and loaded providers.

    Args:
      name: Aggregate test-suite target name.
      suites: Test-only libraries containing the common test corpus.
      tags: Additional tags applied to every generated test target.
      target_compatible_with: Constraints required by every test mode.
      visibility: Visibility of the aggregate test suite.
    """
    tags = tags or []
    common_deps = suites + [
        "//libamdf/cts/util:provider_headers",
        "//third_party:google_test",
    ]
    runtime_data = ["//libamdf:amdf_runtime"]
    test_main = ["//libamdf/cts/util:test_main.cc"]

    amdf_cc_test(
        name = "static",
        srcs = test_main,
        data = runtime_data,
        tags = tags,
        target_compatible_with = target_compatible_with,
        deps = common_deps + [
            "//libamdf/cts/util:linked_provider",
            "//libamdf:amdf_static",
        ],
    )
    amdf_cc_test(
        name = "shared",
        srcs = test_main,
        data = ["//libamdf:amdf_shared_artifact"] + runtime_data,
        tags = tags,
        target_compatible_with = target_compatible_with,
        deps = common_deps + [
            "//libamdf/cts/util:linked_provider",
            "//libamdf:amdf",
        ],
    )
    amdf_cc_test(
        name = "dynamic",
        srcs = test_main,
        args = [
            "--amdf_library=$(rootpath //libamdf:amdf_shared_artifact)",
        ],
        data = ["//libamdf:amdf_shared_artifact"] + runtime_data,
        tags = tags,
        target_compatible_with = target_compatible_with,
        deps = common_deps + ["//libamdf/cts/util:dynamic_provider"],
    )
    native.test_suite(
        name = name,
        tests = [
            ":dynamic",
            ":shared",
            ":static",
        ],
        visibility = visibility,
    )
