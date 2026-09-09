# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Tests the canonical physical VM text model."""

import unittest

from iree.vm.bytecode.spec.specification import SPECIFICATION
from iree.vm.bytecode.spec.text import (
    InstructionFieldKind,
    InstructionPosition,
    SymbolDomain,
)
from iree.vm.bytecode.spec.text.module import (
    DeclareSymbol,
    NumericField,
    NumericSpelling,
)
from iree.vm.bytecode.spec.text.specification import (
    INSTRUCTION_TEXT_FORMAT,
    MODULE_TEXT_FORMAT,
)


def _instruction(mnemonic: str):
    return next(
        item
        for item in INSTRUCTION_TEXT_FORMAT.instructions
        if item.instruction.mnemonic == mnemonic
    )


class InstructionTextTest(unittest.TestCase):
    def test_every_instruction_has_one_complete_text_description(self) -> None:
        self.assertEqual(
            tuple(item.instruction for item in INSTRUCTION_TEXT_FORMAT.instructions),
            SPECIFICATION.instructions,
        )

    def test_ordinary_arithmetic_uses_register_syntax(self) -> None:
        instruction = _instruction("integer.add.i64")
        self.assertEqual(
            tuple(
                (field.name, field.position, field.kind) for field in instruction.fields
            ),
            (
                (
                    "destination",
                    InstructionPosition.RESULT,
                    InstructionFieldKind.VALUE_REGISTER,
                ),
                (
                    "left",
                    InstructionPosition.OPERAND,
                    InstructionFieldKind.VALUE_REGISTER,
                ),
                (
                    "right",
                    InstructionPosition.OPERAND,
                    InstructionFieldKind.VALUE_REGISTER,
                ),
            ),
        )

    def test_stack_ranges_project_shared_lengths_once(self) -> None:
        instruction = _instruction("stack.copy")
        self.assertEqual(
            tuple(
                (field.name, field.kind, field.primary_field, field.related_field)
                for field in instruction.fields
            ),
            (
                (
                    "target",
                    InstructionFieldKind.LOCAL_BYTES_RANGE,
                    "target_u16",
                    "length_u16",
                ),
                (
                    "source",
                    InstructionFieldKind.LOCAL_BYTES_RANGE,
                    "source_u16",
                    "length_u16",
                ),
            ),
        )

    def test_lane_format_splits_mnemonic_and_register_extent(self) -> None:
        instruction = _instruction("buffer.load")
        projection = instruction.mnemonic_projection
        self.assertIsNotNone(projection)
        self.assertEqual(projection.field_name, "format_u8")
        self.assertEqual(
            {(item.element_name, item.lane_count) for item in projection.formats},
            {
                (element, count)
                for element in ("i8", "i16", "i32", "i64")
                for count in (1, 2, 4, 8)
            },
        )
        result = instruction.fields[0]
        self.assertEqual(result.kind, InstructionFieldKind.VALUE_REGISTER_FORMAT_RANGE)
        self.assertEqual(result.related_field, "format_u8")

    def test_composite_fields_preserve_physical_values(self) -> None:
        cases = (
            (
                "constant.i64",
                "bits",
                InstructionFieldKind.COMBINED_HEX,
                "bits_low_u32",
                "bits_high_u32",
            ),
            (
                "control.call",
                "target",
                InstructionFieldKind.DIRECT_TARGET,
                "target_kind_u8",
                "target_ordinal_u16",
            ),
            (
                "control.switch",
                "targets",
                InstructionFieldKind.SWITCH_SLICE,
                "target_count_u16",
                "target_base_u32",
            ),
            (
                "buffer.copy.rodata",
                "rodata",
                InstructionFieldKind.RODATA_RANGE,
                "rodata_u16",
                "source_offset_u32",
            ),
        )
        for mnemonic, name, kind, primary, related in cases:
            with self.subTest(mnemonic=mnemonic):
                field = next(
                    field
                    for field in _instruction(mnemonic).fields
                    if field.name == name
                )
                self.assertEqual(
                    (field.kind, field.primary_field, field.related_field),
                    (kind, primary, related),
                )

    def test_direct_targets_preserve_selector_domains(self) -> None:
        for mnemonic in ("control.call", "func.address"):
            with self.subTest(mnemonic=mnemonic):
                instruction = _instruction(mnemonic)
                self.assertEqual(
                    tuple(
                        (
                            item.encoded_value,
                            item.symbol_domain,
                            item.symbol_flags_mask,
                            item.symbol_flags_value,
                        )
                        for item in instruction.direct_target_kinds
                    ),
                    (
                        (0, SymbolDomain.FUNCTION, 0, 0),
                        (1, SymbolDomain.IMPORT, 1, 0),
                        (2, SymbolDomain.IMPORT, 1, 1),
                    ),
                )

    def test_symbol_domains_are_explicit(self) -> None:
        cases = (
            ("constant.pool.load.i32", SymbolDomain.CONSTANT),
            ("func.import.resolved", SymbolDomain.IMPORT),
            ("global.value.mutable.load", SymbolDomain.GLOBAL_VALUE),
            ("global.ref.mutable.load.retain", SymbolDomain.GLOBAL_REF),
            ("global.func.mutable.load", SymbolDomain.GLOBAL_FUNCTION),
            ("buffer.rodata.load", SymbolDomain.RODATA),
        )
        for mnemonic, domain in cases:
            with self.subTest(mnemonic=mnemonic):
                field = next(
                    field
                    for field in _instruction(mnemonic).fields
                    if field.kind == InstructionFieldKind.MODULE_SYMBOL
                )
                self.assertEqual(field.symbol_domain, domain)

    def test_generated_symbol_prefixes_match_vm_vocabulary(self) -> None:
        self.assertEqual(SymbolDomain.FUNCTION.value, "function")
        self.assertEqual(SymbolDomain.IMPORT.value, "import")
        self.assertEqual(SymbolDomain.CALLABLE.value, "callable")
        self.assertEqual(SymbolDomain.GLOBAL_VALUE.value, "gv")
        self.assertEqual(SymbolDomain.GLOBAL_REF.value, "gr")
        self.assertEqual(SymbolDomain.GLOBAL_FUNCTION.value, "gf")
        self.assertEqual(SymbolDomain.BLOCK.value, "bb")

    def test_packed_selectors_expand_to_named_attributes(self) -> None:
        cases = (
            ("buffer.atomic.reduce", ("carrier", "kind", "ordering", "scope")),
            (
                "buffer.atomic.cmpxchg",
                ("carrier", "success_ordering", "failure_ordering", "scope"),
            ),
        )
        for mnemonic, names in cases:
            with self.subTest(mnemonic=mnemonic):
                packed = tuple(
                    field
                    for field in _instruction(mnemonic).fields
                    if field.kind == InstructionFieldKind.PACKED_SELECTOR
                )
                self.assertEqual(tuple(field.name for field in packed), names)
                self.assertTrue(
                    all(
                        field.position == InstructionPosition.ATTRIBUTE
                        for field in packed
                    )
                )

    def test_immediate_radix_preserves_semantic_values(self) -> None:
        cases = (
            ("constant.i32", "bits", InstructionFieldKind.HEX),
            ("control.call", "direct_ref_move_mask", InstructionFieldKind.HEX),
            ("integer.lea.i32", "scale", InstructionFieldKind.UNSIGNED),
            ("stack.pack.i32.u16.x2", "immediates", InstructionFieldKind.UNSIGNED),
        )
        for mnemonic, name, kind in cases:
            with self.subTest(mnemonic=mnemonic):
                field = next(
                    field
                    for field in _instruction(mnemonic).fields
                    if field.name == name
                )
                self.assertEqual(field.kind, kind)

    def test_wire_suffixes_do_not_leak_into_text_names(self) -> None:
        cases = (
            ("control.assert", "message"),
            ("stack.fill", "target"),
        )
        for mnemonic, expected_name in cases:
            with self.subTest(mnemonic=mnemonic):
                self.assertIn(
                    expected_name,
                    tuple(field.name for field in _instruction(mnemonic).fields),
                )


