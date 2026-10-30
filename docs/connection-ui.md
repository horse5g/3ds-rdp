# Native 3DS Remote Desktop Connection UI

This document describes the **working experimental FreeRDP launcher and session touchpad**, redesigned after the first successful physical O3DS Windows RDP connection. The interface is inspired by Windows Remote Desktop Connection's grouped layout and tabs, but is built with our own monitor graphic and a compact RGB565 software renderer, without Citro2D.

## Screen arrangement

- **Top display (400×240):** large Remote Desktop Connection monitor/title treatment, selected computer, login metadata and status. When an RDP session connects, the entire top display switches to the remote desktop; no menu panel obscures Windows.
- **Bottom touchscreen (320×240):** the working connection settings dialog, then a laptop-style relative touchpad after connecting. A slim application bar replaces the former large title treatment; status and settings use distinct visual treatments.

### Launcher

The bottom screen uses a window-style tab strip: **General, Display, Local, Experience, Advanced**.

**General — two scrollable pages:** The first shows Computer (text entry plus saved-profile chooser), User name and Domain. The second shows Port, Save As name and read-only password guidance. Touch the narrow scrollbar at the right (upper/lower part), or move through fields with D-pad Up/Down; the UI automatically follows the focused field.

The bottom command bar provides **Save**, **Save As**, **Open**, **Connect**. Save uses the selected profile name or asks for one; Save As requires a *different* name and refuses accidental overwrite of another saved connection; Open presents the paged list of up to 12 saved profiles; Connect starts the full client in a full-client build and runs the X.224-only probe in the lightweight build. The Open list has **Back, New, Delete, Next**. Existing safeguards require confirming deletion and discarding unsaved edits.

**Display:** Remote desktop resolution dropdown (400×240 / 640×360 / 800×480 / 960×540 / 1280×720 / 1920×1080) and negotiated 16-/32-bit color-depth dropdown. The physical top screen stays 400×240. 800×480 and higher modes are experimental on the original 3DS; the server cannot force an unconfigured desktop allocation.

**Local:** Editable pointer speed (1–3×), scroll step (1–3 notches), and reverse scrolling (on/off). Profile preferences apply at connection start; the session's Settings overlay can adjust these for the current connection.

**Experience:** Editable low-bandwidth/wallpaper preference; gray read-only transport (direct TCP) and clipboard (disabled) indicators.

**Advanced:** Editable connection timeout (8/12/20 seconds), **GDI Format: XRGB32 / BGRX32** (session-only experimental decoder output format), and **Cache Password: This Run On/Off**. This cache is OFF by default, never written to SD, and retained in process RAM when returning to the launcher with cache still enabled; wiped on toggling OFF or exiting. It is valid only for the same host:port, username and domain, and is stored only after a successful NLA connection. There is no bypass of TLS or NLA. The certificate manager remains available with L+X in the launcher.

**What is actionable?** A blue-outlined white field with a dropdown arrow is an editable setting. A muted gray value row is status or unavailable capability, not a switch. The slim footer includes a clearly marked `STATUS:` message area, visually separated from action buttons. The option dropdown is a true modal list: tap a choice or use D-pad Up/Down, A to confirm; B or tapping outside cancels.

The design uses a lightweight embedded 5×7 uppercase font, so saved hostnames render in uppercase on screen but retain their original case in profile files. No passwords, tokens, TLS secrets or session traffic are saved.

## In-session trackpad

The bottom touchscreen has a four-button toolbar:

1. **Disconnect** — two-tap confirmation; START also ends the session.
2. **Settings** — live input speed, wheel step and reverse wheel direction.
3. **Keyboard** — on-screen virtual shortcut keys; X or Text opens the built-in text keyboard.
4. **Help** — dedicated overlay containing the full gestures and controller shortcuts.

The regular pad is deliberately quiet: a recessed, bordered contact surface, a small zoom indicator, a faint `TOUCHPAD / SWIPE TO MOVE` legend, two separate left/right buttons below and an independent scroll track along the right edge. Help text does not compete with the usable touch area.

Gestures and controls:

