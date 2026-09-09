# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Generates exact conversion vectors consumed only by runtime tests."""

from iree.vm.bytecode.spec.conversion_test_vectors import (
    render_conversion_test_vectors,
)
from iree.vm.bytecode.spec.generation import run_generator
from iree.vm.bytecode.spec.specification import SPECIFICATION


def generate_outputs() -> dict[str, str]:
    return {
        "conversion_test_vectors": render_conversion_test_vectors(SPECIFICATION),
    }


def main() -> int:
    return run_generator("Generate VM conversion test vectors.", generate_outputs())


if __name__ == "__main__":
    raise SystemExit(main())
