# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Current canonical VM bytecode text specification."""

from iree.vm.bytecode.spec.specification import SPECIFICATION
from iree.vm.bytecode.spec.text.instruction import build_instruction_text_format
from iree.vm.bytecode.spec.text.module import build_module_text_format

INSTRUCTION_TEXT_FORMAT = build_instruction_text_format(SPECIFICATION)
MODULE_TEXT_FORMAT = build_module_text_format(SPECIFICATION)
