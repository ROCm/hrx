# XDNA image compatibility fixture

`mul_i32.xdna` is the exact Loom-generated vector multiplication image from the
native driver smoke package. It targets AIE2P Strix Halo
`amd.xdna.strix_halo.17f0_11`, with one compute core at context-relative `(0, 2)`
in a one-column, six-row partition.

The entry `mul_i32` has three 64-byte buffer bindings: read-only lhs, read-only
rhs, and write-only output. It multiplies sixteen little-endian i32 elements,
retaining each product's low 32 bits. The image contains the tile program,
ordered ARRAY initialization, finite CONTROL dispatch, three typed address
relocations, and an output DMA completion wait.

These exact bytes completed native Linux XDNA execution with all sixteen
results checked bit-for-bit and explicit resource teardown. The fixture tests
the compiler/loader ABI without rebuilding the compiler or requiring a device.
It does not establish concurrent or repeated dispatch behavior.

SHA-256:
`2085441fa886afc38ee8e850e6bcc5baeeb08cddaf8fe31c8fa11aa2c06cb3e7`
