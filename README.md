# Remote Desktop Connection for Nintendo 3DS

A standalone **Microsoft Remote Desktop Protocol (RDP) client** for the original Nintendo 3DS/2DS, using **FreeRDP 3.32.2**, **OpenSSL 3.5.9**, devkitARM and libctru. It connects directly to a Windows RDP server over Wi-Fi; no intermediary PC or remote-desktop gateway is required.

> **Status — experimental but working on physical O3DS.** An actual Windows RDP session has connected successfully, including TLS, NLA/CredSSP and the interactive desktop. Certificate approval and returning to the Homebrew Launcher have also been tested. A hardware-observed **red/blue channel reversal** has a new test fix, but the updated colors have not yet been verified on O3DS. Higher-resolution desktop modes compile, but have not yet been verified for O3DS performance or available memory. This is not a polished, stable release.

## What it can do

- Connect to an IPv4 address or a DNS hostname using Windows username, optional domain and a configurable TCP port.
- Negotiate NLA/CredSSP, authenticate over TLS, and render an RDP desktop on the **upper 400×240 display**.
- Display a server-provided mouse cursor, with a fallback software-drawn cursor.
- Use the **lower 320×240 touchscreen as a relative laptop-style touchpad**, with tap-to-click, drag, right click, wheel controls and a software keyboard.
- Select a remote desktop size of **400×240**, **640×360**, **1280×720** or **1920×1080**. The entire desktop is scaled into the upper 3DS screen; local zoom and cursor-centered panning expose details. **1280×720 and 1920×1080 remain experimental on O3DS.**
- Create and save up to **12 named connection profiles** (host, username, domain, port and display/input preferences, never passwords).
- Inspect and manage **host:port-bound SHA-256 certificate fingerprints**, with visible confirmation on first use or when the certificate name does not match.
- Run targeted native SOC/entropy/event and FreeRDP diagnostics.

The native UI borrows the **layout and interaction conventions** of Windows Remote Desktop Connection (tabbed properties, grouped fields, dropdowns and explicit Save/Open actions), with an original monitor graphic rather than Microsoft's artwork.

## Screens and controls

**Connection launcher:** The **top screen** displays the large Remote Desktop Connection heading, monitor graphic, selected computer and connection status. The **bottom screen** is a compact, touch-operated settings dialog:

| Tab | Contents |
| --- | --- |
| **General** | Computer, User name, Domain, Port and Save As, across two pages with a scrollbar; the Computer dropdown opens saved profiles |
| **Display** | Remote desktop resolution and 16-/32-bit color depth; local zoom information |
| **Local** | Pointer speed, wheel step and reverse scroll |
| **Experience** | Low-bandwidth/wallpaper preference; read-only transport and clipboard capabilities |
| **Advanced** | Connection timeout; read-only NLA and certificate status |

**Blue-outlined fields and dropdown arrows are editable settings; muted gray rows are status, not buttons.** Tap a dropdown to choose a value or use Up/Down and A to confirm. For General, use the right-edge scrollbar or D-pad Up/Down to move between the two pages. **Save** overwrites the named connection; **Save As** requests a distinct name, **Open** browses saved connections, and **Connect** starts an RDP session in the full build. **New** is available from the Open/saved-connections view.

**During a session:** the upper screen is exclusively the remote desktop. The lower screen becomes a quiet, laptop-style pad: a large gray touch surface, separate left/right click zones, and a vertical wheel strip with Up/Down buttons. The four-button toolbar contains **Disconnect**, **Settings**, **Keyboard**, and **Help**. Help opens the full control reference instead of covering the pad with permanent instructions.

| Action | Control |
| --- | --- |
| Move pointer | Swipe on lower touchscreen; lift and swipe again without cursor teleportation |
| Left click | Tap pad or left click zone |
| Right click | Right click zone or physical **B** |
| Drag | Hold physical **A** and swipe |
| Wheel | Right-side scroll strip or Up/Down buttons |
| Keyboard / text | Toolbar **Keyboard** or physical **X**; built-in virtual keys and 3DS text entry |
| Smooth zoom in/out | Hold **L** and rotate the Circle Pad clockwise/counterclockwise |
| Compare source color-channel mapping | **L + X** toggles corrected/old red-blue output for diagnosis |
| Compare high-resolution downscaling | **L + Y** toggles four-tap smoothing when heavily zoomed out |
| Fit entire desktop | Physical **Y** |
| Toggle fit / close-up | Physical **R** |
| Disconnect | **START** or two taps on **Disconnect** |
| Session-only pad options | **Settings** (speed, wheel step, reverse direction) |
| Show controls | **Help** |

The touchscreen and on-screen keyboard are experimental. Keyboard Unicode/IME support is incomplete; the in-session speed and wheel adjustments are currently **session-only**. Display scaling is software-based; 1080p may exhaust memory or cause very low frame rates on the original 3DS.

## Install and connect

1. From a successful **FreeRDP ARM11 with OpenSSL** GitHub Actions run, download the **`3ds-rdp-full-o3ds`** artifact. This is a ZIP containing the full executable **`3ds-rdp-full.3dsx`**. The separate `freerdp-openssl-arm11-archives` artifact contains **static `.a` libraries**, not a runnable client.
2. Place `3ds-rdp-full.3dsx` in `sdmc:/3ds/3ds-rdp/` (the `/3ds/3ds-rdp/` folder on the SD card). Launch it from the Homebrew Launcher.
3. Set the **correct date and time on the 3DS**. An incorrect clock causes legitimate TLS certificates to fail their validity checks.
4. In **General**, enter the target computer's IPv4 address or hostname, username and optional domain. The default RDP port is **3389**. Set resolution/depth in **Display**. Save the connection if desired and tap **Connect**.
5. When an unknown certificate or **IP/DNS name mismatch** appears, verify its **full SHA-256 leaf certificate fingerprint independently on the Windows host** before accepting. Press **A** to trust for this session or **X** to save the exact fingerprint for this host and port. **B/START** rejects it. A previously saved certificate changing unexpectedly remains blocked.
6. Enter your password at connection time; it is never stored in a profile. After authentication, the top display shows Windows; the bottom touchscreen controls the mouse.

