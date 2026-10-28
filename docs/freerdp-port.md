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

The separate `FreeRDP ARM11 feasibility` GitHub Actions workflow performs a **non-blocking experimental configure/compile** on pushes and pull requests, and uploads diagnostic logs. Its green status is **not proof** of successful FreeRDP compilation: read the step outcome. It can also be manually dispatched with a `build` argument once the workflow exists on the default branch.

## Verified CI progress (October 28, 2026)

The experimental `build` job completes FreeRDP **CMake configuration** for ARMv6K using devkitARM, static 3DS Mbed TLS and WinPR's built-in Unicode handling. `freerdp-primitives` builds for ARM11. This does **not** mean the WinPR or FreeRDP core libraries are complete or linked into the homebrew application.

Portability issues identified and handled so far:

- **DNS and networking headers:** libctru provides `netdb.h`, `getaddrinfo()` and IPv4 sockets, but its include directory was absent from FreeRDP's CMake toolchain. The port now includes it. `sys/un.h` and `net/if.h` are absent and are guarded for the 3DS where only their headers were referenced.
- **Socket types:** libctru uses signed `int` file descriptors, unlike WinPR's generic `SOCKET` typedef. A 3DS-specific type patch prevents conflicting `closesocket` declarations. IPv6 address structures needed for WinPR headers have a layout-only definition; **IPv6 socket support is not implemented**.
- **Synchronization:** `pipe2()`, POSIX signal timers, and `pthread_mutex_timedlock()` are incorrectly detected or unavailable in the target environment. The build requests WinPR's existing pipe and timed-mutex fallback paths, and disables false-positive POSIX timer detection. A narrow patch avoids unsupported real-time thread scheduling.
- **Dynamic loading:** Horizon OS does not have `dlfcn.h`. In this static build, WinPR's module loader returns `ERROR_CALL_NOT_IMPLEMENTED` on 3DS rather than pretending `dlopen` and `dlsym` work.
- **File APIs:** some WinPR filesystem modules include Unix socket headers despite not using those types; these are guarded for 3DS.

**Critical runtime gaps:** These edits remove *compile-time* blockers. `pipe()` and its descriptor semantics are not yet verified on Horizon, WinPR waitable timers need a real native implementation, and CMake feature probes using `STATIC_LIBRARY` can falsely detect symbols that fail to link. We must not present a compiled FreeRDP archive as proof of usable remote sessions. Missing dynamic plugin loading may also require static channel registration later.

Use the [experimental ARM11 CI workflow](https://github.com/horse5g/3ds-rdp/actions/workflows/freerdp-port.yml) to inspect the *Configure / compile FreeRDP* step and uploaded logs. The step is non-blocking in the draft PR, so a green overall workflow does **not** mean a successful port. `scripts/check-patches.py` now validates patch hunk syntax as part of the fast host CI checks.

## Domains and DNS names

The homebrew configuration parser now accepts `host=rdp01.corp.example.com`, `domain=CORP`, and `username=alice` as future FreeRDP connection settings, along with `CORP\\alice` and `alice@corp.example.com` account formats. Hostname resolution for the **negotiation probe** uses the real libctru `getaddrinfo()` implementation (IPv4). The username/domain are not yet passed to FreeRDP or Windows; there is **no login**.

When CredSSP/NLA is implemented, a username/domain may be used for NTLM authentication. Some enterprise policies require Kerberos; the current build explicitly disables the optional Kerberos/GSSAPI support, so **successful domain sign-in cannot be promised** until we implement and test the required authentication mechanism, secure time, and certificate trust. Passwords are deliberately never loaded from `host.txt`.

## Acceptance criteria

1. A CI-produced `3ds-rdp.3dsx` exists for the *existing* RDP negotiation probe.
2. FreeRDP/WinPR successfully configure for O3DS using devkitARM.
3. Static ARMv6K libraries compile and link into a minimal 3DSX application.
4. Measure actual memory usage and performance on O3DS.
5. Only then implement validated TLS, CredSSP/NLA and graphics.

**Item 2 (CMake configuration) is now verified by CI. Items 3–5 remain open until builds, links and hardware tests verify them.** RDP authentication must never disable TLS certificate validation or NLA to hide a portability problem. Avoid exposing RDP to the public internet.

References: [FreeRDP upstream](https://github.com/FreeRDP/FreeRDP), [3DS homebrew SDK](https://github.com/devkitPro/libctru).
