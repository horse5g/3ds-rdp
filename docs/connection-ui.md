# Native 3DS Remote Desktop Connection UI

This document describes the **working experimental FreeRDP launcher and session touchpad**, redesigned after the first successful physical O3DS Windows RDP connection. The interface is inspired by Windows Remote Desktop Connection's grouped layout and tabs, but is built with our own monitor graphic and a compact RGB565 software renderer, without Citro2D.

## Screen arrangement

- **Top display (400×240):** large Remote Desktop Connection monitor/title treatment, selected computer, login metadata and status. When an RDP session connects, the entire top display switches to the remote desktop; no menu panel obscures Windows.
- **Bottom touchscreen (320×240):** the working connection settings dialog, then a laptop-style relative touchpad after connecting. A slim application bar replaces the former large title treatment; status and settings use distinct visual treatments.

### Launcher

The bottom screen uses a window-style tab strip: **General, Display, Local, Experience, Advanced**.

**General — two scrollable pages:** The first shows Computer (text entry plus saved-profile chooser), User name and Domain. The second shows Port, Save As name and read-only password guidance. Touch the narrow scrollbar at the right (upper/lower part), or move through fields with D-pad Up/Down; the UI automatically follows the focused field.

The bottom command bar provides **Save**, **Save As**, **Open**, **Connect**. Save uses the selected profile name or asks for one; Save As requires a *different* name and refuses accidental overwrite of another saved connection; Open presents the paged list of up to 12 saved profiles; Connect starts the full client in a full-client build and runs the X.224-only probe in the lightweight build. The Open list has **Back, New, Delete, Next**. Existing safeguards require confirming deletion and discarding unsaved edits.

**Display:** Remote desktop resolution dropdown (400×240 / 640×360 / 1280×720 / 1920×1080) and negotiated 16-/32-bit color-depth dropdown. The physical top screen stays 400×240. 720p and 1080p are experimental on the original 3DS; the server cannot force an unconfigured desktop allocation.

**Local:** Editable pointer speed (1–3×), scroll step (1–3 notches), and reverse scrolling (on/off). Profile preferences apply at connection start; the session's Settings overlay can adjust these for the current connection.

**Experience:** Editable low-bandwidth/wallpaper preference; gray read-only transport (direct TCP) and clipboard (disabled) indicators.

**Advanced:** Editable connection timeout (8/12/20 seconds), read-only NLA-required and trusted-certificate manager shortcut notes. There is no bypass of TLS or NLA.

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

A tap on **Help** disables pointer/click hit-testing in the content area until closed; only the toolbar remains actionable. Opening Keyboard or Settings closes Help, and keyboard modifiers are cleared when switching modes. Settings remain session-only.

### Rendering and performance

FreeRDP decodes a full remote desktop into a GDI `PIXEL_FORMAT_XRGB32` buffer; a software blitter scales/samples it into the upper LCD's native 400×240 RGB565 buffer. High desktop resolutions preserve the true remote pointer coordinates while zooming/panning locally. The source reports a red/yellow rendering tint on physical hardware despite a correctly colored local cursor; opt-in aggregate GDI color diagnostics are in development. 1080p has a primary 32-bit GDI buffer of at least 8.3 MB before codec caches; test 720p first on original 3DS.

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