class ModuleTextTest(unittest.TestCase):
    def test_shell_exposes_version_and_section_alignment(self) -> None:
        shell = MODULE_TEXT_FORMAT.shell
        self.assertEqual(shell.module_prefix, "vm.module core(")
        self.assertEqual(shell.core_major.field_name, "core_major_u16")
        self.assertEqual(
            shell.core_required_minor.field_name, "core_required_minor_u16"
        )
        self.assertEqual(shell.section_alignment.field_name, "payload_alignment_u32")

    def test_every_section_has_one_canonical_text_description(self) -> None:
        self.assertEqual(
            tuple(section.section for section in MODULE_TEXT_FORMAT.sections),
            SPECIFICATION.module_format.sections,
        )

    def test_every_grammar_has_a_unique_name(self) -> None:
        names = tuple(grammar.name for grammar in MODULE_TEXT_FORMAT.grammars)
        self.assertEqual(len(names), len(set(names)))

    def test_all_variable_tails_are_owned_once(self) -> None:
        self.assertEqual(
            MODULE_TEXT_FORMAT.tail_names,
            ("strings", "bytecode", "rodata", "metadata"),
        )

    def test_yield_permission_uses_source_vocabulary(self) -> None:
        grammars = {grammar.name: grammar for grammar in MODULE_TEXT_FORMAT.grammars}
        for grammar_name in ("callable", "function"):
            with self.subTest(grammar=grammar_name):
                numeric_fields = tuple(
                    element
                    for element in grammars[grammar_name].elements
                    if isinstance(element, NumericField)
                )
                self.assertEqual(len(numeric_fields), 1)
                self.assertEqual(
                    numeric_fields[0].spelling_overrides,
                    (NumericSpelling("may_yield", "yieldable"),),
                )

    def test_import_symbols_preserve_resolution_flags(self) -> None:
        grammar = next(
            grammar
            for grammar in MODULE_TEXT_FORMAT.grammars
            if grammar.name == "import"
        )
        declaration = next(
            element
            for element in grammar.elements
            if isinstance(element, DeclareSymbol)
        )
        self.assertEqual(declaration.domain, SymbolDomain.IMPORT)
        self.assertEqual(declaration.flags_field_name, "flags_u16")


if __name__ == "__main__":
    unittest.main()
