# Copyright 2026 The IREE Authors
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Inventories the public headers and link interfaces of selected SDK frameworks.

Framework headers belong to explicit C/C++ dependencies, not to every compiler
action. Header inventories conservatively include conditional framework imports;
the selected linker supplies the architecture-specific re-export closure. No
framework implementation or shader compiler is part of an SDK dependency.
"""

import argparse
import json
import re
import subprocess
from pathlib import Path


def find_cxx_headers(sdk, native_compiler=None):
    """Finds libc++ in the SDK or its native Xcode companion toolchain."""
    directory = sdk / "usr/include/c++/v1"
    if not (directory / "vector").is_file() and native_compiler:
        directory = (
            native_compiler.resolve(strict=True).parent.parent / "include/c++/v1"
        )
    if not (directory / "vector").is_file():
        raise ValueError(
            "The selected macOS toolchain has no libc++ headers. Cross SDK "
            "exports must include usr/include/c++/v1 from the matching Xcode "
            "toolchain; see BUILDING.md"
        )
    return directory


def directory_files(directory, ancestors=()):
    """Lists logical file paths without following ancestor directory aliases."""
    canonical = directory.resolve(strict=True)
    if canonical in ancestors:
        return []
    if not directory.is_dir():
        return [directory]
    result = []
    for child in sorted(directory.iterdir()):
        result.extend(directory_files(child, ancestors + (canonical,)))
    return result


class FrameworkHeaders:
    """Owns SDK framework namespaces and their public header dependencies."""

    def __init__(self, sdk):
        self.sdk = sdk
        self.frameworks = {
            path.stem: path
            for path in sorted((sdk / "System/Library/Frameworks").glob("*.framework"))
        }
        # A top-level framework wins over aliases in umbrella frameworks.
        self.owners = {name: name for name in self.frameworks}
        for name, path in self.frameworks.items():
            pending = self.children(path)
            while pending:
                child = pending.pop()
                self.owners.setdefault(child.stem, name)
                pending.extend(self.children(child))
        self.headers = {}
        self.dependencies = {}
        self.directories = set()

    @staticmethod
    def children(framework):
        return sorted((framework / "Frameworks").glob("*.framework"))

    def collect(self, name):
        if name in self.headers:
            return
        if name not in self.frameworks:
            raise ValueError(f"The selected macOS SDK has no {name} framework")
        pending = [self.frameworks[name]]
        headers = []
        while pending:
            framework = pending.pop()
            directory = framework / "Headers"
            if directory.exists():
                self.directories.add(directory)
                headers.extend(directory_files(directory))
            pending.extend(self.children(framework))
        self.headers[name] = headers
        dependencies = set()
        for header in headers:
            if header.suffix != ".h":
                continue
            # SDK headers include legacy encodings. Preprocessor directive
            # punctuation and framework names use the ASCII subset.
            content = header.read_text(encoding="latin-1").replace("\\\n", "")
            content = re.sub(r"/\*.*?\*/|//[^\n]*", "", content, flags=re.DOTALL)
            for directive in re.findall(
                r"^\s*#\s*(?:include|import)\s+([^\n]+)", content, re.MULTILINE
            ):
                match = re.fullmatch(r'\s*([<"])([^>"]+)[>"]\s*', directive)
                if not match:
                    raise ValueError(
                        f"Cannot inventory computed SDK include in {header}: {directive}"
                    )
                delimiter, include = match.groups()
                if delimiter == '"' and (header.parent / include).exists():
                    continue
                dependency = self.owners.get(include.split("/")[0])
                if dependency and dependency != name:
                    dependencies.add(dependency)
        self.dependencies[name] = dependencies

    def closure(self, name):
        visited = set()
        pending = [name]
        while pending:
            name = pending.pop()
            if name in visited:
                continue
            visited.add(name)
            self.collect(name)
            pending.extend(self.dependencies[name])
        return sorted(
            {
                path.relative_to(self.sdk).as_posix()
                for name in visited
                for path in self.headers[name]
            }
        )


def link_inputs(sdk, linker, architecture, sdk_version, minimum_os, framework, output):
    """Asks the selected linker for every input in a real SDK-only link."""
    flags = ["-framework", framework] if framework else ["-lSystem", "-lc++", "-lobjc"]
    dependency_file = output.with_suffix(".deps")
    subprocess.run(
        [
            str(linker),
            "-arch",
            architecture,
            "-platform_version",
            "macos",
            minimum_os,
            sdk_version,
            "-syslibroot",
            str(sdk),
            "-dylib",
            *flags,
            "-o",
            str(output.with_suffix(".dylib")),
            "-dependency_info",
            str(dependency_file),
        ],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    data = dependency_file.read_bytes()
    paths = set()
    missing_paths = set()
    while data:
        opcode = data[0]
        value, data = data[1:].split(b"\0", 1)
        # ld's dependency-info stream uses a byte opcode and NUL-terminated
        # string per entry. 0x10 denotes an input that was actually opened.
        if opcode == 0x10:
            path = Path(value.decode())
            paths.add(path.relative_to(sdk).as_posix())
        elif opcode == 0x11:
            path = Path(value.decode())
            if path.is_relative_to(sdk):
                missing_paths.add(path.relative_to(sdk).as_posix())
        elif opcode not in (0x00, 0x40):
            raise ValueError(f"Unknown linker dependency opcode {opcode:#x}")
    return sorted(paths), sorted(missing_paths)


def native_tool_libraries(tools):
    """Resolves non-system Mach-O libraries using each tool's load commands."""
    libraries = {}
    for executable in tools:
        pending = [(executable, [])]
        visited = set()
        while pending:
            binary, inherited_paths = pending.pop()
            binary = binary.resolve(strict=True)
            if binary in visited:
                continue
            visited.add(binary)
            commands = subprocess.check_output(
                ["/usr/bin/otool", "-l", str(binary)], text=True, stderr=subprocess.PIPE
            )
            rpaths = re.findall(
                r"cmd LC_RPATH\n\s+cmdsize \d+\n\s+path (.+) \(offset", commands
            )

            def expand(path):
                return Path(
                    path.replace("@loader_path", str(binary.parent)).replace(
                        "@executable_path", str(executable.parent)
                    )
                )

            search_paths = list(
                dict.fromkeys([expand(path) for path in rpaths] + inherited_paths)
            )
            dependencies = subprocess.check_output(
                ["/usr/bin/otool", "-L", str(binary)], text=True, stderr=subprocess.PIPE
            )
            for dependency in re.findall(
                r"^\t(.+) \(compatibility version", dependencies, re.MULTILINE
            ):
                if dependency.startswith(("/usr/lib/", "/System/Library/")):
                    continue
                if dependency.startswith("@rpath/"):
                    candidates = [
                        path / dependency[len("@rpath/") :] for path in search_paths
                    ]
                elif dependency.startswith(("@loader_path/", "@executable_path/")):
                    candidates = [expand(dependency)]
                else:
                    raise ValueError(
                        f"Non-relocatable tool dependency in {binary}: {dependency}"
                    )
                resolved = next(
                    (path.resolve() for path in candidates if path.is_file()), None
                )
                if resolved is None:
                    raise ValueError(
                        f"Cannot resolve tool dependency in {binary}: {dependency}"
                    )
                name = Path(dependency).name
                previous = libraries.setdefault(name, str(resolved))
                if previous != str(resolved):
                    raise ValueError(
                        f"Tools require different libraries named {name}: {previous}, {resolved}"
                    )
                pending.append((resolved, search_paths))
    return libraries


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", required=True, type=Path)
    parser.add_argument("--linker", required=True, type=Path)
    parser.add_argument("--minimum-os", required=True)
    parser.add_argument("--framework", action="append", default=[])
    parser.add_argument("--native-tool", action="append", type=Path, default=[])
    parser.add_argument("--native-compiler", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    sdk = args.sdk.resolve(strict=True)
    settings = json.loads((sdk / "SDKSettings.json").read_text())
    include = sdk / "usr/include"
    cxx_include = find_cxx_headers(sdk, args.native_compiler)
    frameworks = FrameworkHeaders(sdk)
    metadata = {
        "c_headers": [
            path.relative_to(sdk).as_posix()
            for path in directory_files(include)
            if not path.is_relative_to(include / "c++")
        ],
        "cxx_directory": str(cxx_include),
        "cxx_headers": [
            path.relative_to(cxx_include).as_posix()
            for path in directory_files(cxx_include)
        ],
        "framework_headers": {
            name: frameworks.closure(name) for name in args.framework
        },
        "libraries": {},
        "native_dynamic_libraries": native_tool_libraries(args.native_tool),
    }
    missing_paths = set()
    for architecture in ["arm64", "x86_64"]:
        metadata["libraries"][architecture] = {}
        for name in [""] + args.framework:
            inputs, missing = link_inputs(
                sdk,
                args.linker,
                architecture,
                settings["Version"],
                args.minimum_os,
                name,
                args.output.parent / (architecture + "-" + (name or "system")),
            )
            metadata["libraries"][architecture][name] = inputs
            missing_paths.update(missing)
    metadata["missing_link_paths"] = sorted(missing_paths)
    metadata["header_directories"] = [
        path.relative_to(sdk).as_posix() for path in sorted(frameworks.directories)
    ]
    args.output.write_text(json.dumps(metadata, indent=2) + "\n")


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        diagnostic = error.stderr
        if isinstance(diagnostic, bytes):
            diagnostic = diagnostic.decode()
        raise SystemExit(diagnostic) from error
