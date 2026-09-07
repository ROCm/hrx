# libamdf

libamdf is the portable C boundary for native AMD GPU and XDNA device access.
It isolates operating-system and driver-private mechanisms behind an
unloadable library while leaving executable formats, command construction,
scheduling, and memory policy in the calling runtime.

The base public surface is `include/amdf/amdf.h`. `amdf_query_api` negotiates an
ABI version and returns an immutable API table. Optional family surfaces such
as `include/amdf/xdna.h` are negotiated from that table according to what was
compiled into the library, independent of the hardware currently present.
Querying either table performs no allocation, system call, device discovery,
dependent-library load, or other observable initialization.

An explicit provider instance owns all dependent native libraries and can
enumerate fixed-stride summaries of independently selectable AMD execution
endpoints. Opening an endpoint caches immutable identity for later GPU or XDNA
qualification without creating a device, address space, paging queue,
allocation, executable, or hardware queue. The Windows provider implements
this query-only layer with public KMT adapter APIs. Builds without a native
platform provider expose the public headers but do not produce provider
artifacts.

Each opened endpoint also reports a dense immutable set of native queue
families. A family identifies its accepted command representation (PM4, SDMA,
AQL, or XDNA) independently from how commands are published. User publication
means the caller directly updates mapped queue state; kernel publication means
a bounded provider call accepts already prepared top-level commands. CPU
translation from one representation to another is not a publication mode.

Advertising a command/publication pair promises that a matching queue can be
constructed for that endpoint. Missing families mean the loaded provider has
no matching implementation, not that the silicon necessarily lacks the
capability. `endpoint_query_queue_family_info` copies records cached during
endpoint open and performs no allocation, system call, device initialization,
queue creation, retry, sleep, or device wait. The query-only provider currently
reports no families because it contains no queue constructors.

The build produces two link modes from one implementation:

- `//libamdf:amdf` and `amdf::amdf` consume the shared library.
- `//libamdf:amdf_static` and `amdf::amdf_static` consume the static library.
- `//libamdf:amdf_shared_artifact` names the loadable DLL or shared object for
  clients that resolve `amdf_query_api` at runtime.

`AMDF_BUILD` controls the CMake subtree and defaults to the value of
`IREE_HAL_DRIVER_AMDGPU`. The Bazel equivalent is
`--//libamdf/config:enabled`. RDNA, CDNA, and XDNA package admission is selected
with the `AMDF_FAMILY_*` CMake options or the
`--//libamdf/config:families=...` Bazel setting. Bazel implementation packages
select individual members through `//libamdf/config/family:rdna`, `:cdna`, and
`:xdna` without interpreting the setting themselves.

Configure and test Bazel explicitly without enabling the legacy AMDGPU HAL:

```bash
python dev.py bazel configure -DAMDF_BUILD=ON
iree-bazel-test //libamdf/cts/...
iree-bazel-run //libamdf/examples:enumerate
```

The equivalent CMake build is:

```bash
iree-cmake-configure -DAMDF_BUILD=ON -DIREE_HAL_DRIVER_AMDGPU=OFF -DLIBHRX_BUILD=OFF -DLOOM_BUILD=OFF
iree-cmake-build amdf amdf_static
iree-cmake-test -R '^libamdf/' -LE manual
```

Hardware-backed CTS targets carry the `manual` label and run only when named
explicitly on a qualified host.

Installing the repository exports `amdf::amdf` and `amdf::amdf_static` through
`find_package(amdf CONFIG REQUIRED)` and installs the public headers beneath
`include/amdf/`.
