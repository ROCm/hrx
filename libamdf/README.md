# libamdf

libamdf is the portable C boundary for native AMD GPU and XDNA device access.
It isolates operating-system and driver-private mechanisms behind an
unloadable library while leaving executable formats, command construction,
scheduling, and memory policy in the calling runtime.

The public surface is `include/amdf/amdf.h`. `amdf_query_api` negotiates an ABI
version and returns an immutable API table. Querying the table performs no
allocation, system call, device discovery, dependent-library load, or other
observable initialization.

The build produces two link modes from one implementation:

- `//libamdf:amdf` and `amdf::amdf` consume the shared library.
- `//libamdf:amdf_static` and `amdf::amdf_static` consume the static library.
- `//libamdf:amdf_shared_artifact` names the loadable DLL or shared object for
  clients that resolve `amdf_query_api` at runtime.

`AMDF_BUILD` controls the CMake subtree and defaults to the value of
`IREE_HAL_DRIVER_AMDGPU`. The Bazel equivalent is
`--//libamdf/config:enabled`. RDNA, CDNA, and XDNA package admission is selected
with the `AMDF_FAMILY_*` CMake options or the
`--//libamdf/config:families=...` Bazel setting.

Configure and test Bazel explicitly without enabling the legacy AMDGPU HAL:

```bash
python dev.py bazel configure -DAMDF_BUILD=ON
iree-bazel-test //libamdf/cts/...
iree-bazel-run //libamdf/examples:query_api
```

The equivalent CMake build is:

```bash
iree-cmake-configure -DAMDF_BUILD=ON -DIREE_HAL_DRIVER_AMDGPU=OFF -DLIBHRX_BUILD=OFF -DLOOM_BUILD=OFF
iree-cmake-build libamdf/all
iree-cmake-test -R '^libamdf/'
```

Installing the repository exports `amdf::amdf` and `amdf::amdf_static` through
`find_package(amdf CONFIG REQUIRED)` and installs the public headers beneath
`include/amdf/`.
