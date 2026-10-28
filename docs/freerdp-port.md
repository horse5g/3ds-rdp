# FreeRDP / WinPR ARM11 port investigation

**Status: build-system feasibility work, not a working port.** The existing RDP negotiation client does not link FreeRDP, perform TLS/NLA, or render a desktop.

## Why FreeRDP 3.32.2?

Pinned upstream release 3.32.2 (October 12, 2026) includes recent security fixes. Older FreeRDP 2.x is not an appropriate shortcut for an authentication client. Do not edit vendored sources casually; the [exploratory script](../scripts/build-freerdp-3ds.sh) fetches the tagged source and the version can be changed explicitly for updates.

## Build approach

- Cross-compile for **ARMv6K / O3DS**, devkitARM with the official [toolchain file](../cmake/3ds-armv6k.cmake).
- Static libraries only, no X11, server, client binaries, sound, video, or redirected devices.
- Request FreeRDP's **Mbed TLS** implementation because devkitPro offers a `3ds-mbedtls` package (currently 2.28.8). Upstream warns that its Mbed TLS backend is **experimental**; compatibility and authentication must be verified.
- Request `WinPR` and `libfreerdp` within the same upstream CMake build. Even if configuration succeeds, the next task is resolving the missing platform capabilities, not assuming it links.

Horizon OS isn't POSIX-complete Linux. Native sockets, threads, time, certificate storage, and missing process/filesystem facilities may need WinPR platform adapters. `CMAKE_SYSTEM_NAME=Generic` deliberately avoids pretending the 3DS is Linux, but this might expose CMake assumptions requiring targeted patches.

## First CI findings

The first exploratory CI run reached FreeRDP 3.32.2 CMake configuration. It failed because upstream marks OpenSSL as `REQUIRED` even when `WITH_OPENSSL=OFF` and `WITH_MBEDTLS=ON` are requested. We therefore include a **minimal build-system patch**, [`freerdp-3.32.2-mbedtls-only.patch`](../patches/freerdp-3.32.2-mbedtls-only.patch), which changes only feature detection when Mbed TLS is selected. The script applies it idempotently to its ignored `external/FreeRDP` checkout and **does not patch a custom `FREERDP_SOURCE` checkout**. No cryptographic verification is bypassed.

The next CI failure was `find_package(ICU REQUIRED)` in `winpr/libwinpr/crt/CMakeLists.txt`. Upstream provides `-DWITH_UNICODE_BUILTIN=ON`, so the configuration script now selects its built-in Unicode converter instead of pulling ICU into a resource-constrained target.

This patch only removes an incorrect dependency requirement. It does not establish that Mbed TLS 2.28.8 is compatible, secure for deployment, or that WinPR compiles on Horizon OS. Later build logs determine the next adapter work.

## Reproduce

From devkitPro MSYS2, Linux with devkitARM or a supported devkitPro container, install `3ds-mbedtls` and `3ds-zlib`, then:

```bash
export DEVKITPRO=/opt/devkitpro
export DEVKITARM="$DEVKITPRO/devkitARM"
bash scripts/build-freerdp-3ds.sh configure
bash scripts/build-freerdp-3ds.sh build
```

The default `configure` command only configures; `build` additionally attempts a static-library compilation. Both **may fail until we write the Horizon-specific compatibility layer**. Supply `FREERDP_SOURCE=/path/to/existing/FreeRDP` to use an already checked-out tree, or let the script clone the 3.32.2 tag into ignored `external/FreeRDP`. Files are built under ignored `build/freerdp-3ds`.

The separate `FreeRDP ARM11 feasibility` GitHub Actions workflow performs a **non-blocking experimental configure** on pushes and pull requests, and uploads diagnostic logs. Its green status is **not proof** of successful FreeRDP compilation: read the step outcome. It can also be manually dispatched with a `build` argument once the workflow exists on the default branch.

## Verified CI progress (October 28, 2026)

The experimental `build` job now successfully completes upstream FreeRDP **CMake configuration** for ARMv6K with static Mbed TLS and the built-in Unicode converter. We also disabled `WITH_SWSCALE` and `WITH_CAIRO` so FFmpeg is not required at this stage. The cross-compiler compiled the `freerdp-primitives` library target.

The first **real WinPR compilation blocker** in the CI logs is:

```text
winpr/include/winpr/winsock.h:83:10:
fatal error: netdb.h: No such file or directory
```

This is a devkitARM/Horizon OS portability gap. Next, inventory the actual DNS/socket interfaces supplied by libctru/newlib; implement a proper WinPR adapter (including supported `getaddrinfo` / `getnameinfo` behavior where needed), rather than declaring networking functions that are absent or disabling certificate validation. Then continue compilation and diagnose additional missing POSIX headers / APIs. The CMake feature checks currently use `STATIC_LIBRARY` try-compiles, so a detected function does **not** guarantee a resolved symbol at final link time.

Reproducible evidence is in the `FreeRDP ARM11 feasibility` workflow logs for the PR branch. The experimental step intentionally allows failure while the upstream port is being developed.

## Acceptance criteria

1. A CI-produced `3ds-rdp.3dsx` exists for the *existing* RDP negotiation probe.
2. FreeRDP/WinPR successfully configure for O3DS using devkitARM.
3. Static ARMv6K libraries compile and link into a minimal 3DSX application.
4. Measure actual memory usage and performance on O3DS.
5. Only then implement validated TLS, CredSSP/NLA and graphics.

**Items 2–5 remain open until build logs and hardware tests verify them.** RDP authentication must never disable TLS certificate validation or NLA to hide a portability problem. Avoid exposing RDP to the public internet.

References: [FreeRDP upstream](https://github.com/FreeRDP/FreeRDP), [3DS homebrew SDK](https://github.com/devkitPro/libctru).
