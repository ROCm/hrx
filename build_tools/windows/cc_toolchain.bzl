# Copyright 2026 The IREE Authors
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Windows C/C++ toolchain selection, MASM tools, and sanitizer runtimes."""

load("@platforms//host:constraints.bzl", "HOST_CONSTRAINTS")
load("@rules_cc//cc:find_cc_toolchain.bzl", "CC_TOOLCHAIN_TYPE", "find_cc_toolchain", "use_cc_toolchain")
load("@rules_cc//cc/common:cc_common.bzl", "cc_common")
load("@rules_cc//cc/common:cc_info.bzl", "CcInfo")

_WINDOWS_X86_64 = ["@platforms//cpu:x86_64", "@platforms//os:windows"]

def _windows_cc_toolchain_impl(ctx):
    cc = ctx.attr.cc[cc_common.CcToolchainInfo]
    return [
        cc,
        ctx.attr.cc[DefaultInfo],
        ctx.attr.cc[platform_common.TemplateVariableInfo],
        platform_common.ToolchainInfo(
            cc = cc,
            cc_provider_in_toolchain = True,
            # MASM is distinct from the generic C/C++ assembler action.
            masm = struct(
                # Executable to invoke in the C/C++ toolchain environment.
                assembler = ctx.file.masm,
                # Assembler flags preceding the output and source paths.
                arguments = ctx.attr.masm_arguments,
                # Exact executable and loader inputs, without C/C++ headers.
                files = depset([ctx.file.masm] + ctx.files.masm_files),
            ),
            # Compiler-owned Windows runtime artifacts, indexed by filename.
            address_sanitizer_files = {file.basename: file for file in ctx.files.address_sanitizer_files},
        ),
    ]

windows_cc_toolchain = rule(
    implementation = _windows_cc_toolchain_impl,
    attrs = {
        "address_sanitizer_files": attr.label_list(allow_files = True, doc = "Matching Windows compiler-rt DLL, import library, and runtime thunk."),
        "cc": attr.label(mandatory = True, providers = [cc_common.CcToolchainInfo]),
        "masm": attr.label(allow_single_file = True, cfg = "exec", mandatory = True),
        "masm_arguments": attr.string_list(doc = "MASM flags preceding output and source paths."),
        "masm_files": attr.label_list(allow_files = True, cfg = "exec", doc = "MASM executable and loader dependencies."),
    },
    doc = "Preserves a C/C++ toolchain and binds its Windows assembler and runtime artifacts.",
)

def _address_sanitizer_runtime_impl(ctx):
    files = ctx.toolchains[CC_TOOLCHAIN_TYPE].address_sanitizer_files
    cc = find_cc_toolchain(ctx)
    configuration = cc_common.configure_features(
        ctx = ctx,
        cc_toolchain = cc,
        requested_features = ctx.features,
        unsupported_features = ctx.disabled_features,
    )
    static_crt = cc_common.is_enabled(feature_configuration = configuration, feature_name = "static_link_msvcrt")
    names = [
        "clang_rt.asan_dynamic-x86_64.dll",
        "clang_rt.asan_dynamic-x86_64.lib",
        "clang_rt.asan_%s_runtime_thunk-x86_64.lib" % ("static" if static_crt else "dynamic"),
    ]
    missing = [name for name in names if name not in files]
    if missing:
        fail("The selected Windows C/C++ toolchain requires matching compiler-rt files for AddressSanitizer: %s. For cross-compilation, set WINDOWS_COMPILER_RT_ROOT to the Windows LLVM lib/clang/<version>/lib/windows directory. See BUILDING.md." % ", ".join(missing))
    dynamic_library = cc_common.create_library_to_link(
        actions = ctx.actions,
        cc_toolchain = cc,
        feature_configuration = configuration,
        dynamic_library = files[names[0]],
        interface_library = files[names[1]],
    )
    thunk_library = cc_common.create_library_to_link(
        actions = ctx.actions,
        cc_toolchain = cc,
        feature_configuration = configuration,
        static_library = files[names[2]],
        alwayslink = True,
    )
    linker_input = cc_common.create_linker_input(
        owner = ctx.label,
        libraries = depset([dynamic_library, thunk_library]),
        # Runtime libraries are explicit inputs. MSVC inference would add the
        # same thunk again through its installation path and duplicate symbols.
        user_link_flags = depset(["/INCLUDE:__asan_seh_interceptor"] + (["/INFERASANLIBS:NO"] if cc.compiler == "msvc-cl" else [])),
    )
    return [
        CcInfo(linking_context = cc_common.create_linking_context(linker_inputs = depset([linker_input]))),
        DefaultInfo(runfiles = ctx.runfiles(files = [files[names[0]]])),
    ]

# This rule consumes raw toolchain files directly: cc_library and cc_import
# themselves depend on the runtimes toolchain and would introduce a cycle here.
_address_sanitizer_runtime = rule(
    implementation = _address_sanitizer_runtime_impl,
    fragments = ["cpp"],
    toolchains = use_cc_toolchain(),
)

def _runtimes_toolchain_impl(ctx):
    return [platform_common.ToolchainInfo(cc_runtimes_info = struct(
        # Runtime C/C++ dependencies for library and executable link actions.
        runtimes = ctx.attr.runtimes,
        # These runtimes require no additional source compilation flags.
        copts = [],
    ))]

_runtimes_toolchain = rule(
    implementation = _runtimes_toolchain_impl,
    attrs = {"runtimes": attr.label_list(providers = [CcInfo])},
)

# This package has one fixed toolchain set selected by the local host platform.
# buildifier: disable=unnamed-macro
def windows_toolchains():
    """Declares local Windows toolchains without configuring unused SDKs."""
    if "@platforms//os:windows" in HOST_CONSTRAINTS:
        for compiler, cc_target in [
            ("clang_cl", "cc-compiler-x64_windows-clang-cl"),
            ("msvc", "cc-compiler-x64_windows"),
        ]:
            windows_cc_toolchain(
                name = "native_" + compiler,
                tags = ["manual"],
                cc = "@local_config_cc//:" + cc_target,
                masm = "//build_tools/bazel:msvc_masm_wrapper.bat",
                masm_arguments = ["/nologo", "/Zi", "/c"],
                address_sanitizer_files = select({
                    "//build_tools/bazel:address_sanitizer_target": ["@local_config_cc//:" + compiler + "_x64_asan_files"],
                    "//conditions:default": [],
                }),
            )
            native.toolchain(
                name = "windows_x86_64_" + compiler,
                exec_compatible_with = HOST_CONSTRAINTS,
                target_compatible_with = _WINDOWS_X86_64,
                target_settings = [":compiler_" + compiler],
                toolchain = ":native_" + compiler,
                toolchain_type = "@bazel_tools//tools/cpp:toolchain_type",
            )
    elif "@platforms//os:linux" in HOST_CONSTRAINTS:
        native.toolchain(
            name = "windows_x86_64_clang_cl",
            exec_compatible_with = HOST_CONSTRAINTS,
            target_compatible_with = _WINDOWS_X86_64,
            target_settings = [":compiler_clang_cl"],
            toolchain = "@iree_windows_toolchain//:toolchain",
            toolchain_type = "@bazel_tools//tools/cpp:toolchain_type",
        )
    else:
        return

    _address_sanitizer_runtime(name = "address_sanitizer_runtime", tags = ["manual"])
    _runtimes_toolchain(name = "address_sanitizer_runtimes", runtimes = [":address_sanitizer_runtime"], tags = ["manual"])
    native.toolchain(
        name = "windows_x86_64_address_sanitizer",
        exec_compatible_with = HOST_CONSTRAINTS,
        target_compatible_with = _WINDOWS_X86_64,
        target_settings = ["//build_tools/bazel:address_sanitizer_target"],
        toolchain = ":address_sanitizer_runtimes",
        toolchain_type = "@bazel_tools//tools/cpp:cc_runtimes_toolchain_type",
    )
