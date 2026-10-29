# O3DS pre-hardware runtime audit (October 2026)

This branch is stacked on draft PR #9 (framebuffer artifacts and WinPR HOME
bootstrap). It is **not merged into main** and does not assume successful
Windows authentication on a physical 3DS.

## Findings and repairs

### Saved connection before first RDP attempt
The new WinPR home/bootstrap from PR #9 created
`sdmc:/3ds/3ds-rdp` only when attempting the first *full connection*,
but the connection editor can save profiles earlier. A first-run user who
tapped Save could therefore receive an SD write error because the parent
directory did not yet exist. Initialize the application SD home at startup
before loading/saving profiles. If it cannot be created, surface an explicit
error on the upper screen rather than hiding it.

### 3DS software-keyboard applet changes GSP presentation state
Opening a field, password prompt or in-session text keyboard hands graphics
and input to the system software keyboard. After returning, recheck RGB565
pixel formats and restore top and bottom double buffering. Software consoles
are rebound by the existing PR #9 console-framebuffer patch.

### Unbounded remote desktop resize
FreeRDP calls `gdi_resize()` on server-directed desktop size changes.
While the UI intentionally offers 400x240 and 640x360, a server could request
a much larger desktop, exhausting original-3DS memory or crashing. Validate
the server-reported size against the chosen bounded profile mode before
allocating a new GDI surface; unexpected resizes return failure instead of
allocating. This is a defensive guard, not dynamic-resolution support.

### Duplicate profile names during SD load
Interactive Save intentionally overwrites a selected name, but **loading**
two differently cased names from one file previously used the same upsert
and silently replaced one saved computer. Reject duplicate names as
corruption; continue to use the existing strict recovery/backup flow.

## Validation and constraints

- Portable tests: desktop resize limit and duplicate profile rejection.
- CI: protocol tests, normal O3DS build, full FreeRDP/OpenSSL build
  (requires checking the non-optional full integration steps).
- No hardware or active Windows RDP session exercised here.
- The current top-screen viewport is still limited to 400x240 physical pixels.
- No changes to NLA, unknown-certificate approval, or password storage.

Manual smoke test: fresh SD startup -> save profile without connecting ->
open/cancel field keyboard -> connect -> open/cancel password keyboard ->
observe both LCDs -> test session and remote cursor if possible.
