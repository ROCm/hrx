# Copyright 2026 The IREE Authors
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Exports the selected Xcode macOS SDK and its C++ headers as a tar.gz archive."""

import argparse
import subprocess
import tarfile
from pathlib import Path

from sdk_metadata import find_cxx_headers


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    sdk = Path(
        subprocess.check_output(
            ["xcrun", "--sdk", "macosx", "--show-sdk-path"], text=True
        ).strip()
    ).resolve(strict=True)
    compiler = Path(
        subprocess.check_output(["xcrun", "--find", "clang"], text=True).strip()
    )
    cxx_headers = find_cxx_headers(sdk, compiler).resolve(strict=True)
    sdk_has_cxx_headers = cxx_headers.is_relative_to(sdk)

    def sdk_member(member):
        # Replace external libc++ directory aliases with the companion headers.
        if not sdk_has_cxx_headers and (
            member.name == "MacOSX.sdk/usr/include/c++"
            or member.name.startswith("MacOSX.sdk/usr/include/c++/")
        ):
            return None
        return member

    # Resolving the SDK root avoids exporting just Xcode's versioned directory
    # symlink. Internal framework aliases remain relative links in the archive.
    with tarfile.open(args.output, "x:gz") as archive:
        archive.add(sdk, arcname="MacOSX.sdk", filter=sdk_member)
        if not sdk_has_cxx_headers:
            archive.add(
                cxx_headers.resolve(strict=True),
                arcname="MacOSX.sdk/usr/include/c++/v1",
            )
    print(args.output)


if __name__ == "__main__":
    main()
