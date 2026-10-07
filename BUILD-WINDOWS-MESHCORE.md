# Windows build notes

Use the repository's installed, verified PlatformIO ESP32 toolchains and pinned dependencies. Do not substitute an unverified compiler or ignore integrity/certificate errors.

T-Deck Pro's many board-driver include paths can exceed the Windows GCC process command-line limit in a deeply nested checkout. `support/windows_response_files.py` uses standard SCons response files for long compile commands. If the GCC driver still reports `CreateProcess: No such file or directory`, build from a short checkout path or a temporary `subst` alias of the same checkout. Confirm the chosen drive letter is unused before assigning it.

For example, after checking `M:` is unused, map it to the isolated XNODE checkout with `subst M: <absolute-checkout>`, run from `M:\`, and remove the alias with `subst M: /D` when finished. Keep that alias alive through linking. This changes path length, not compiler, flags or source bytes.

**Do not run PlatformIO against two path aliases with a shared build directory.** Its project checksum includes expanded paths and may clear the other build's outputs. Use sequential builds from one alias, or set `PLATFORMIO_BUILD_DIR` to a separate task-local directory for the alias build. For example, use `.pio/build-pro-short/tdeck-pro` for the alias and `.pio/build/<target>` for other builds.

Run `npm test`, `python src/app/meshcore/test_crypto.py`, and `python support/test_mesh_ble.py --wire-client <XTOC-checkout>/xtoc-web/tools/xnode-wire-test.mjs`. Crypto requires the pinned installed Crypto 0.4.0 and Python cryptography; the runner does not install/download dependencies.

Windows LLVM-MinGW builds without a sanitizer runtime retain the native assertions and strict compiler warnings; use a runtime with ASan/UBSan for sanitizer instrumentation. The Windows crypto test linkage aborts if a dormant RNG reference is invoked.

Application-only images do not constitute a complete factory installation bundle. Follow the board installation instructions and XNODE-CLIENT-RADIO.md.
