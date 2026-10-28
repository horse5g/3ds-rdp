# Connection UI — original Nintendo 3DS

This is a touch-first, Windows Remote Desktop Connection-inspired launcher for
the standalone 3DS client. It runs in the **normal probe build**, independent of
whether the FreeRDP/OpenSSL experiment can link. The top screen remains
available for network diagnostics and, eventually, the RDP desktop.

## Layout

Bottom screen (320x240):

- Windows-blue title bar, with a **SAVED** shortcut
- Tabs: **General**, **Display**, **Local**, **Experience**, **Advanced**
- General input rows: **Computer**, **User name**, **Domain**, **Port**, **Save as**
- Bottom actions: **Save**, **Saved**, **New**, **Connect**
- Saved view: five connections per page, **Back**, **New**, **Delete**, **Next**

General values are edited using the stock 3DS software keyboard. Field text is
case-preserved in storage even though the lightweight bitmap font renders
ASCII letters as uppercase. The bitmap UI is a dependency-free RGB565
framebuffer renderer, designed for O3DS without Citro2D allocations. The
Display, Local, Experience and Advanced tabs are **informational only**, not
editable settings yet. Do not mistake these labels for implemented features.

## Controls

| Action | Control |
| --- | --- |
| Edit field | Tap row; or D-pad Up/Down and A on General |
| Switch tabs | Tap tab; or D-pad Left/Right |
| Save / overwrite named profile | Tap Save or press Y |
| Browse saved computers | Tap Saved or press X |
| Load saved computer | Tap its row; or D-pad Up/Down then A |
| More saved computers | Tap Next (five per page) |
| New connection | Tap New |
| Delete stored computer | Tap Delete twice or SELECT twice |
| Negotiation probe | Press R |
| Connect | Tap Connect (probe-only build: probe; experimental full build: attempt FreeRDP session) |
| Experimental FreeRDP session | Hold L and press R (full build only) |
| Kernel events | B |
| SOC loopback | L+Y |
| PS entropy service | L+SELECT |
| Exit | START |

Delete requires an explicit second press. Loading another profile with unsaved
edits also requires a second selection. To avoid accidentally creating a new
connection while using L as a modifier, L alone does nothing.

## Persistence

- Profiles: `sdmc:/3ds/3ds-rdp/connections.txt`
- Fallback import when there are no profiles: `sdmc:/3ds/3ds-rdp/host.txt`
- Versioned text header: `3DSRDP1`
- Each record: `name|hostname|port|username|domain`
- 12 profiles maximum; names must be unique case-insensitively.
- Temporary-file + .bak rename provides **best-effort** replacement and
  recovery after interruption. The backup is checked if the primary is
  missing or cannot be parsed. This is not a guarantee against SD-card
  failure; back up your SD card separately.

Only connection metadata is stored. **No passwords, tokens, client secrets or
certificate bypass preferences** are accepted or persisted. A future trusted
certificate manager must enforce certificate validation; the experimental
FreeRDP build currently fails closed on unknown certificates.

## Development

- Host tests: `make -C tests test` (includes saved profiles and touch bounds)
- O3DS build: `make` in a devkitARM/libctru environment
- CI: `O3DS homebrew build`, `Protocol tests`
- Hardware behavior and frame rate on original 3DS still need measurement.

Do not treat a passing experimental full-session workflow as evidence of
functional TLS/NLA, RDP authentication or desktop graphics. Its steps are
intentionally allowed to fail during porting.


## In-session touchpad mode (experimental full-client builds)

After a FreeRDP session connects, the top 400x240 display remains the remote
desktop. The **bottom 320x240 touchscreen switches from the connection
launcher to a laptop-style trackpad**. The top edge of the *bottom screen*
has three buttons: Disconnect, Settings, Keyboard.

- The large left area is a **relative touchpad**. Swipe to move the remote
  mouse, lift, and swipe again without moving the pointer to the touch
  contact position. A quick stationary tap is a left click.
- Physical **A** is a holdable left mouse button, so hold A and swipe for
  drag-and-drop. Physical **B** is a holdable right mouse button.
- There are also on-screen left and right click buttons along the bottom.
- The right-hand strip has **UP**, a vertical drag-to-scroll region, and
  **DOWN**. Each button sends a mouse-wheel notch; vertical dragging sends
  incremental notches proportional to the swipe distance.
- **Settings** opens a lightweight overlay; tap settings again to return.
  Tap Cursor speed (1x/2x/3x), Scroll step (1/2/3 notches for buttons), or
  Reverse scroll. These preferences are **session-only** for now; they are
  not written to profiles or shared with the Windows server.
- **Keyboard** opens the libctru software keyboard; confirmed ASCII text is
  sent to the RDP session and the touchpad returns after dismissal. The
  keyboard is also available with physical X. Unicode IME, modifier keys,
  and non-ASCII remote text still need a fuller mapping.
- **Disconnect** takes two deliberate taps; pressing physical START also
  ends the session. Once disconnected, the normal launcher reappears.

The source is split between portable touch gesture logic in
`include/session_pad.h` / `source/session_pad.c`, the RGB565 pad drawing in
`source/connection_ui_draw.c`, and FreeRDP event dispatch in
`source/freerdp_session.c`. Host tests for gestures, scrolling,
clicks, and toolbar state run under `make -C tests test`.

### Local zoom with L + Circle Pad

While an experimental FreeRDP session is open, **hold L and rotate the
Circle Pad clockwise to zoom in or counterclockwise to zoom out**. One full
rotation changes magnification by approximately 100 percentage points
(subject to 100%-300% bounds). Changes interpolate smoothly; releasing L
finishes the easing animation, and recentering the stick does not jump zoom.

This is **local screen magnification**, not Ctrl+mouse-wheel and not a
Windows display-resolution change. The top screen samples a zoomed 400x240
viewport whose center follows the **remote mouse position**; near edges the
view is clamped to the desktop boundaries. Relative touchpad input and mouse
clicks continue to use unchanged remote desktop coordinates. The bottom
touchpad shows the current zoom percentage. Zoom resets to 100% for each
new session; nothing is persisted or sent to the RDP server.

The pure geometry/controller code is in `session_zoom.h/.c` and has host
tests for the rotation direction, dead zone, angle wrap, smoothing, crop
clamping and sample-point alignment. On physical hardware, confirm
`hidCircleRead()`'s directional orientation and performance before
considering the controls final.

The **normal probe-only build does not enter this mode**; it continues to
use the launcher to test X.224 security negotiation. Full session rendering,
network waits, authentication and pad input still need physical O3DS
validation. No changes were made to certificate validation.
