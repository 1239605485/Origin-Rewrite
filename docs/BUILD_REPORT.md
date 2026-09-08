# Build report — OriginRewrite 0.8.0 terrain change diagnostic

Date: 2026-09-07 (UTC)

## Inputs used

- `OriginRewrite_起源重构_整合优化设计文档_v0.6(2).docx`
- `OriginRewrite-v0.6.84-source.zip`
- `OriginRewrite-v0.6.84-batch-drop-final-arg-android-arm64.zip`
- supplied TEFKernel, KernelLoader, C++ Wrapper, Mods, TEFPkg Tool and TEFManager sources
- official BNM-Android 2.5.2, commit `502928771983d29e37f28c78f28823dfb775a3aa`

No v0.6.78 binary was attached, so the installable layout was verified against the supplied v0.6.84
Android package: root metadata plus `Resources/lib/libOriginRewrite.android.arm64.so`.

## Build

| Item | Result |
|---|---|
| Android toolchain | NDK r26c, Clang 17.0.2 |
| ABI / API | arm64-v8a / android-24 |
| C / C++ | C11 / C++20 |
| Shared object | AArch64 ELF64, dynamically linked, not stripped |
| SONAME | `libOriginRewrite.android.arm64.so` |
| Dependencies | `liblog.so`, `libdl.so`, `libm.so`, `libc.so` |
| Required exports | `create_kernel_mod`, `mod_logger_write` present |
| Build ID | `bcd86044cf0d4fd98d93e9d038f6980c299fb03b` |

## Verification

- Host CMake configure and full shared-library link: passed.
- Pure-core and configuration-overlay CTest suite: 1/1 passed.
- Android ARM64 CMake build including official BNM and xDL: passed.
- All source/package JSON documents parsed by `jq`: passed.
- ZIP CRC test for every entry: passed.
- ZIP path traversal check: passed.
- Metadata/version and required package paths: passed.

Installable SHA-256:

`1ca2377e9227898d77fb13ed7f09835b5c6fd56889880f3d90add01ae8a2d8d4`

Shared-library SHA-256:

`942db5e9f7a4d666b1d7f5449c9c453ef97d4fc30a7ef133fb625f478f36b580`

## Device-validation boundary

This report proves source tests, cross compilation, ELF/package structure and static contracts. It
cannot prove behavior inside the user's exact Terraria APK without launching that APK. The package
therefore stays `experimental=true` and `stableVerified=false`. Special native AI actions, color
storage, extra item creation, world-save writes and custom multiplayer transport remain SAFE-OFF;
the verified stat/name lifecycle and vanilla-preserving fallbacks stay available.
