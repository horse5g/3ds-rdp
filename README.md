# 3DS RDP (O3DS)

Experimental **standalone** Microsoft RDP client for the original Nintendo 3DS/2DS. Built using devkitARM/libctru. The aim is direct connections to Windows, without a gateway.

## Current status: X.224 security negotiation probe

This is a first milestone, **not yet an interactive remote desktop client**.

- Reads IPv4 address **or DNS hostname**, port, and optional Windows domain/account metadata from SD card.
- Resolves an IPv4 address using libctru's DNS resolver and opens direct TCP to port 3389 (or configured port).
- Sends TPKT / X.224 Connection Request and RDP_NEG_REQ advertising TLS and CredSSP (NLA).
- Reads server's X.224 Connection Confirm, reporting selected protocol or failure.
- Disconnects without sending passwords or starting TLS.

No authentication, graphics, mouse or keyboard input is implemented.

## Build

Install [devkitPro 3DS development tools](https://devkitpro.org/wiki/Getting_Started) including devkitARM/libctru. In devkitPro MSYS2 or correctly configured Linux:

```sh
make
```

Expected output: `3ds-rdp.3dsx`. GitHub Actions has successfully compiled this homebrew target using the official devkitPro Docker image; **the B/Y diagnostics have now been tested successfully on an original 3DS**; authentication and desktop rendering are not implemented.

Portable packet parser tests (GCC or Clang):

```sh
make -C tests test
```

## SD card configuration

Put the compiled `3ds-rdp.3dsx` at `/3ds/3ds-rdp/` on your SD card. Create `/3ds/3ds-rdp/host.txt`:

```ini
host=workstation.corp.example.com
port=3389
domain=CORP
username=alice
```

- `host`: IPv4 address or DNS name, including domain-joined machine FQDN. **IPv4 only** for now; the 3DS's configured DNS server must be able to resolve internal names.
- `port`: optional TCP port, defaults to 3389.
- `domain`: optional Windows Active Directory domain (NetBIOS `CORP` or DNS-style `corp.example.com`).
- `username`: optional. Supports a simple `alice`, `CORP\\alice` (automatically extracts the domain), or UPN `alice@corp.example.com`. For a UPN, omit the separate `domain` key.

Legacy files containing only `192.168.1.100:3389` or `rdp01.corp.example.com` still work. Lines beginning with `#` or `;` are comments. Unknown settings, including `password=`, are rejected intentionally; **do not save passwords on the SD card**.

With the connection launcher, press **R** to probe the selected host; use **B** for native kernel-event tests, **L+Y** for SOC loopback, **L+SELECT** for PS entropy, or **START** to exit. The bottom screen edits and saves host/user/domain settings; the normal probe still does **not** send credentials. Actual Windows sign-in requires the experimental FreeRDP/CredSSP/NLA integration.

## Windows-style connection launcher (draft PR #3)

The bottom touchscreen now has a native RGB565, Windows Remote Desktop
Connection-inspired interface with General/Display/Local/Experience/Advanced
tabs. General lets you type **Computer, User name, Domain, Port, Save as** using
the 3DS on-screen keyboard. Save and browse up to 12 named connections under
`/3ds/3ds-rdp/connections.txt`. Existing `host.txt` can be imported as an
unsaved connection. Stored passwords are never supported.

Tap **Connect** (or press R) to probe an RDP server in the normal build. The
experimental full-client build instead attempts a FreeRDP session when Connect
is tapped; hold L+R for that same experimental attempt. B still tests Horizon
events; L+Y checks SOC loopback and L+SELECT checks PS entropy. START exits.
Other tabs currently explain fixed settings and limitations; they are **not**
working configuration switches.

See [Connection UI details](docs/connection-ui.md). This interface is a build
milestone, **not** evidence that Windows login or desktop rendering work.

## Integrated FreeRDP + OpenSSL client (ARM11 build verified)

The existing `3ds-rdp.3dsx` remains a negotiation/diagnostic probe. The
separate `3ds-rdp-full.3dsx` is now **successfully cross-compiled, linked,
and packaged** against FreeRDP 3.32.2, PS-backed OpenSSL 3.5.9, WinPR,
and libctru in [CI run 40672154636](https://github.com/horse5g/3ds-rdp/actions/runs/40672154636). To reproduce:

```bash
bash scripts/build-openssl-3ds.sh build
FREERDP_CRYPTO_BACKEND=openssl bash scripts/build-freerdp-3ds.sh build
bash scripts/link-freerdp-3ds.sh
bash scripts/build-combined-3dsx.sh
```

**Build status:** FreeRDP core, full-session source, combined ELF and
`3ds-rdp-full.3dsx` all passed the final CI gate. Retrieve the
[full application and static archives](https://github.com/horse5g/3ds-rdp/actions/runs/40672154636/artifacts/12808337729)
from the successful workflow. **This is a build-only milestone:** the full app
has not run on O3DS; certificate verification and hardware runtime
validation are still incomplete. **New experimental TLS trust gate:** put a
manually verified, host-and-port-specific SHA-256 pin in
`sdmc:/3ds/3ds-rdp/cert-pin.txt` before pressing R. The client rejects
unconfigured, changed or mismatched server certificates without asking to
accept them. See [certificate pin provisioning](docs/certificate-pinning.md). In the opt-in app,
R attempts a session, B/START exits, A clicks, touch moves the pointer, and
X opens a limited text keyboard. This code has **not been hardware tested**.
Unknown or changed RDP certificates must not be accepted automatically, and
passwords are entered at runtime, never loaded from `host.txt`.
See [porting details](docs/freerdp-port.md) for remaining trust, login, rendering
and memory limitations.

## Hardware entropy-service diagnostic

Press **SELECT** to check `psInit()` and two 32-byte calls to the console's `PS_GenerateRandomBytes()` service. The test checks that neither output is all zero and that the buffers differ, then wipes the temporary buffers without printing or saving their contents. A PASS confirms service availability **only**; it does not prove unpredictability, seed OpenSSL's DRBG, enable TLS, or expose passwords. It still needs testing on physical original 3DS hardware.

## Horizon synchronization prototype

The app now includes a small kernel-event backend in `source/horizon_sync.c` (separate from FreeRDP) using the actual 3DS `svcCreateEvent`, `svcSignalEvent`, `svcClearEvent` and `svcWaitSynchronization` APIs. It supports sticky (manual-reset) and one-shot (auto-reset) kernel events, a nonblocking poll, bounded waits, and an infinite wait.

Press **B** on your original 3DS to test unsignaled timeouts, auto-reset consumption, manual-reset persistence and clearing, plus cross-thread wakeup from a native libctru thread. The B-button diagnostic **passed on original 3DS hardware** after correcting timeout result handling. This tests the kernel event wrapper independently of the experimental WinPR bridge.

**It is not connected to WinPR yet.** Kernel handles are not POSIX file descriptors, so the existing WinPR `pollset` and socket wait code must be adapted separately. An experimental socket-backed WinPR event/pipe patch now compiles, but its runtime behavior under FreeRDP has not been verified.

## Experimental WinPR socket-notification bridge

The source now includes `loopback_pair.c`, a bounded IPv4 localhost TCP pair using libctru's SOC sockets. On original 3DS hardware, press **Y** to test local `bind`/`listen`/`connect`/`accept`, `select()` wakeups, `send()/recv()`, and cleanup. This test does not need an RDP server, but it requires successful `socInit()`. Failures report the stage and `errno`.

A new **experimental upstream patch** changes WinPR's anonymous pipes and event-fd notifications to use these socket pairs, allowing the same SOC `select()` path to observe event notifications and TCP RDP socket readiness. Unix-domain named-pipe creation explicitly returns unsupported.

**Resource and correctness warning:** 3DS SOC has a limited global socket budget; every emulated WinPR event consumes **two sockets**, and pair construction transiently consumes a third listening socket. The standalone Y-button SOC transport test **passed on original 3DS hardware**. The complete WinPR adapter is not linked into the homebrew application and has not been exercised in a real RDP session. Repeated signaling, timeouts, auto-reset semantics, mixed waits, memory pressure, network initialization, and socket exhaustion need more work. This is a diagnostic porting strategy, not a finished RDP synchronization backend.

## FreeRDP / WinPR port investigation

A separate [ARM11 FreeRDP build investigation](docs/freerdp-port.md) now provides a pinned upstream release, devkitARM CMake toolchain, experimental cross-compilation command and diagnostic CI. **WinPR itself now cross-compiles, but the full FreeRDP core does not yet build or run on O3DS:** the normal app is still a standalone RDP security-negotiation probe. The separate O3DS CI workflow will publish a test `.3dsx` artifact if the devkitARM build succeeds.

## Verified ARM11 port progress

The experimental build now produces the complete `winpr` static-library target for O3DS alongside `freerdp-primitives` and `freerdp-codecs`. The main FreeRDP library is **not** complete: FreeRDP 3.32.2 contains OpenSSL-specific TLS, certificate and HTTP code despite its experimental Mbed TLS selection. Optional smart-card emulation and desktop-terminal passphrase handling are excluded for the current 3DS target, without changing certificate verification or NLA behavior.

An independent [OpenSSL ARM11 build experiment](docs/openssl-arm11.md) now targets pinned OpenSSL 3.5.9 with actual pthread support and libctru socket compatibility. Static compilation remains experimental; the full FreeRDP core is not linked. Neither approach has passed a native 3DS link or TLS handshake yet. See [port notes](docs/freerdp-port.md) for the exact compiler findings.

An isolated [OpenSSL RNG smoke-test](docs/openssl-arm11.md) is now being
built as a separate experimental `.3dsx`. Its `RAND_bytes()` source is
wired to the 3DS PS cryptographic random service; it does not connect to a
Windows server and still requires hardware validation. The normal RDP probe
remains independent of these cryptographic experiments.

## Planned work

1. Validate device build, networking, and received X.224 responses on O3DS.
2. Prototype minimal FreeRDP + WinPR cross-compile for armv6k, review TLS dependencies.
3. Add correctly validated TLS/CredSSP authentication, preserving NLA.
4. Render bitmap/GDI updates on top screen, then implement touch input, keyboard, dirty rectangles and performance telemetry.

References: [FreeRDP](https://github.com/FreeRDP/FreeRDP), [devkitPro 3DS examples](https://github.com/devkitPro/3ds-examples), [Microsoft MS-RDPBCGR](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-rdpbcgr/).

## Security

This first prototype never handles credentials; domain and username are local metadata for the future authentication implementation. Only connect to systems you control or are authorized to access. Do not expose RDP directly to the internet for testing.
