# Overlay adapter test doubles

`slacs_overlay_checks` compiles the production `src/integration/OverlayAdapter.cpp`
with these in-memory RaceMenu interfaces and actor leases. Only that test target
adds this directory to its include path. The DLL uses the real headers.

The suite checks blank slots with residual alpha, foreign material preservation,
cleared cached slot reuse, overlapping old/new region ranges during migration,
actor clearing, actual capacity intersecting the configured slot range, and
residual opacity after a foreign diffuse takes over an owned slot.

These doubles cover adapter allocation and ownership decisions. They do not
validate the RaceMenu binary ABI, shader rendering, callbacks, or Skyrim threads.

Run `xmake build -y slacs_overlay_checks`, then
`build/windows/x64/release/slacs_overlay_checks.exe`.
