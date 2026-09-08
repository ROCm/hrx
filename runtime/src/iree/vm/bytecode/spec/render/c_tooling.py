# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Small C-rendering primitives shared by VM textual-tool projections."""

from __future__ import annotations

from collections.abc import Hashable, Iterable
from typing import TypeVar

_GENERATED = """\
// Generated from the authoritative VM bytecode specification. Do not edit.
// clang-format off
"""

_RowT = TypeVar("_RowT", bound=Hashable)


class BStringPool:
    """Interns short strings into a compact length-prefixed byte pool."""

    def __init__(self, description: str):
        self.description = description
        self.data = bytearray(b"\x00")
        self.offsets = {"": 0}
        self.values = [""]

    def intern(self, value: str) -> int:
        if value in self.offsets:
            return self.offsets[value]
        encoded = value.encode("utf-8")
        if len(encoded) > 0xFF:
            raise ValueError(f"{self.description} BSTRING is too long: {value!r}")
        if len(self.data) + 1 + len(encoded) > 0xFFFF:
            raise ValueError(f"{self.description} BSTRING pool exceeds u16 offsets")
        offset = len(self.data)
        self.data.append(len(encoded))
        self.data.extend(encoded)
        self.offsets[value] = offset
        self.values.append(value)
        return offset


def intern_row_group(
    rows: Iterable[_RowT],
    table: list[_RowT],
    group_bases: dict[tuple[_RowT, ...], int],
) -> int:
    """Packs a row group into a table and returns its stable table base."""

    group = tuple(rows)
    base = group_bases.get(group)
    if base is not None:
        return base

    for base in range(len(table) - len(group) + 1):
        if tuple(table[base : base + len(group)]) == group:
            group_bases[group] = base
            return base

    overlap = 0
    for candidate in range(1, min(len(table), len(group)) + 1):
        if tuple(table[-candidate:]) == group[:candidate]:
            overlap = candidate
    base = len(table) - overlap
    table.extend(group[overlap:])
    group_bases[group] = base
    return base


def _escape_c_string(value: str) -> str:
    return (
        value.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\n", "\\n")
        .replace("\r", "\\r")
        .replace("\t", "\\t")
    )


def render_bstring_pool(declaration: str, pool: BStringPool) -> list[str]:
    """Renders a BSTRING pool as readable adjacent C string literals."""

    lines = [f"{declaration} ="]
    for value in pool.values:
        byte_length = len(value.encode("utf-8"))
        lines.append(f'    "\\x{byte_length:02x}" "{_escape_c_string(value)}"')
    lines[-1] += ";"
    return lines


def render_byte_array(declaration: str, data: bytes) -> list[str]:
    """Renders deterministic compact byte-array initializers."""

    lines = [f"{declaration} = {{"]
    for offset in range(0, len(data), 16):
        values = ", ".join(f"0x{value:02X}" for value in data[offset : offset + 16])
        lines.append(f"    {values},")
    lines.append("};")
    return lines


def generated_preamble() -> str:
    return _GENERATED.rstrip()
