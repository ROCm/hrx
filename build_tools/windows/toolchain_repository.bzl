# Copyright 2026 The IREE Authors
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Projects a selected local LLVM and xwin sysroot into a Windows toolchain."""

load(
    "//build_tools/bazel:elf_dynamic_library_closure.bzl",
    "elf_loader_environment",
    "resolve_elf_dynamic_library_closure",
)

_TOOLS = [
    "clang-cl",
    "lld-link",
    "llvm-lib",
    "llvm-cov",
    "llvm-nm",
    "llvm-objcopy",
    "llvm-objdump",
    "llvm-strip",
]

_WRAPPER = """#!/bin/sh
set -eu
tool_root="${0%/*}/.."
export LD_LIBRARY_PATH="$tool_root/host_dynamic_libraries"
unset LD_PRELOAD LD_AUDIT
exec "$tool_root/bin/{tool}" "$@"
"""

def _required_path(repository_ctx, path):
    path = repository_ctx.path(path)
    if not path.exists:
        fail("Windows toolchain requires %s; check LLVM_ROOT and WINSDK_ROOT." % path)
    repository_ctx.watch(path)
    canonical_path = path.realpath
    repository_ctx.watch(canonical_path)
    return canonical_path

def _project(repository_ctx, source, destination):
    repository_ctx.symlink(_required_path(repository_ctx, source), destination)

def _version_directory(repository_ctx, parent):
    parent = _required_path(repository_ctx, parent)
    versions = [path for path in parent.readdir() if path.is_dir]
    if len(versions) != 1:
        fail("Windows toolchain requires exactly one version under %s; found %s. Select a versioned xwin sysroot with WINSDK_ROOT." % (parent, versions))
    return _required_path(repository_ctx, versions[0])

def _windows_toolchain_repository_impl(repository_ctx):
    if repository_ctx.os.name == "windows":
        repository_ctx.file("BUILD.bazel", """
package(default_visibility = ["//visibility:public"])
alias(name = "toolchain", actual = "@local_config_cc//:cc-toolchain-x64_windows-clang-cl")
alias(name = "address_sanitizer_runtime", actual = "@local_config_cc//:clang_cl_x64_asan_runtime")
""")
        return
    if repository_ctx.os.name != "linux":
        fail("The local Windows toolchain currently executes on Linux or Windows; host is %s." % repository_ctx.os.name)
    architecture = repository_ctx.os.arch
    if architecture in ["amd64", "x86_64"]:
        architecture = "x86_64"
    elif architecture in ["aarch64", "arm64"]:
        architecture = "aarch64"
    else:
        fail("Unsupported Linux LLVM execution architecture: %s" % architecture)

    llvm_root = repository_ctx.getenv("LLVM_ROOT")
    sdk_root = repository_ctx.getenv("WINSDK_ROOT")
    if not llvm_root or not sdk_root:
        fail("Windows cross-compilation requires LLVM_ROOT and WINSDK_ROOT. Set these environment variables or pass --repo_env=LLVM_ROOT=/path/to/llvm --repo_env=WINSDK_ROOT=/path/to/windows-sysroot. See BUILDING.md.")
    llvm_root = _required_path(repository_ctx, llvm_root)
    sdk_root = _required_path(repository_ctx, sdk_root)
    artifacts = {}
    for tool in _TOOLS:
        artifacts[tool] = _required_path(repository_ctx, llvm_root.get_child("bin", tool))
        repository_ctx.symlink(artifacts[tool], "bin/" + tool)
        repository_ctx.file("tools/" + tool, _WRAPPER.replace("{tool}", tool), executable = True)

    libraries = resolve_elf_dynamic_library_closure(repository_ctx, artifacts)
    for name, path in libraries.items():
        repository_ctx.symlink(path, "host_dynamic_libraries/" + name)

    resource_result = repository_ctx.execute(
        [str(artifacts["clang-cl"]), "--no-default-config", "-print-resource-dir"],
        environment = elf_loader_environment(),
        quiet = True,
    )
    if resource_result.return_code:
        fail("Could not query LLVM resource directory:\n" + resource_result.stderr)
    resource_root = _required_path(repository_ctx, resource_result.stdout.strip())
    _project(repository_ctx, resource_root.get_child("include"), "resource/include")

    crt_root = _version_directory(repository_ctx, sdk_root.get_child("VC", "Tools", "MSVC"))
    sdk_include_root = _version_directory(repository_ctx, sdk_root.get_child("Windows Kits", "10", "Include"))
    sdk_library_root = sdk_root.get_child("Windows Kits", "10", "Lib", sdk_include_root.basename)
    _project(repository_ctx, crt_root.get_child("include"), "include/crt")
    for component in ["ucrt", "shared", "um", "winrt", "cppwinrt"]:
        _project(repository_ctx, sdk_include_root.get_child(component), "include/" + component)
    _project(repository_ctx, crt_root.get_child("lib", "x64"), "lib/crt")
    for component in ["ucrt", "um"]:
        _project(repository_ctx, sdk_library_root.get_child(component, "x64"), "lib/" + component)

    repository_ctx.file("BUILD.bazel", """
load({toolchain_bzl}, "windows_cc_toolchain")
package(default_visibility = ["//visibility:public"])
windows_cc_toolchain(
    name = "toolchain",
    repository_path = {repository_path},
    execution_architecture = {execution_architecture},
    msvc_version = {msvc_version},
)
""".format(
        toolchain_bzl = repr(str(repository_ctx.attr._toolchain_bzl)),
        repository_path = repr("external/" + repository_ctx.name),
        execution_architecture = repr(architecture),
        msvc_version = repr(crt_root.basename),
    ))

windows_toolchain_repository = repository_rule(
    implementation = _windows_toolchain_repository_impl,
    attrs = {
        "_toolchain_bzl": attr.label(default = Label("//build_tools/windows:toolchain.bzl")),
    },
    local = True,
    configure = True,
)
