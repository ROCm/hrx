# Experimental XDNA execution through libamdf

This directory contains temporary executable and prepared-command adapters,
their fake provider, and a standalone runner. They bridge the durable HAL image
loader to the root `libamdf/` library during native bring-up. A real HAL driver
will replace these consumers with normal tooling and HAL CTS coverage.

`iree-xdna-run` executes one entry from an intact Loom `.xdna` file through the
experimental executable and prepared-command adapters and libamdf. It supports the
Strix Halo `17f0:11` target, using native Linux DRM or Windows MCDM. The Linux
path has no XRT, HSA, or ROCr dependency.

From a checkout configured with libamdf and the XDNA family enabled:

```sh
iree-bazel-run --config=asan //experimental/xdna:iree-xdna-run -- \
  --image=driver-smoke/copy_i32.xdna --columns=1 --entry=copy_i32 \
  --binding=driver-smoke/data/lhs.bin \
  --binding=driver-smoke/data/copy_i32.output_initial.bin \
  --output=1=copy-output.bin
```

`--columns` selects the logical context size; image qualification requires an
exact match. `--entry` names an export and defaults to ordinal zero when
omitted. `--device` selects an XDNA endpoint ordinal and defaults to zero.

Each `--binding` supplies initial raw bytes in the entry's binding order. Its
file size determines the logical buffer length. Output buffers also receive
initial bytes, allowing a sentinel to expose missing writes. The image's
canonical binding records determine alignment and validate access and range
requirements. Each allocation has its own native attachment, persistent host
view, and logical HAL buffer; offsets are zero.

Each `--output=ordinal=path` writes the selected buffer's complete logical
contents after successful command retirement and explicit cache invalidation.
Paths are overwritten. Multiple distinct bindings may be written. Output
comparison belongs to the caller or the artifact's accompanying checker:

```sh
python driver-smoke/fixtures.py check copy_i32 copy-output.bin
```

Every invocation creates a fresh context and realizes the ARRAY before its
CONTROL program. Completion means the finite command's output DMA work has
completed; a resident tile worker may remain blocked waiting for another
input. The runner waits without a hidden deadline, then releases the queue,
prepared command, executable, mappings, memory, context, and instance in that
order. A native error is reported without retry. Failed cleanup stops at its
ownership boundary and returns failure; that run has not established safe
explicit teardown.

The CLI tests exercise host argument and file ownership without creating a
device. Native execution is an explicit operator action. Crash-continuity
capture, kernel logging, and input/output retention belong to the surrounding
run harness. Windows source support does not establish Windows driver
execution safety.