| Input | Remote effect |
| --- | --- |
| Swipe pad | Relative mouse movement, automatically scaled by remote resolution and visible zoom with subpixel accumulation; no cursor teleport |
| Quick tap | Left click |
| Physical A + swipe | Drag with the left button held |
| Physical B | Right mouse button |
| On-screen left/right zones | Click corresponding mouse button |
| Scroll strip/up/down | Remote mouse-wheel notches |
| L + Circle Pad rotation | Smooth pointer-centered local zoom, 100–500% |
| Y | Fit full remote desktop |
| R | Toggle fit and close-up |
| X / Text | Open stock 3DS text entry |
| START | Disconnect |
| SELECT | Toggle read-only performance panel during a connected session |
| L + SELECT | Toggle native LCD red/green/blue/white/yellow/cyan/magenta test strip on the top screen; independent of RDP decoding |

A tap on **Help** disables pointer/click hit-testing in the content area until closed; only the toolbar remains actionable. Opening Keyboard or Settings closes Help, and keyboard modifiers are cleared when switching modes. Settings remain session-only.

### Rendering and performance

A recent hardware test found that the experimental local `PIXEL_FORMAT_RGB16` GDI output showed only a solid color at 720p/1080p. This diagnostic build therefore requests `PIXEL_FORMAT_XRGB32` at all resolutions; it still disables oversized optional planar codec allocations at large desktop sizes. The increased primary buffer size may cause 1080p to fail on memory-constrained original hardware. A software blitter scales/samples the desktop into the top LCD's 400×240 RGB565 framebuffer. The user observed a red/blue channel reversal in early hardware builds; a reversible fix is present but still requires physical confirmation. The new 800×480 mode is an exact 2× source resolution in both top-screen dimensions, and 960×540 is a modest 16:9 option. All intermediate-resolution rendering remains experimental on O3DS.

## Profiles and security

- Profiles: `sdmc:/3ds/3ds-rdp/connections.txt`, 12 names maximum, `3DSRDP2` format with previous `3DSRDP1` compatibility. Changed UI options are only persisted on Save / Save As.
- Optional initial import: `host.txt` when no saved profiles are present.
- Certificate trust: `trusted-certs.txt`, at most 32 exact host:port SHA-256 leaf pins. `L + X` from launcher opens the certificate manager. New or name-mismatched certificates require *visible approval* with fingerprint; changed previously trusted certificates are rejected. Please independently verify the SHA-256 fingerprint with the real Windows host.
- Diagnostics: a pre-existing writable `rdplog.txt` opts into non-secret error and aggregate graphics logging.

## Development and regression testing

UI controller logic: `source/connection_ui.c`; screen drawing: `source/connection_ui_draw.c`; portable geometry: `source/connection_ui_hit.c`. Session state and gestures: `source/session_pad.c` and `include/session_pad.h`, zoom: `source/session_zoom.c`. Profile persistence: `source/profile_store.c`.

Run `make -C tests test` for portable parsers, controller-independent hit bounds, trackpad transitions, zoom, pixel conversion and desktop allocation guards. The full FreeRDP and libctru CI job builds `3ds-rdp-full.3dsx`. Both code review and physical input testing remain necessary; CI does not validate LCD color, touchpad ergonomics or live RDP frame rate.

### Large-desktop pointer feel

The pad now interprets a finger swipe in **visible LCD space**, not a
fixed remote-pixel count. The prior fixed `dx * sensitivity` movement
felt roughly 4.8 times slower on 1920x1080 than on 400x240 in fit
view. It now uses `dx * sensitivity * remote_width / (400 * zoom)`
(and the equivalent 240-high Y axis), retaining a signed Q16
subpixel remainder so single-pixel motion still works when the
viewport is highly magnified. D-pad nudges scale with the same
remote resolution and visible zoom: roughly eight LCD-equivalent
pixels at any desktop size. At 400x240 fit view the old sensitivity
is unchanged. This is a local input change, not a remote RDP display
resize or graphics security change. Portable tests cover fit,
720p/1080p, 4x/5x zoom, fractional moves and desktop clamping.

### Connected-session performance and reconnect

Press **SELECT** to show/hide the noninteractive diagnostic panel over the trackpad. It reports measured top-screen paints/second, last desktop software-blit time, the latest FreeRDP event wait, remote resolution, and primary GDI allocation KiB. It neither measures total memory use nor full network latency, and adding the panel itself requires occasional display redraws. The renderer avoids continuous extra SD logs.

