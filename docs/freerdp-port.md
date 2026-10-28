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
