# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Generates projections consumed by the shipping VM runtime."""

from iree.vm.bytecode.spec.generation import run_generator
from iree.vm.bytecode.spec.render.c_runtime import (
    render_core_header,
    render_instruction_verifier_cases,
    render_interpreter_data,
    render_layout_data,
    render_module_header,
    render_module_verifier_cases,
    render_verifier_data,
    render_wire_assertions,
)
from iree.vm.bytecode.spec.specification import SPECIFICATION


def generate_outputs() -> dict[str, str]:
    """Returns the complete deterministic shipping-runtime projection family."""

    return {
        "wire_module_header": render_module_header(SPECIFICATION),
        "wire_core_header": render_core_header(SPECIFICATION),
        "wire_assertions_source": render_wire_assertions(SPECIFICATION),
        "instruction_verifier_cases": render_instruction_verifier_cases(SPECIFICATION),
        "module_verifier_cases": render_module_verifier_cases(SPECIFICATION),
        "verifier_source": render_verifier_data(SPECIFICATION),
        "interpreter_data": render_interpreter_data(SPECIFICATION),
        "layout_data": render_layout_data(SPECIFICATION),
    }


def main() -> int:
    return run_generator(
        "Generate shipping VM runtime projections.", generate_outputs()
    )


if __name__ == "__main__":
    raise SystemExit(main())
