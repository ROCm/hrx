# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Generates the normative Markdown VM specification."""

from iree.vm.bytecode.spec.generation import run_generator
from iree.vm.bytecode.spec.render.markdown import render_specification
from iree.vm.bytecode.spec.specification import SPECIFICATION


def generate_outputs() -> dict[str, str]:
    return {"documentation": render_specification(SPECIFICATION)}


def main() -> int:
    return run_generator("Generate VM specification documentation.", generate_outputs())


if __name__ == "__main__":
    raise SystemExit(main())
