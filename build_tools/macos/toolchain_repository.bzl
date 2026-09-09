# Copyright 2026 The IREE Authors
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Projects native Xcode or Linux LLVM with a selected macOS SDK."""

load("//build_tools/bazel:elf_dynamic_library_closure.bzl", "elf_loader_environment", "resolve_elf_dynamic_library_closure")

# Frameworks with repository consumers. Each exposes its SDK-owned header and
# link dependency closure without adding unrelated frameworks to build actions.
_SDK_FRAMEWORKS = ["CoreFoundation", "CoreGraphics", "Foundation", "Metal"]

_WRAPPER = """#!/bin/sh
set -eu
tool_root="${0%/*}/.."
{environment}
exec "$tool_root/bin/{tool}" "$@"
"""

def _required_path(repository_ctx, path):
    path = repository_ctx.path(path)
    if not path.exists:
        fail("macOS toolchain requires %s. Check the selected compiler and SDK in BUILDING.md." % path)
    repository_ctx.watch(path)
    repository_ctx.watch(path.realpath)
    return path.realpath

def _execute(repository_ctx, arguments):
    environment = elf_loader_environment() | {
        "DYLD_LIBRARY_PATH": "",
        "DYLD_FRAMEWORK_PATH": "",
        "DYLD_INSERT_LIBRARIES": "",
    }
    result = repository_ctx.execute(arguments, environment = environment, quiet = True)
    if result.return_code:
        fail("macOS toolchain discovery failed:\n" + result.stderr)
    return result.stdout.strip()

def _macos_toolchain_repository_impl(repository_ctx):
    cross = repository_ctx.os.name == "linux"
    tools = {}
    if cross:
        llvm_root = repository_ctx.getenv("LLVM_ROOT")
        sdk_root = repository_ctx.getenv("MACOS_SDK_ROOT")
        if not llvm_root or not sdk_root:
            fail("Linux-to-macOS builds require LLVM_ROOT and MACOS_SDK_ROOT (environment variables or --repo_env options). See BUILDING.md.")
        llvm_root = _required_path(repository_ctx, llvm_root)
        for name, source in {
            "clang": "clang",
            "clang++": "clang++",
            "ld64.lld": "ld64.lld",
            "libtool": "llvm-libtool-darwin",
            "strip": "llvm-strip",
        }.items():
            tools[name] = _required_path(repository_ctx, llvm_root.get_child("bin", source))
        libraries = resolve_elf_dynamic_library_closure(repository_ctx, tools)
        environment = "export LD_LIBRARY_PATH=\"$tool_root/host_dynamic_libraries\"\nunset LD_PRELOAD LD_AUDIT"
    elif repository_ctx.os.name == "mac os x":
        # Native builds follow xcode-select/DEVELOPER_DIR and ignore cross SDK
        # variables, including when selecting the other macOS architecture.
        repository_ctx.getenv("DEVELOPER_DIR")
        xcrun = _required_path(repository_ctx, "/usr/bin/xcrun")
        sdk_root = _execute(repository_ctx, [xcrun, "--sdk", "macosx", "--show-sdk-path"])
        for name in ["clang", "clang++", "ld", "libtool", "strip"]:
            tools[name] = _required_path(repository_ctx, _execute(repository_ctx, [xcrun, "--find", name]))
        libraries = {}
        environment = "export DYLD_LIBRARY_PATH=\"$tool_root/host_dynamic_libraries\"\nunset DYLD_INSERT_LIBRARIES"
    else:
        fail("macOS toolchains support Linux and macOS build hosts; host is " + repository_ctx.os.name)

    sdk_root = _required_path(repository_ctx, sdk_root)
    repository_ctx.symlink(sdk_root, "sysroot")
    for name, path in tools.items():
        repository_ctx.symlink(path, "bin/" + name)
        repository_ctx.file("tools/" + name, _WRAPPER.replace("{environment}", environment).replace("{tool}", name), executable = True)
    resource = _required_path(repository_ctx, _execute(repository_ctx, [tools["clang"], "-print-resource-dir"]))
    repository_ctx.symlink(_required_path(repository_ctx, resource.get_child("include")), "resource/include")
    runtime_root = repository_ctx.getenv("MACOS_COMPILER_RT_ROOT") if cross else None
    runtime_root = _required_path(repository_ctx, runtime_root) if runtime_root else resource.get_child("lib", "darwin")
    for name in ["libclang_rt.osx.a", "libclang_rt.asan_osx_dynamic.dylib"]:
        path = runtime_root.get_child(name)
        repository_ctx.watch(path)
        if path.exists:
            repository_ctx.symlink(_required_path(repository_ctx, path), "resource/lib/darwin/" + name)

    python = repository_ctx.which("python3")
    if not python:
        fail("macOS SDK configuration requires Python 3.9 or newer on PATH.")
    repository_ctx.file("metadata/.keep", "")
    arguments = [
        python,
        repository_ctx.path(repository_ctx.attr._sdk_metadata),
        "--sdk",
        sdk_root,
        "--linker",
        repository_ctx.path("bin/ld64.lld" if cross else "bin/ld"),
        "--minimum-os",
        repository_ctx.attr.minimum_os,
        "--output",
        repository_ctx.path("metadata/sdk.json"),
    ]
    for framework in _SDK_FRAMEWORKS:
        arguments.extend(["--framework", framework])
    if not cross:
        arguments.extend(["--native-compiler", tools["clang"]])
        for path in tools.values():
            arguments.extend(["--native-tool", path])
    _execute(repository_ctx, arguments)
    metadata = json.decode(repository_ctx.read("metadata/sdk.json"))
    cxx_directory = _required_path(repository_ctx, metadata.pop("cxx_directory"))
    repository_ctx.symlink(cxx_directory, "cxx")
    repository_ctx.watch_tree(cxx_directory)
    libraries.update(metadata.pop("native_dynamic_libraries"))
    for name, path in libraries.items():
        repository_ctx.symlink(_required_path(repository_ctx, path), "host_dynamic_libraries/" + name)

    repository_ctx.watch(sdk_root.get_child("SDKSettings.json"))
    repository_ctx.watch(sdk_root.get_child("System", "Library", "Frameworks"))
    for directory in ["usr/include"] + metadata["header_directories"]:
        repository_ctx.watch_tree(sdk_root.get_child(directory).realpath)
    for architecture in metadata["libraries"].values():
        for paths in architecture.values():
            for path in paths:
                _required_path(repository_ctx, sdk_root.get_child(path))

    # A newly installed search candidate can change the selected link closure.
    for path in metadata["missing_link_paths"]:
        repository_ctx.watch(sdk_root.get_child(path))
    repository_ctx.file("BUILD.bazel", """
load({toolchain_bzl}, "macos_cc_toolchains")
package(default_visibility = ["//visibility:public"])
macos_cc_toolchains(
    name = "toolchain",
    repository_path = {repository_path},
    metadata = {metadata},
    minimum_os = {minimum_os},
    cross = {cross},
)
""".format(
        toolchain_bzl = repr(str(repository_ctx.attr._toolchain_bzl)),
        repository_path = repr("external/" + repository_ctx.name),
        metadata = repr(metadata),
        minimum_os = repr(repository_ctx.attr.minimum_os),
        cross = repr(cross),
    ))

macos_toolchain_repository = repository_rule(
    implementation = _macos_toolchain_repository_impl,
    attrs = {
        "minimum_os": attr.string(default = "11.0", doc = "Minimum macOS deployment version for both destination architectures."),
        "_sdk_metadata": attr.label(default = Label("//build_tools/macos:sdk_metadata.py")),
        "_toolchain_bzl": attr.label(default = Label("//build_tools/macos:toolchain.bzl")),
    },
    local = True,
    configure = True,
)
