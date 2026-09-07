# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""libamdf package policy."""

load(
    "//build_tools/bazel:package_policy.bzl",
    "apply_target_policy",
    "apply_test_policy",
    "collect_package_policy",
    "package_policy",
)
load(
    "//libamdf/requirements:defs.bzl",
    "LIBAMDF",
    "LIBAMDF_GPU",
    "LIBAMDF_XDNA",
)

PACKAGE_POLICIES = [
    package_policy(
        packages = [
            "libamdf",
            "libamdf/cts/...",
            "libamdf/examples/...",
            "libamdf/src/...",
        ],
        build_requirements = [LIBAMDF],
    ),
    package_policy(
        packages = [
            "libamdf/src",
            "libamdf/src/platform/...",
        ],
        forbidden_deps = [
            "//libamdf/src/gpu/...",
            "//libamdf/src/xdna/...",
        ],
    ),
    package_policy(
        packages = [
            "libamdf/cts/gpu/...",
            "libamdf/src/gpu/...",
        ],
        build_requirements = [LIBAMDF_GPU],
        forbidden_deps = ["//libamdf/src/xdna/..."],
    ),
    package_policy(
        packages = [
            "libamdf/cts/xdna/...",
            "libamdf/src/xdna/...",
        ],
        build_requirements = [LIBAMDF_XDNA],
        forbidden_deps = ["//libamdf/src/gpu/..."],
    ),
]

def _current_policy():
    return collect_package_policy(native.package_name(), PACKAGE_POLICIES)

def apply_amdf_target_policy(kwargs, name = None):
    return apply_target_policy(kwargs, _current_policy(), name = name)

def apply_amdf_test_policy(kwargs, name = None):
    return apply_test_policy(kwargs, _current_policy(), name = name)
