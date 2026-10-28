# 3DS RDP (O3DS)

Experimental **standalone** Microsoft RDP client for the original Nintendo 3DS/2DS. Built using devkitARM/libctru. The aim is direct connections to Windows, without a gateway.

## Current status: X.224 security negotiation probe

This is a first milestone, **not yet an interactive remote desktop client**.

- Reads IPv4 address and port from SD card.
- Opens direct TCP connection to port 3389 (or configured port).
- Sends TPKT / X.224 Connection Request and RDP_NEG_REQ advertising TLS and CredSSP (NLA).
- Reads server's X.224 Connection Confirm, reporting selected protocol or failure.
- Disconnects without sending passwords or starting TLS.

No authentication, graphics, mouse or keyboard input is implemented.

## Build

Install [devkitPro 3DS development tools](https://devkitpro.org/wiki/Getting_Started) including devkitARM/libctru. In devkitPro MSYS2 or correctly configured Linux:

```sh
make
```

Expected output: `3ds-rdp.3dsx`. The project has **not yet been built with devkitARM or run on hardware** in this development environment; please report compiler output or 3DS behavior for validation.

Portable packet parser tests (GCC or Clang):

```sh
make -C tests test
```

## SD card configuration

Put the built `3ds-rdp.3dsx` in `/3ds/3ds-rdp/`. Make `/3ds/3ds-rdp/host.txt` with an IPv4 address and optional port:

```text
192.168.1.100:3389
```

Launch via Homebrew Launcher. Press **A** to probe, **X** to reload config, **START** to exit. The top screen displays negotiated protocol, network errors or negotiation failures. Use a Windows Pro/Enterprise/Server host with Remote Desktop enabled, or another compatible RDP server. Keep NLA enabled.

The response is only an indication of supported **security negotiation**, not a successful login.

## FreeRDP / WinPR port investigation

A separate [ARM11 FreeRDP build investigation](docs/freerdp-port.md) now provides a pinned upstream release, devkitARM CMake toolchain, experimental cross-compilation command and diagnostic CI. **This does not yet mean FreeRDP can build or run on O3DS:** the normal app is still a standalone RDP security-negotiation probe. The separate O3DS CI workflow will publish a test `.3dsx` artifact if the devkitARM build succeeds.

## Planned work

1. Validate device build, networking, and received X.224 responses on O3DS.
2. Prototype minimal FreeRDP + WinPR cross-compile for armv6k, review TLS dependencies.
3. Add correctly validated TLS/CredSSP authentication, preserving NLA.
4. Render bitmap/GDI updates on top screen, then implement touch input, keyboard, dirty rectangles and performance telemetry.

References: [FreeRDP](https://github.com/FreeRDP/FreeRDP), [devkitPro 3DS examples](https://github.com/devkitPro/3ds-examples), [Microsoft MS-RDPBCGR](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-rdpbcgr/).

## Security

This first prototype never handles credentials. Only connect to systems you control or are authorized to access. Do not expose RDP directly to the internet for testing.
