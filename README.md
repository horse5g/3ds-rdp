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

Expected output: `3ds-rdp.3dsx`. GitHub Actions has successfully compiled this homebrew target using the official devkitPro Docker image; **physical O3DS hardware testing is still pending**.

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

Press **A** to send an RDP negotiation probe, **B** to run native Horizon kernel event tests, **X** to reload `host.txt`, or **START** to exit. The bottom screen displays the parsed username and domain. Those fields are **not transmitted to Windows** in this milestone. Domain sign-in requires future FreeRDP + CredSSP/NLA integration (and Kerberos if domain policy requires it).

## Horizon synchronization prototype

The app now includes a small kernel-event backend in `source/horizon_sync.c` (separate from FreeRDP) using the actual 3DS `svcCreateEvent`, `svcSignalEvent`, `svcClearEvent` and `svcWaitSynchronization` APIs. It supports sticky (manual-reset) and one-shot (auto-reset) kernel events, a nonblocking poll, bounded waits, and an infinite wait.

Press **B** on your original 3DS to test unsignaled timeouts, auto-reset consumption, manual-reset persistence and clearing, plus cross-thread wakeup from a native libctru thread. Record the on-screen PASS/FAIL output. This is intended to establish real kernel-event behavior before connecting WinPR's event/wait layers to it.

**It is not connected to WinPR yet.** Kernel handles are not POSIX file descriptors, so the existing WinPR `pollset` and socket wait code must be adapted separately. The FreeRDP cross-build still stops at its pipe/event implementation.

## FreeRDP / WinPR port investigation

A separate [ARM11 FreeRDP build investigation](docs/freerdp-port.md) now provides a pinned upstream release, devkitARM CMake toolchain, experimental cross-compilation command and diagnostic CI. **This does not yet mean FreeRDP can build or run on O3DS:** the normal app is still a standalone RDP security-negotiation probe. The separate O3DS CI workflow will publish a test `.3dsx` artifact if the devkitARM build succeeds.

## Planned work

1. Validate device build, networking, and received X.224 responses on O3DS.
2. Prototype minimal FreeRDP + WinPR cross-compile for armv6k, review TLS dependencies.
3. Add correctly validated TLS/CredSSP authentication, preserving NLA.
4. Render bitmap/GDI updates on top screen, then implement touch input, keyboard, dirty rectangles and performance telemetry.

References: [FreeRDP](https://github.com/FreeRDP/FreeRDP), [devkitPro 3DS examples](https://github.com/devkitPro/3ds-examples), [Microsoft MS-RDPBCGR](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-rdpbcgr/).

## Security

This first prototype never handles credentials; domain and username are local metadata for the future authentication implementation. Only connect to systems you control or are authorized to access. Do not expose RDP directly to the internet for testing.