A Windows RDP certificate is often issued for a hostname rather than a numeric IP. **Connecting using a hostname on that certificate, or issuing a certificate with the correct IP Subject Alternative Name, is preferred** to approving a mismatch. Never approve a fingerprint you have not verified.

The SD folder may also contain an optional legacy `host.txt` (used as an initial import if there are no saved connections):

```ini
host=rdp01.example.com
port=3389
domain=EXAMPLE
username=alice
```

Username/domain can also be entered through the graphical editor. A password field in `host.txt` is **not supported**.

## Files and security

| SD card file | Purpose |
| --- | --- |
| `connections.txt` | Versioned saved host/profile preferences (`3DSRDP2`; older `3DSRDP1` loads with defaults) |
| `trusted-certs.txt` | Explicitly trusted host:port and SHA-256 leaf certificate pins, up to 32 endpoints |
| `cert-pin.txt` | Optional pre-provisioned certificate SHA-256 pin |
| `host.txt` | Optional legacy initial connection settings |
| `rdplog.txt` | **Opt-in diagnostic events**: logging runs only if the file already exists and is writable |

Unknown certificates are **not automatically accepted**, even if the server is on your LAN. A verified and explicitly approved fingerprint can provide endpoint pinning when the X.509 IP/DNS name does not match. Certificate validity checks remain mandatory and changed saved pins are rejected. No password is written to saved profiles, the trust store or diagnostic logs.

For managing certificates on device, hold **L and press X** in the launcher. This lets you inspect or remove saved trust entries with confirmation. For a deeper description see [Certificate pinning](docs/certificate-pinning.md).

## Launcher diagnostics

| Control | Action |
| --- | --- |
| **R** | Non-authenticated X.224/RDP negotiation probe |
| **L + R** | Full FreeRDP session (alternative to tapping Connect) |
| **L + X** | Trusted certificates manager |
| **B** | Horizon kernel event self-test |
| **L + Y** | SOC loopback self-test |
| **L + SELECT** | 3DS PS-service entropy self-test |
| **START** | Exit |

The normal `3ds-rdp.3dsx` target is a lighter-weight **probe-only** app, not the full RDP client. For actual desktop sessions install `3ds-rdp-full.3dsx`.

## Build from source

Install the [devkitPro 3DS development environment](https://devkitpro.org/wiki/Getting_Started) (devkitARM, libctru, compatible CMake and native build tools). To build the lightweight negotiation probe:

```sh
make
```

To build/link the full FreeRDP/OpenSSL client:

```sh
bash scripts/build-openssl-3ds.sh build
FREERDP_CRYPTO_BACKEND=openssl bash scripts/build-freerdp-3ds.sh build
bash scripts/link-freerdp-3ds.sh
bash scripts/build-combined-3dsx.sh
```

Portable/native regression tests:

```sh
make -C tests test
```

CI results and the full executable artifact appear under [GitHub Actions](https://github.com/horse5g/3ds-rdp/actions). Experimental work is developed and hardware-tested on feature/fix branches; CI builds **do not** prove that an untested feature works on a real 3DS.

## Known issues and current roadmap

- **Color rendering:** a physical O3DS test found red and blue reversed (yellow becomes cyan). A targeted, runtime-toggleable desktop conversion correction has been added; confirm colors with L+X on hardware. The local cursor is unchanged.
- **O3DS performance:** a hardware log showed 720p/1080p connections failing before TCP with no error code, consistent with large planar codec buffers. The O3DS build skips those optional buffers at higher resolutions and now uses **FreeRDP's native RGB565 GDI primary surface** for 720p/1080p, halving that surface's memory use versus 32-bit output (about 1.84 MB at 720p, 4.15 MB at 1080p). This 16-bit local surface is separate from the negotiated 16-/32-bit network setting and still requires physical testing. An optional four-tap downscaler trades additional pixel reads for less aliasing; 1080p may remain impractical.
- **Network transport:** direct IPv4 TCP only; no RDP gateway, UDP multitransport or clipboard redirection in the current O3DS build.
- **Input:** software keyboard text previously failed; new US-layout ASCII scan-code injection uses the same input path as the built-in virtual keys. Unicode/IME and other keyboard layouts are not supported. The fix and additional keyboard diagnostic counts need on-device testing.
- **Shutdown:** after full FreeRDP use, an O3DS-specific workaround defers a blocking SOC shutdown to process teardown. It has restored normal exit in physical testing but needs broader verification.

Next priorities: resolve GDI color decoding, validate frame rate and memory at higher resolutions, refine trackpad and focus behavior on hardware, and improve keyboard/input coverage.

For implementation details see [UI and controls](docs/connection-ui.md), [FreeRDP 3DS port](docs/freerdp-port.md), [TLS and certificate trust](docs/certificate-pinning.md), and [connection worker architecture](docs/connect-thread.md).

Protocol references: [FreeRDP](https://github.com/FreeRDP/FreeRDP) · [Microsoft MS-RDPBCGR](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-rdpbcgr/) · [devkitPro](https://devkitpro.org/).

**Use only with remote computers you own or are authorized to access.** Do not expose an unprotected RDP service directly to the public internet.
