# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Command-line support shared by purpose-specific specification generators."""

from __future__ import annotations

import argparse
from collections.abc import Mapping
from pathlib import Path


def _write_output(path: Path, contents: str | bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if isinstance(contents, bytes):
        if path.is_file() and path.read_bytes() == contents:
            return
        path.write_bytes(contents)
    else:
        if path.is_file() and path.read_text(encoding="utf-8") == contents:
            return
        path.write_text(contents, encoding="utf-8")


def run_generator(description: str, outputs: Mapping[str, str | bytes]) -> int:
    """Writes one deterministic, consumer-owned family of projections."""

    parser = argparse.ArgumentParser(description=description)
    for output_name in outputs:
        parser.add_argument(
            f"--{output_name.replace('_', '-')}", type=Path, required=True
        )
    arguments = parser.parse_args()
    for output_name, contents in outputs.items():
        _write_output(getattr(arguments, output_name), contents)
    return 0
