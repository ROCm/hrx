# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Generates projections consumed exclusively by VM textual tools."""

from iree.vm.bytecode.spec.generation import run_generator
from iree.vm.bytecode.spec.render.c_assembler import render_assembler_data
from iree.vm.bytecode.spec.render.c_disassembler import render_disassembler_data
from iree.vm.bytecode.spec.specification import SPECIFICATION
from iree.vm.bytecode.spec.text.specification import (
    INSTRUCTION_TEXT_FORMAT,
    MODULE_TEXT_FORMAT,
)


def generate_outputs() -> dict[str, str]:
    return {
        "assembler_data": render_assembler_data(
            SPECIFICATION,
            INSTRUCTION_TEXT_FORMAT,
            MODULE_TEXT_FORMAT,
        ),
        "disassembler_data": render_disassembler_data(
            SPECIFICATION,
            INSTRUCTION_TEXT_FORMAT,
            MODULE_TEXT_FORMAT,
        ),
    }


def main() -> int:
    return run_generator("Generate VM textual-tool projections.", generate_outputs())


if __name__ == "__main__":
    raise SystemExit(main())