After leaving a session, the client now shows **A: Reconnect** / **B: Back** on the bottom screen. Reconnection is always deliberate: a new FreeRDP client, password prompt, fresh certificate verification, and cleaned-up prior context. This adds no automatic retry or trust bypass. Repeated connections are not yet confirmed on physical O3DS, and the earlier SOC shutdown workaround is retained for program exit.

### Session-only password caching security

Under **Advanced → Cache Password**, choose **This Run: On** before connecting to opt into RAM-only password reuse. Default OFF. The password is held only after a successful NLA connection; it is scoped to the exact host, port, username and domain. Failed/cancelled connections and a switch to another endpoint wipe it. Returning from reconnect to the launcher now preserves the opt-in cache for later manual connections in the same process. Toggling the setting OFF or exiting wipes it, including explicit OpenSSL cleansing. No password is stored in `connections.txt`, a new SD credential file, or diagnostic logs. Reconnects still verify the server certificate anew, and the cache does not support unattended or automatic reconnect.

**Persistent saving across power cycles is intentionally not implemented.** A 3DS SD card is removable and cannot safely store a reusable plaintext Windows password; a persistent mode should require a separate user passphrase and an authenticated encrypted vault before it is offered.

### Compare FreeRDP decoded GDI pixel formats (experimental)

With the desktop contents and resolution kept constant, select **Advanced → GDI Format → XRGB32** and connect; then return to the launcher, select **BGRX32**, and connect again. These 32-bit output layouts differ at the *FreeRDP decoder* stage (XRGB32 MSB-first X/R/G/B versus BGRX32 MSB-first B/G/R/X); this is not the same as the L+X final conversion toggle. The application chooses the corresponding byte order automatically in its blitter. The choice is not stored in a saved profile, and 16-/32-bit network color negotiation is separate.

Each session resets its 1/8/32/96 frame color sampling counters. Logs record `gdi-init-format`, a synthetic `gdi-color-canary` generated using FreeRDP's own packing API, and normalized raw channel averages labeled `gdi-avg-rgb` in both GDI modes. The synthetic canary should pack as `00D05020` in XRGB32 or `2050D000` in BGRX32; it is **not** read from the remote Windows screen. These events contain aggregate pixel statistics only, not a screenshot. Press L+SELECT to display independent native-LCD swatches. The output and decoder experiment need O3DS hardware confirmation.

### Two-row synthetic color diagnostic

**L + SELECT** overlays a small two-row color comparison at the top of the upper LCD. Each row contains seven colored blocks: red, green, blue, white, yellow, cyan, magenta. The **top row** writes fixed RGB565 values directly to the physical screen. The **bottom row** uses `FreeRDPGetColor` and `FreeRDPWriteColor` to generate the same colors in the currently selected XRGB32 or BGRX32 output layout, then uses the client’s normal 32-bit-to-RGB565 conversion. The rows are drawn after the remote cursor to avoid obstruction. A mismatch proves a mismatch in local conversion or format handling, independent of the Windows desktop. Matching rows but incorrect Windows colors points toward FreeRDP’s actual decoded GDI pixels (or subsequent remote update handling). This diagnostic never changes the decoded desktop and does not record screenshots.

When `rdplog.txt` is enabled, `gdi-spatial-change` reports how many of the 192 sparse raw GDI sample pixels differ from the first sample on frames 1, 8, 32, and 96. This is an aggregate content-variation measure, not a color fix or a captured image. A uniformly colored remote screen with near-zero differences in these samples suggests the remote primary buffer is itself flat; an image full of varied content with near-zero differences on the LCD suggests the conversion/output stage instead.

### Source-verified pixel layout correction

FreeRDP 3.32.2's `FreeRDPWriteColor_int` writes **byte 0 from color >> 24**, then >> 16, >> 8 and the low byte. `FreeRDPGetColor` returns XRGB32 as `0x00RRGGBB` and BGRX32 as `0xBBGGRR00`. Therefore real buffer bytes are `[X,R,G,B]` for XRGB32 and `[B,G,R,X]` for BGRX32. Previous code wrongly treated XRGB32 as B/G/R/X and BGRX32 as R/G/B/X. This exactly predicts the user's wrong synthetic-strip colors and explains both the yellow cast and absent blue. The desktop path, smoothing filter, GDI aggregate statistics and both rows of synthetic diagnostic now use the corrected channel maps. The LCD's native RGB565 packing remains unchanged. Physical desktop rendering at 640p and higher is not yet re-verified.
