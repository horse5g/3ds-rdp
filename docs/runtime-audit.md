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

### Interrupted SOC select() retry
The native RDP probe's `waitfd()` previously initialized `fd_set` and the
three-second timeout **once**, then retried `select()` on `EINTR` without
resetting either argument. POSIX allows `select()` to mutate both objects;
Horizon/SOC signals could therefore trigger a spurious failed or instantly
timed-out socket operation. Reinitialize the descriptor set and timeout on
every retry. A portable injected-`select` test explicitly clobbers both
on its first interrupted call and asserts they are restored on retry.

### On-screen keyboard page and stale modifiers

The F1–F12 key page previously had no way back to the normal modifier,
navigation, and text keys without closing the panel; the UI labeled the
function-page action as `F1-F12` even while already on that page. The
`F1-F12 / BASIC` toggle now stays in the same touchscreen cell on both
pages, and regression tests cover switching back.

Keyboard modifiers are sticky intentionally while the key panel is open,
but must not remain invisible/latched after closing it. Clear Ctrl/Alt/
Shift/Win on close and when switching to the Settings overlay; opening
Keyboard replaces Settings instead of allowing the two overlays to conflict.
Tests exercise both toolbar and on-screen transitions.

### Full RDP session startup / NLA audit

Traced connection startup all the way through WinPR known paths, password
entry, `freerdp_new`, `freerdp_context_new`, validated FreeRDP settings,
threaded TLS/NLA connect, certificate callback, `PostConnect` graphics
allocation, and first desktop paint.

- **Missing NLA username:** The network probe intentionally accepts just a
  hostname; the full FreeRDP client did the same, creating an unusable blank
  user CredSSP identity. A separate portable preflight now validates the
  selected profile and options and requires a username *before* allocating
  WinPR client settings. It preserves passwordless profile storage.
- **Password entry before context allocation:** The app previously prompted
  for a secret even when `freerdp_context_new()` was about to fail on WinPR
  paths or memory. FreeRDP client/context setup now happens **before** the
  password applet, so the user gets the initialization error without
  re-entering credentials. The password is still only held transiently in
  RAM and never saved.
- **OpenSSL PS entropy failure:** TLS/NLA previously began without a specific
  entropy diagnostic. A 32-byte `RAND_bytes()` preflight runs after WinPR
  HOME/XDG initialization and before the password prompt. The bytes are
  immediately cleansed, never logged/stored, and failure is explicitly
  identified as the 3DS PS/OpenSSL backend. The patched OpenSSL entropy
  provider already calls `psInit`/`PS_GenerateRandomBytes` and fails
  closed if PS does not work.
- **Unsafe initial desktop allocation:** The resize callback added in the
  previous audit protected later `gdi_resize`, but the **initial**
  `PostConnect` call made `gdi_init` *before* validating negotiated
  dimensions. Now reject unsupported or changed desktop sizes prior to
  any initial GDI framebuffer allocation; report a specific graphics/O3DS
  memory error if `gdi_init` fails.
- **Opaque failures:** Map known FreeRDP error codes to useful DNS, TCP,
  NLA security, TLS, credential, account permission, and desktop-activation
  error messages while retaining the hexadecimal code. The lower-screen
  error dialog wraps longer messages so the numeric code remains visible.
  Certificate pin changes and unknown certificate refusals still take
  precedence.
- **Null context settings:** Check client settings after context allocation
  before using them, rather than unconditionally dereferencing the pointer.

The check for a nonempty username applies **only to the full NLA session**;
the probe-only TCP/negotiation client still permits host-only profiles.
Also note that the ARM11 FreeRDP build intentionally disables Kerberos/GSSAPI
and only supports NTLM-compatible authentication. A server requiring
Kerberos-only domain sign-on will not work without additional porting.
Neither NLA nor hostname/certificate verification is disabled.

### Additional context allocation trap: WinPR workstation name

FreeRDP 3.32.2 calls `settings_init_computer_name()` during
`freerdp_settings_new()` (before `freerdp_context_new()` succeeds).
That function invokes WinPR `GetComputerNameExA(ComputerNameNetBIOS)`,
which calls `gethostname()`. A 3DS does not have a guaranteed POSIX
hostname, so the original context allocation can still fail **even after
setting HOME**.

A narrowly scoped `__3DS__` WinPR source patch supplies the harmless
client workstation label `NINTENDO3DS` when `gethostname()` fails or
returns empty. Valid hostnames are preserved. This string is a protocol
client label **only**, not a Windows username, certificate identity,
TLS peer validation or Unix user identity. Other platforms are unchanged.

### Case-insensitive DNS certificate endpoint lookup

DNS hostnames are ASCII case-insensitive but certificate pins previously
used exact `strcmp()`; `OfficeHost` and `officehost` could be treated
as distinct endpoints even at the same TCP port. Matching is now limited
to ASCII letter-case folding for the **full hostname plus port**; suffixes,
trailing dots and wildcard forms are **not** allowed to match. SHA-256
fingerprints still have to match exactly. Duplicates with different
case are rejected during pool parsing, including changed fingerprints.

### Do not advertise unimplemented transport features

Upstream FreeRDP settings initialize `FreeRDP_SupportMultitransport`
and `FreeRDP_RedirectClipboard` as enabled, although this homebrew build
deliberately uses IPv4/TCP transport and configures
`-DWITH_CHANNELS=OFF`. Before starting the connection, explicitly set
both options to `FALSE` to avoid requesting RDP UDP multitransport or a
clipboard channel that the current O3DS client cannot use. This does not
downgrade CredSSP/NLA or TLS; all current certificate requirements stay
enabled. Future channel work can re-enable individual features only when
they have a matching implementation and tests.

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
