# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

from __future__ import annotations

import sys
from pathlib import Path


def main(argv: list[str]) -> int:
    if "--helpish" in argv:
        print("Usage: fixture_tool [options]")
        print("--echo-stdin")
        print("--write-file=PATH")
        return 0
    if "--fail" in argv:
        print("intentional fixture failure", file=sys.stderr)
        return 3
    for arg in argv:
        if arg.startswith("--write-file="):
            Path(arg.split("=", 1)[1]).write_bytes(b"fixture artifact\n")
        elif arg.startswith("--cat-file="):
            print(Path(arg.split("=", 1)[1]).read_text(encoding="utf-8"), end="")
        elif arg == "--echo-stdin":
            print("stdin:")
            print(sys.stdin.read(), end="")
        else:
            print(f"unknown argument: {arg}", file=sys.stderr)
            return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
