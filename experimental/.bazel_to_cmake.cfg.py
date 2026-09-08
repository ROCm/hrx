# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

import bazel_to_cmake_config
import bazel_to_cmake_converter
import bazel_to_cmake_requirements


class XdnaBuildFileFunctions(bazel_to_cmake_converter.BuildFileFunctions):
    def _custom_initialize(self):
        self._xdna_policy = bazel_to_cmake_requirements.load_project_policy(
            self._repo_root, "experimental/xdna"
        ).collect("experimental/xdna")

    def _apply_xdna_policy(self, kwargs):
        kwargs = dict(kwargs)
        kwargs["target_compatible_with"] = (
            bazel_to_cmake_requirements.append_cmake_conditions(
                kwargs.get("target_compatible_with"),
                self._xdna_policy.cmake_conditions(),
            )
        )
        kwargs["tags"] = list(kwargs.get("tags") or []) + self._xdna_policy.tags(
            include_run_requirements=False
        )
        return kwargs

    def xdna_cc_library(self, deps=[], **kwargs):
        self.cc_library(
            deps=deps + ["//runtime/src:defines"], **self._apply_xdna_policy(kwargs)
        )

    def xdna_cc_binary(self, deps=[], **kwargs):
        self.cc_binary(
            deps=deps + ["//runtime/src:defines"], **self._apply_xdna_policy(kwargs)
        )

    def xdna_cc_test(self, deps=[], **kwargs):
        self.cc_test(
            deps=deps + ["//runtime/src:defines"], **self._apply_xdna_policy(kwargs)
        )

    def xdna_execution_test_suite(self, **kwargs):
        self.iree_execution_test_suite(**self._apply_xdna_policy(kwargs))


PROJECT_CONFIG = bazel_to_cmake_config.ProjectConfig(
    name="experimental",
    package_prefixes=["experimental"],
    build_file_functions=XdnaBuildFileFunctions,
)
