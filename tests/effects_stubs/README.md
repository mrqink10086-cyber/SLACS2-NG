# Effects adapter test doubles

`slacs_effects_checks` compiles the production `src/integration/Effects.cpp`
with in-memory spell records and actor spell membership. Only this test target
adds this directory to its include path. The DLL uses the real engine headers.

The suite injects failed additions, replacements and clears, then verifies that
the adapter reports failure, retains no successful revision for failed work,
does not stack a new effect after failed removal, and converges on retry.

Run `xmake build -y slacs_effects_checks`, then
`build/windows/x64/release/slacs_effects_checks.exe`.
