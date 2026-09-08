# libamdf

libamdf is the portable C boundary for native AMD GPU and XDNA device access.
It isolates operating-system and driver-private mechanisms behind an
unloadable library while leaving executable formats, command construction,
scheduling, and memory policy in the calling runtime.

The base public surface is `include/amdf/amdf.h`. `amdf_query_api` negotiates an
ABI version and returns an immutable API table. Optional family surfaces in
`include/amdf/gpu.h` and `include/amdf/xdna.h` are negotiated from that table
according to what was compiled into the library, independent of the hardware
currently present. Querying any table performs no allocation, system call,
device discovery, dependent-library load, or other observable initialization.

An explicit provider instance owns the native platform state retained across
calls and can enumerate fixed-stride summaries of independently selectable AMD
execution endpoints. Opening an endpoint caches immutable identity and available
family-specific qualification without creating a device, address space,
paging queue, allocation, executable, or hardware queue. The Windows provider
implements this query-only layer with public KMT adapter APIs. Builds without a
native platform provider expose the public headers but do not produce provider
artifacts.

On x86-64 Windows, the GPU extension qualifies an already opened KMT adapter
through a private `amdf_wkmi_bridge.dll` runtime companion. The bridge contains
the pinned binary-only WKMI C++ and CRT ABI behind a versioned C table, retains
no adapter state, and unloads before endpoint open returns. GPU information
queries copy the cached exact GFX identity, ASIC revision, active compute
geometry, LDS limit, and XCC topology without loading a library or entering the
driver. GPU endpoints advertise kernel-published PM4 and SDMA families only
when the loaded KMT and WKMI surfaces provide hardware-scheduled queues for
the selected engine.

Each opened endpoint also reports a dense immutable set of native queue
families. A family identifies its accepted command representation (PM4, SDMA,
AQL, or XDNA) independently from how commands are published. User publication
means the caller directly updates mapped queue state; kernel publication means
a bounded provider call accepts already prepared top-level commands. CPU
translation from one representation to another is not a publication mode.

A materialized Windows GPU device owns one WDDM address domain and can attach
system, device-local, and registered-host memory at stable GPU virtual
addresses. Local allocations may be executable without being host-visible;
callers stage their contents through prepared GPU commands. The current
kernel-mediated PM4 and SDMA queues accept one immutable native command range
without reading or translating its bytes, borrow the command attachment until
a monitored progress fence retires it, and support bounded polling plus a
kernel wait.

Advertising a command/publication pair promises that a matching queue can be
constructed for that endpoint. Missing families mean the loaded provider has
no matching implementation, not that the silicon necessarily lacks the
capability. `endpoint_query_queue_family_info` copies records cached during
endpoint open and performs no allocation, system call, device initialization,
queue creation, retry, sleep, or device wait. A Windows endpoint advertises the
kernel-mediated XDNA family only when the loaded KMT surface contains every
operation required to construct and publish to that queue.

A materialized XDNA device can own system-memory backing with one stable XDNA
virtual address on qualified Windows x86-64 systems. Memory creation publishes
the address only after mapping and ordinary residency have completed, without
using the fatal `MustSucceed` residency mode. Explicit host mappings expose
write-back cached pages and require range-scoped flush or invalidate operations
when ownership moves between the host and XDNA. Placement classes or properties
that the provider cannot fully satisfy fail explicitly instead of silently
degrading.

Qualified XDNA profiles can also accept copied target-native program components
and immutable prepared commands. Program creation validates the exact
profile-specific transaction envelope and declared array footprint before
copying component bytes. Command creation copies invocation control, resolves
each memory binding to its stable device address once, and borrows the program
and memory objects until destruction. Neither operation submits work or mutates
active array state.

The qualified NPU5 path exposes a kernel-mediated XDNA queue with one prepared
command per submission and one unretired submission at a time. Submission is a
bounded native publication call: it performs no allocation, transaction
parsing, lowering, binding resolution, command transcription, retry, sleep, or
host wait. Status queries and waits retire the submission's command borrow only
after the native progress fence covers it.

The build produces two link modes from one implementation:

- `//libamdf:amdf` and `amdf::amdf` consume the shared library.
- `//libamdf:amdf_static` and `amdf::amdf_static` consume the static library.
- `//libamdf:amdf_shared_artifact` names the loadable DLL or shared object for
  clients that resolve `amdf_query_api` at runtime.

Windows GPU distributions install the private WKMI bridge beside `amdf.dll`.
It is neither a public link input nor part of the libamdf ABI.

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
