## Opt-in combined application (integration milestone, not hardware validated)

The normal `make` target still builds `3ds-rdp.3dsx`, with the proven
A negotiation probe and B/Y/SELECT diagnostics. A **separate experimental**
full-client path now successfully compiles, links and packages OpenSSL, WinPR, the FreeRDP core,
libctru, runtime password entry, a basic GDI framebuffer and touch controls:

```sh
bash scripts/build-openssl-3ds.sh build
FREERDP_CRYPTO_BACKEND=openssl bash scripts/build-freerdp-3ds.sh build
bash scripts/link-freerdp-3ds.sh
bash scripts/build-combined-3dsx.sh
```

The third command creates a standalone combined ARM11 ELF/3DSX using the real
`freerdp_new`, `freerdp_connect`, settings and OpenSSL symbols. The fourth
builds `3ds-rdp-full.3dsx` with `RDP_FULL_CLIENT=1` in a separate
`build-full/` intermediate directory. **All four integration gates passed** in
[GitHub Actions run 40672154636](https://github.com/horse5g/3ds-rdp/actions/runs/40672154636)
(commit `2c189caf`), including the actual `.3dsx` packaging step. The
[full app and static archives](https://github.com/horse5g/3ds-rdp/actions/runs/40672154636/artifacts/12808337729)
are available as a workflow artifact. The workflow intentionally keeps detailed
failed-stage diagnostics but its final gate now requires all stages to succeed.

In the opt-in application, press **R** to attempt a FreeRDP session. The
password comes from the on-screen keyboard and is **never read from** or
written to `host.txt`; FreeRDP owns a temporary copy while the session is
alive. The 3DS GDI path currently targets 400x240, maps touch motion onto the
remote desktop, sends A as a left-click and X as an ASCII-only text keyboard.
B or START exits the session. No graphical controls or certificate management
UI is implemented yet.

**Certificate trust:** the full client now supports a 32-entry,
host:port-bound **saved certificate pool** at
`sdmc:/3ds/3ds-rdp/trusted-certs.txt`. A first-time certificate with valid
names/dates triggers an explicit 3DS prompt: **B/START** reject, **A** accept
for one session, **X** accept and save. Existing fingerprints match without
reprompting, while **changed** certificates are blocked until the user
verifies and manually removes or updates the old entry. The optional
single-endpoint, out-of-band `cert-pin.txt` takes precedence for its host.
See [certificate-pinning.md](certificate-pinning.md) for risks, pool format,
recovery, manual pins and a secure source of fingerprints.

FreeRDP still uses external X.509 verification with name/date checks; the
client does not enable `/cert:ignore` or disable NLA. **First-use acceptance
is subject to MITM attacks if the user fails to verify the displayed SHA-256
against the actual Windows host.** Neither pinning nor this pool automatically
checks certificate revocation. Successful ELF/3DSX builds do not establish
safe TLS/NLA on hardware, domain sign-in, framebuffer correctness,
thread/event behavior or O3DS memory/CPU performance.

Earlier sections below record the development history; compile failures mentioned in those historical notes may have since been resolved.

## FreeRDP with native ARM11 OpenSSL (experimental)

The previous ARM11 FreeRDP job deliberately used experimental Mbed TLS only. It
cross-compiled WinPR, primitives and codecs, but the main FreeRDP library
stopped at an unconditional `#include <openssl/bio.h>` in
`libfreerdp/utils/http.c`. Disabling that code or replacing it with fake
OpenSSL declarations would obscure a required TLS dependency.

An **independent** cross-build now pairs the pinned PS-backed OpenSSL 3.5.9
archives with FreeRDP 3.32.2. See [FreeRDP ARM11 with OpenSSL
CI](../.github/workflows/freerdp-openssl-arm11.yml). It builds the crypto
archives first, then runs:

```sh
bash scripts/build-openssl-3ds.sh build
FREERDP_CRYPTO_BACKEND=openssl bash scripts/build-freerdp-3ds.sh build
```

The OpenSSL backend supplies `WITH_OPENSSL=ON`, `WITH_MBEDTLS=OFF`, explicit
ARM11 `libssl.a` and `libcrypto.a` paths and both upstream and generated
OpenSSL include directories. The original Mbed TLS experiment remains
available by omitting `FREERDP_CRYPTO_BACKEND`. Build directories should not
be reused when switching between the crypto backends.

The combined workflow reports per-stage outcomes and has a final gate that
requires core compilation, full-client syntax, combined link and application
packaging to succeed. Independent Mbed TLS feasibility checks may still allow
compile failures. Full compilation,
static linking into the homebrew application, secure hostname/certificate
validation, tested OpenSSL entropy on hardware, CredSSP/NLA, and graphics are
separate milestones. This new build path does **not** disable NLA or weaken
certificate verification.

# FreeRDP / WinPR ARM11 port investigation

**Current status: integrated FreeRDP/OpenSSL client cross-build passes; O3DS runtime remains unverified.** The default RDP negotiation client still has no TLS/NLA or desktop output; the separate `3ds-rdp-full.3dsx` is not yet a proven working remote desktop.

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
- **Serial/COM ports:** the WinPR serial module assumes POSIX `termios`, which the O3DS SDK does not expose. The experimental build excludes that module and its file-handle creator. Serial device redirection is not supported.
- **File APIs and named pipes:** a WinPR filesystem module included unavailable Unix socket headers, now guarded on 3DS. WinPR's Unix-domain named-pipe client requires `AF_UNIX` and is explicitly disabled for 3DS; the descriptor helper returns unsupported. This is separate from TCP RDP transport. WinPR's POSIX file-timestamp code also assumes `UTIME_OMIT` / `futimens`, which the target lacks; the 3DS path now returns `ERROR_CALL_NOT_IMPLEMENTED` for timestamp changes rather than silently claiming success.

**Critical runtime gaps:** These edits remove *compile-time* blockers. `pipe()` and its descriptor semantics are not yet verified on Horizon, WinPR waitable timers need a real native implementation, and CMake feature probes using `STATIC_LIBRARY` can falsely detect symbols that fail to link. We must not present a compiled FreeRDP archive as proof of usable remote sessions. Missing dynamic plugin loading may also require static channel registration later.

Use the [experimental ARM11 CI workflow](https://github.com/horse5g/3ds-rdp/actions/workflows/freerdp-port.yml) to inspect the *Configure / compile FreeRDP* step and uploaded logs. The step is non-blocking in the draft PR, so a green overall workflow does **not** mean a successful port. `scripts/check-patches.py` now validates patch hunk syntax as part of the fast host CI checks.

## Next architectural blocker: WinPR pipe/event backend

After the module exclusions above, the next ARM11 compiler error is:

```text
winpr/libwinpr/pipe/pipe.c:42:10: fatal error: sys/un.h: No such file or directory
```

This is **not another harmless header omission**: WinPR `pipe.c` implements both anonymous WinPR pipes (which are used by events and waits) and named pipes (which require AF_UNIX and descriptor duplication). Horizon/libctru has no Unix-domain sockets, and its socket file descriptors do not automatically share POSIX `read`/`write` semantics. We must not add fake `sockaddr_un` or `AF_UNIX` definitions to force the compiler through.

The next substantive porting task is an honest 3DS-specific synchronization/pipe implementation. Candidate design: use libctru/3DS kernel events for synchronization, with a separate socket wait strategy for SOC sockets; adapt WinPR's event, wait and pollset layers to avoid assuming everything is a Unix file descriptor. Disable unsupported named pipes and serial endpoints explicitly, and test wait wakeups, cancellation, timeouts and cleanup on real O3DS. Timers require a native implementation as well. Only then is there value in attempting an integrated FreeRDP client or CredSSP authentication.

The compilation pipeline has reached WinPR's core pipe subsystem, but no end-to-end WinPR library link, remote session, or runtime synchronization behavior has been verified.

## Native Horizon kernel-event backend (hardware smoke test)

A new standalone library (`include/horizon_sync.h` and `source/horizon_sync.c`) uses 3DS `svcCreateEvent`, `svcSignalEvent`, `svcClearEvent`, `svcWaitSynchronization`, and `svcWaitSynchronizationN` directly. It implements sticky manual-reset and one-shot auto-reset kernel events, bounded/infinite waits, and a **wait-any** on up to 16 kernel events. These functions operate on true `Handle` objects, **not POSIX or SOC descriptors**. They can eventually be adapted into WinPR's event/multiple-wait code.

The built O3DS homebrew application exposes a **B-button hardware self-test** which checks unsignaled timeouts, one-shot signal consumption, sticky persistence and reset, indexed wait-any, and a blocking cross-thread signal/wakeup using a libctru thread. The test reports a PASS/FAIL reason on the top screen. Compiling the test with devkitARM is **not** equivalent to running it: results must be verified on an original 3DS or 2DS.

Next, implement WinPR handle bridging and mixed waits: native kernel events should use `svcWaitSynchronizationN`; SOC IPv4 sockets require network readiness through `select()`, which cannot be passed as kernel handles. A combined wait needs a bounded, measurable mechanism for cancellation and wakeups rather than treating handles as file descriptors or spinning without a timeout. Named-pipe, COM/serial, file timestamp and dynamic library features remain unavailable; none of these should silently report success.

**Do not wire these kernel APIs into FreeRDP authentication until the hardware self-test and wait semantics have been validated.** An experimental SOC loopback event and anonymous-pipe patch is now available, but the completed WinPR/FreeRDP build, static link, and runtime behavior are not verified.

## Experimental loopback notification bridge

The native `source/loopback_pair.c` establishes a TCP connection entirely over `127.0.0.1` using `socInit`/SOC socket calls and produces a read and write socket. It is testable on Linux using `make -C tests test` and on actual O3DS using the **Y-button** self-test in the homebrew app. The hardware test verifies initial unreadable state, notification delivery, `select()` readability, `recv()` and cleanup. The B-button kernel-event tests remain separate.

The optional `freerdp-3.32.2-3ds-loopback-waits.patch` attempts a first WinPR integration: anonymous pipe reads/writes use `recv/send`, event notification writes/reads use `send/recv`, and WinPR's fd-based `select()` pollset can see the same socket descriptor type as its RDP transport. The patch keeps named pipes unsupported and compiles `source/loopback_pair.c` into experimental WinPR via `WINPR_3DS_ROOT`. It does not bypass TLS or authentication. **The result must be checked in the experimental build logs and on physical hardware before it can be considered working.**

Limitations: socket creation needs SOC initialized **before** WinPR event initialization. Nintendo 3DS's socket service has a limited global descriptor budget (3dbrew documents up to 64 total sockets), and each emulated event consumes two sockets; this is likely unacceptable for unbounded event creation. `CreatePipe` currently uses nonblocking sockets, which are not fully equivalent to POSIX blocking pipes. Non-socket handles cannot be inserted into SOC `select`, and the existing native kernel-event backend still needs a separate mixed-wait strategy. Device sleep/WiFi transitions, WinPR auto-reset events, thread safety, cancellation, and resource exhaustion are not yet tested. This code is a **portability experiment**, not a production-ready implementation.

### Cross-compilation results

The `3ds-loopback-waits` patch now applies and FreeRDP's CMake generator recognizes the shared `source/loopback_pair.c` source in the top-level WinPR static library. A cross-build on October 28, 2026 progressed past `winpr/libwinpr/pipe/pipe.c` and `winpr/libwinpr/synch/event.c`; `freerdp-primitives` and `freerdp-codecs` targets compiled. The next blockers were an unused `sys/un.h` include in `winpr/libwinpr/io/io.c` (guarded for 3DS) and optional stack-unwinding's `dlfcn.h` (addressed by configuring `-DUSE_UNWIND=OFF`). Neither demonstrates a complete WinPR build.

This is deliberately **not a complete native event implementation**. Future hardware tests must measure the socket budget and loopback behavior, particularly under Wi-Fi sleep, repeated wakeups, and many simultaneous events. The native `svc` event backend from the B-button tests remains available for an eventual low-socket-count alternative.

### Timezone portability

Newlib's `struct tm` on devkitARM lacks the nonstandard `tm_gmtoff` and `tm_zone` fields that FreeRDP 3.32.2 normally uses. The `freerdp-3.32.2-3ds-portable-timezone.patch` computes the current UTC offset from standard `mktime` / `gmtime_r` / `difftime` and uses `strftime("%Z")` for the timezone abbreviation, instead of pretending the device is always UTC. This remains dependent on correct device time and timezone configuration, and daylight-saving edge cases need hardware verification. The patch does not affect TLS validation or CredSSP.

### Thread priority portability

After the timezone fix, the ARM11 compiler reached `winpr/libwinpr/thread/thread.c` and found `pthread_setschedprio()`, another POSIX scheduling function not supplied by devkitARM. A 3DS-specific conditional now avoids calling it; unlike WinPR's existing generic fallback, the 3DS path returns `ERROR_CALL_NOT_IMPLEMENTED` and `FALSE` so caller code does not assume the thread priority changed. This is not a substitute for a tested `svcSetThreadPriority` adapter.

### WinPR interface enumeration

WinPR's `WSAIoctl(SIO_GET_INTERFACE_LIST)` used Linux `net/if.h`, `struct ifreq`, and `SIOCGIFCONF`. The O3DS build now uses libctru's `SOCU_GetIPInfo()` to populate one IPv4 interface (address/netmask/broadcast), returning a WinSock error if SOC has no address or the supplied buffer is too small. No IPv6 or multi-interface enumeration is implemented, and hardware behavior is unverified. This is more faithful than defining fake Linux structures or claiming the interface list is unavailable.

### Current WinPR Winsock portability blockers

The latest ARM11 CI compiler trace (October 28, 2026) confirms FreeRDP's `freerdp-primitives` and `freerdp-codecs` targets compile, and WinPR progresses into `winpr/libwinpr/winsock/winsock.c`. The `SOCU_GetIPInfo` adapter is included but not yet fully verified because adjacent POSIX assumptions fail compilation:

- `3ds/services/soc.h` requires the libctru primitive types first; the patch now includes `3ds/types.h` before it.
- Newlib/devkitARM lacks some errno values used by WinPR's POSIX ↔ Winsock translation (`ESOCKTNOSUPPORT`, `ESHUTDOWN`, `EUSERS`, `EREMOTE`).
- The `_IOR` ioctl encoding used by `SIO_GET_INTERFACE_LIST` is not supplied by the target headers.
- `getservbyport`, `getservbyname`, `getprotobynumber` and `getprotobyname` lack target declarations and may lack implementations; do not assume the linker provides them.

Next implement a coherent 3DS Winsock compatibility adapter rather than sprinkling invented errno/ioctl values. Unsupported operations should have explicit error semantics, while supported TCP/IPv4/DNS calls should be mapped to libctru. Then continue compilation and finally link with the application's SOC initialization. Neither NLA nor desktop transport is usable yet.

## Original 3DS hardware diagnostics (October 28, 2026)

First hardware run of the two new diagnostics exposed actual portability problems:

- **B** displayed `Kernel events FAIL: Unsignaled event failed to time out`. The root cause is our wrapper: `svcWaitSynchronization` returns `0x09401BFE` on timeout, a nonnegative informational `Result`. Our code called `R_SUCCEEDED` *before* checking `RD_TIMEOUT`, treating a timeout as signal. Fixed for both single and multiple-event waits; the self-test now identifies which initial event was misclassified if another issue remains. **Awaiting repeat hardware test.**
- **Y** displayed `SOC wakeup FAIL: loopback bind (errno=22)` for a `127.0.0.1:0` TCP listener. This indicates the SOC bind call returned `EINVAL`; it does **not** independently prove the loopback address is unsupported. The test now retries **only** `127.0.0.1` with fixed high ports if port zero is rejected. If both methods fail, it prints short diagnostics: `lo0` = original loopback/port-zero bind errno, `loP` = loopback/fixed-port probe, `any` = temporary `INADDR_ANY:0` bind, `wifi` = temporary assigned-Wi-Fi-IP bind (`-1` if unavailable). Zero means the bind was accepted. The `any` and `wifi` probes do **not** listen, accept connections, or become the WinPR transport. **Awaiting repeat hardware test.**

Do not silently fall back to a LAN-facing listener for WinPR event notifications: a socket listener exposed on Wi-Fi would be accessible to other devices and could interfere with the application's event mechanism. If localhost sockets cannot work on Horizon SOC, retire the socket-pair WinPR bridge and use native kernel events plus an explicit mixed kernel-event/SOC-readiness wait strategy instead.

## Confirmed original 3DS hardware results (October 28, 2026)

Following the corrected timeout classification and loopback bind diagnostics, the user tested the updated native application on an original 3DS and reported that **both B and Y pass**:

- **B / Horizon kernel events:** the self-test checks initial timeouts, one-shot event consumption, sticky/manual reset, indexed wait-any, and a cross-thread signal/wakeup. This verifies the independent native 3DS kernel-event backend on hardware.
- **Y / SOC loopback:** the self-test checks socket-pair creation, initial `select` unreadability, a one-byte notification over the loopback transport, subsequent `select` readability, reception and drain. The socket-based wakeup mechanism operates on physical hardware after the port-zero/fixed-port fallback change.

The user has not provided the detail line identifying whether the socket test used ephemeral port zero or the fixed-loopback-port fallback. Do not assume which succeeded. This does **not** establish WinPR event correctness under resource pressure, numerous simultaneous events, concurrent resets, or suspend/resume. No actual RDP session, TLS/CredSSP login, or desktop has been validated.

## FreeRDP core build milestone and cryptography boundary (October 28, 2026)

GitHub Actions now **successfully compiles the full WinPR static-library target** (`Built target winpr`) for ARMv6K, along with `freerdp-primitives` and `freerdp-codecs`. The Mbed TLS-only FreeRDP core remains incomplete. A run reached `libfreerdp/emu/scard/smartcard_virtual_gids.c` and failed on `openssl/x509.h`; optional smartcard emulation has since been disabled via `WITH_SMARTCARD_EMULATE=OFF`. The subsequent core failure was `libfreerdp/utils/passphrase.c` including `termios.h`; the 3DS build now uses the upstream fallback that returns no terminal passphrase rather than reading secrets without protected terminal echo. An interactive 3DS credential UI will need a separate secure implementation.

**Security-critical dependency:** FreeRDP 3.32.2's `libfreerdp/crypto/certificate.c`, `crypto.c`, `tls.c`, and related headers are substantially OpenSSL-specific even with `WITH_MBEDTLS=ON`. Upstream explicitly describes Mbed TLS as experimental and warns about incompatibilities. We must not substitute fake OpenSSL declarations, ignore certificate verification, disable NLA, or claim that Mbed TLS is a drop-in replacement. The viable next investigation is either (a) a maintained, compatible static OpenSSL build for ARM11 and real native TLS/NLA, or (b) a carefully reviewed, complete Mbed TLS backend for FreeRDP's certificate/TLS abstraction. Both require substantial implementation and hardware security testing.

The build script had an unreachable duplicate CMake invocation appended after the first block; this was removed. Fast host CI now runs `bash -n scripts/build-freerdp-3ds.sh` to check syntax as well as parser and patch-format tests. No FreeRDP/WinPR library is linked into the current `.3dsx` app yet, and domain sign-in remains configuration metadata only.

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
