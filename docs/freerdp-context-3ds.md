# Original 3DS FreeRDP context initialization tracing

## Physical-device result (29 October 2026)

The O3DS OpenSSL smoke test now **passes** after the MIT-compatible pthread
thread-specific storage bridge. The full FreeRDP/OpenSSL `.3dsx` advances to
`FreeRDP context init failed (WinPR/heap)`, before the password dialog.

This message is a generic app-side error, not evidence of a specific
WinPR heap allocation failure. FreeRDP 3.32.2's
`freerdp_context_new_ex()` frees partially constructed context before
returning `FALSE`, obscuring which dependency failed.

## O3DS-only context markers

`patches/freerdp-3.32.2-3ds-context-stages.patch` adds two small
read-only diagnostic functions to the pinned FreeRDP sources. These
report only stage numbers, never credentials, memory contents, certificates,
random bytes or hostnames:

- `freerdp_3ds_context_init_stage()` (C): outer context creation.
- `freerdp_3ds_rdp_init_stage()` (R): nested `rdp_new()` and
  `rdp_new_common()` initialization.

They retain the last attempted operation after FreeRDP's failure cleanup.
The app displays `FreeRDP ctx C# R#`, with `R0` when the failing
outer stage did not enter `rdp_new()`.

### Context stage C

| C | Stage being attempted |
|---|---|
| 1 | Context allocation |
| 2 | WinPR logger |
| 3 | Metrics |
| 4 | `rdp_new` subtree; read R |
| 5 | PubSub event bus |
| 6 | Graphics |
| 7 | Error description buffer |
| 8 | WinPR channel-error event |
| 9 | Channel manager |
| 10 | Stream dump |
| 11 | Common access-token callback |
| 12 | Optional client `ContextNew` callback |

### RDP stage R (when C4)

| R | Stage being attempted |
|---|---|
| 1 | RDP object allocation |
| 2 | RDP logger |
| 3 | Log context |
| 4 | Initial client settings creation |
| 5 | Clone and remote/backup settings |
| 6 | Entering nested RDP dependency constructors |
| 7 | Transport callback retrieval and I/O allocation |
| 8 | Input |
| 9 | Update |
| 10 | Redirection |
| 11 | Autodetection |
| 12 | Heartbeat |
| 13 | Multitransport object |
| 14 | Bulk/compression |
| 15 | PubSub event bus |
| 16 | WinPR abort event |
| 17 | RDP timer |
| 101 | Nested transport object |
| 102 | Applying transport I/O callbacks (if used) |
| 103 | AAD adapter |
| 104 | Negotiation |
| 105 | MCS |
| 106 | Licensing |
| 107 | Fastpath |

Stages are reported *at call entry*, not only upon failure.
If a construction call returns false, the marker remains on its stage.
The RDP marker is relevant only when outer stage is C4; when outer
stage is greater than 4, it may reflect successful earlier construction.

## Next hardware test

Build the full client from the new integrated FreeRDP/OpenSSL workflow
and copy **only** `3ds-rdp-full.3dsx` to the SD card.
Try the same connection profile and report the exact `C# R#` message.
Do not merge draft PR #11 until a real connection succeeds.

This is a diagnostic, not a runtime fix. Once the precise stage is
known, inspect that constructor and its WinPR/libctru dependencies,
then retest. Do not mask failures by disabling NLA, encryption, certificate
checks or threaded operation.

## Opt-in SD diagnostic log

Logging is **disabled by default**. To opt in, create an **existing,
writable file** `sdmc:/3ds/3ds-rdp/rdplog.txt` (for example, an empty text
file) **before launching the client**. The file's presence is the enable
switch and the same file receives diagnostic output. The app will never
create `rdplog.txt` when it is missing.

On startup after initializing the SD home, the app checks for the existing
file in `rb+` mode. It then appends single-line event records
`#sequence event HEX_A HEX_B HEX_C`; all writes are immediately flushed
so a crash still leaves the most recent completed checkpoint. This is a
bounded opt-in facility: once the file reaches approximately 128 KiB,
new records stop. To start clean, empty the file on your computer and
relaunch. Do **not** delete it if you wish to keep logging enabled.

The event log covers app startup/exit, connection attempts, WinPR
environment checks, OpenSSL DRBG/provider readiness, `freerdp_new`,
retained context failure C/R stage, numeric `errno` and WinPR
`GetLastError`, password dialog begin/cancel **without reading
password text**, network-worker start/finish, connection error code,
disconnect, and cleanup. The three numeric fields depend on the event
and are non-sensitive diagnostic statuses, stage numbers, and error codes.
There is no freeform dump of WinPR/FreeRDP logger text.

For example, a record named `freerdp-context-fail` has arguments
`C-stage, R-stage, errno` in hexadecimal; the subsequent
`freerdp-context-winpr` records WinPR's last-error value.

No password, username, domain, hostname/IP, certificate, certificate
fingerprint, or raw entropy is intentionally written. The file is still
a local diagnostic artifact: review it before sharing. Logging adds
SD I/O on recorded checkpoints, so remove or rename `rdplog.txt`
and relaunch to disable it. The main session remains safe to start
without the file.

## Physical O3DS result: C4 R17 is the FreeRDP timer

The attached physical-device rdplog.txt records two identical failures after
successful OpenSSL readiness:
```text
freerdp-context-fail 00000004 00000011 0000000B
freerdp-context-winpr 000000CB 00000000 00000000
```
The values are hex: C4, R17, errno 11 (EAGAIN). WinPR 0xCB (203)
could be stale and does not necessarily identify the timer failure.

In pinned FreeRDP 3.32.2, libfreerdp/core/rdp.c stage R17 calls
freerdp_timer_new(). **R17 covers multiple operations**: allocation,
entry-list creation, two WinPR events, and timer thread creation.
WinPR CreateThread uses pthread_create. Current devkitARM provides
a pthread creation bridge on 3DS; the missing pthread_key_* operations
are separate and are already addressed by the project's TSD bridge.
Therefore R17 alone does **not** prove the pthread_create syscall failed.
The errno 11 can be left over from nonblocking SOC socket operations.
Each 3DS WinPR event uses a TCP loopback socket pair, so socket pressure
is another candidate. 3dbrew documents a 64-socket total for soc:U.

patches/freerdp-3.32.2-3ds-native-core-timer.patch retains the complete
FreeRDP timer logic, events and scheduling, but creates, joins and frees
the timer worker through the native libctru thread API on __3DS__.
Non-3DS platforms retain their original CreateThread behavior. The
native timer thread invokes the app's pthread TSD destructor cleanup
on exit. This is an experimental compatibility repair, not yet
validated on hardware.

A nested T-stage diagnostic now shows or logs timer construction
failures:

| T | Operation |
|---|---|
| 1 | Allocate timer |
| 2 | Allocate synchronized timer-entry list |
| 3 | Create timer wake event |
| 4 | Create main-loop timer event |
| 5 | Create native ARM11 timer thread |
| 6 | Timer constructed successfully |

If context creation fails again, the screen reports
FreeRDP ctx C# R# T# and an opted-in rdplog.txt records a
freerdp-timer-stage event. Keep the PR draft until real TLS/NLA
and remote desktop sessions are validated.

### Updated diagnostics from the user-submitted R17 patches

The timer now records its actual requested priority and, on T5
thread-creation failure, attempts to read the process thread-resource
limit and test an additional 68 KiB heap allocation. These checks are
**diagnostic clues**, not proof of the exact allocator or svc failure.
`freerdp-timer-thread` contains priority in column a,
`(used << 16) | max` thread-resource counts in b (0xFFFF means unknown),
and the allocation-probe result (1/0) in c.

The loopback adapter now records open/failure totals and the last
failed opening stage. The `loopback-stats` entry records open count,
failure count, and `(last-stage << 16) | (errno & 0xffff)`.
If context creation fails, an optional `loopback-postfail-cap`
entry records how many **additional** pairs (up to 12) can be opened
**after FreeRDP's failed context has already been released**.
This is not the peak socket headroom at the exact failure point.
Capacity probing deliberately does not run on a successful context
because the session is about to establish TLS/NLA.

`freerdp-3.32.2-3ds-event-hardening.patch` avoids a busy loop
when ResetEvent sees loopback EOF and treats a full nonblocking
SetEvent write buffer as an already-pending wake token. Loopback
writers also request TCP_NODELAY on a best-effort basis. No changes
to TLS validation, NLA authentication or certificate pinning.

The opt-in switch remains the **pre-existing writable**
`sdmc:/3ds/3ds-rdp/rdplog.txt`. No automatic file creation or
diagnostic writes if that file is absent at startup. These are
**experimental** changes; CI success does not confirm physical-device
operation, and the branch is not merged to main.

### Physical O3DS hang after CreateEvent failed (October 29, 2026)

The new user log has two later boots that end after `freerdp-new`.
It contains no `freerdp-context-fail`, `freerdp-context-ok`,
`password-dialog-start`, or worker/network-stage entry on those boots.
The visible FreeRDP message "CreateEvent failed!" is emitted in
`freerdp_context_new_ex()` when the channel-error event cannot be
allocated (C8), after `rdp_new()` succeeds. FreeRDP immediately
calls `freerdp_context_free()`, which first calls `rdp_free()`,
which first destroys the FreeRDP timer. The previous O3DS native timer
patch used an **unbounded `threadJoin(..., U64_MAX)`**. If a worker
blocks in a WinPR wait and the SOC event wakeup fails, cleanup can hang
indefinitely. This is a strong explanation for the observed lockup,
but the log alone cannot prove where cleanup stopped.

**Recovery build behavior:** On __3DS__ only, the optional FreeRDP
timer callback API is marked unsupported, as it is on Emscripten.
The base 3DS client has no callers that register these callbacks
(advanced video channels/X11/Wayland are not linked into this app).
The FreeRDP timer object still allocates its synchronized entries and
one mainloop event handle for the core event loop, but it skips its
unused worker-wakeup event and does **not** launch a background worker.
This removes an unbounded join and one loopback socket pair. On every
other platform, the pinned upstream timer continues unchanged.
Optional timer add returns 0 (explicitly unsupported), rather than
discarding callbacks silently. The timer's T marker reaches T6
without a thread.

A separate O3DS-only weak hook checkpoints context construction and
teardown into `rdplog.txt` **only when the file already exists**.
`core-context-step` arguments: stage, numeric errno/detail, and
`(successful_loopback_pairs << 16) | failed_loopback_pairs`.
`core-socket-last` arguments: most recent loopback failure stage,
errno, 0. Checkpoints:

| Stage | Checkpoint |
|---|---|
| 8 | About to allocate channel-error event |
| 80 | Channel-error CreateEvent failed (numeric errno) |
| 81 | Channel-error event created |
| 90 | About to free failed context |
| 92 | Before `rdp_free()` (which destroys the timer first) |
| 93 | After `rdp_free()` |
| 91 | After complete context free |

If a subsequent `rdplog.txt` ends at `92`, the hang is inside
`rdp_free()`; if `90` appears without `92`, it is before it.
If `80` appears, its errno and socket-count metrics can distinguish
event socket exhaustion from other event allocation errors.

Because the prior test made the system unresponsive, this recovery
build also **disables the post-failure socket capacity stress test**.
Only passive counters and checkpoints are recorded. Avoid using the
previous build again; the new behavior remains unverified on physical
O3DS until the user runs the latest green CI artifact.

### Physical O3DS C9 failure, lazy WinPR events (October 29, 2026)

Following the timerless recovery build, two hardware attempts reached
`C9 R0 T0`, returned cleanly to the menu, and logged a failed socket
pair, stage 1, errno 19 (`ENODEV`):

```text
core-context-step 00000008 00000000 00070000
core-context-step 00000051 00000000 00080000
core-context-step 0000005A 00000013 00080001
core-socket-last 00000001 00000013 00000000
core-context-step 0000005C 00000000 00080001
core-context-step 0000005D 00000000 00080001
core-context-step 0000005B 00000000 00080001
freerdp-context-fail 00000009 00000000 0000000B
```

These `core-context-step` numbers are in **hex**: `08` is
before the C8 channel-error event, `51`=81 is C8 success,
`5A`=90 is failure cleanup start, `5C`=92 and `5D`=93
bracket `rdp_free()`, and `5B`=91 confirms completed
failed-context cleanup.

Pinned FreeRDP 3.32.2 `freerdp_channels_new()` creates a
`MessageQueue_New()`, whose constructor calls `CreateEvent()`.
A failure at C9 immediately after successful C8 and a failing
`loopback_pair_open()` strongly implicates another event socket-pair
allocation. The 3DS libctru `socket()` wrapper can return ENODEV
if its `soc:` device is missing; an error can also be stale
depending on the failed internal path. Thus **do not assert
a specific socket limit from this errno alone**.

Recovery experiment `patches/freerdp-3.32.2-3ds-lazy-events.patch`:
under `__3DS__` only, ordinary WinPR events allocate
their TCP loopback pair **only when a WinPR wait first needs their
FD**, instead of at `CreateEvent()`. Pre-materialization
SetEvent/ResetEvent state is maintained under a per-event LightLock;
an initially signaled event will materialize as signaled.
Attached external FD events are untouched. Non-3DS builds
retain upstream eager event behavior. This saves sockets for
unused constructor and channel-message events, but the
waited-on events can still exhaust sockets: hardware testing
must verify progress and failure handling. No TLS/NLA/cert
policy is weakened.

A second diagnostic `freerdp-channels-stage` identifies the
substep within C9: 1 global table init, 2 allocation,
3 global hash table check, 4 critical section, 5
MessageQueue_New (its event), 6 channelEvents hash table,
7 success. The client only writes this to an already
existing `rdplog.txt` upon C9 failure. No automatic logging,
file creation, socket-stress test or background timer worker.

### Hardware progress: C12 succeeds, TCP connect error 0x00020006

The next physical O3DS log shows two full, successful context constructions,
successful OpenSSL readiness, password dialog and FreeRDP settings, and
`connect-worker-end 00000000 00020006 00000000`. In pinned FreeRDP
3.32.2 `0x00020006` is `FREERDP_ERROR_CONNECT_FAILED`, a broad
connection failure that can be set after socket or WinPR wait errors.
This is **not an authentication, certificate-trust, or TLS-version code**.
Two `freerdp-context-ok 0000000C 00000011` records confirm C12.
The worker finished and FreeRDP cleanup returned; the process later logged
`app-exit` but users reported that the console still appeared stuck while
exiting.

New `patches/freerdp-3.32.2-3ds-tcp-stage-diag.patch` adds **opt-in,
numeric only** `tcp-stage` events inside FreeRDP's pinned TCP path.
Fields: stage in hexadecimal, local errno, and WinPR/WSA/wait status if
relevant. No hostnames, addresses, passwords or certificate material.

| Stage | TCP operation |
|---|---|
| 10/11 | Begin / fail creating temporary WinPR connection event |
| 12 | WSAEventSelect failed |
| 13 | Nonblocking connect failed immediately (field c is WSA status) |
| 14 | WaitForMultipleObjects returned (field c is wait result) |
| 15 | Clearing WSAEventSelect mode failed |
| 16 | Resetting socket blocking mode failed |
| 17 | Low-level TCP helper finished successfully |
| 18 | SO_ERROR reported a failed connection |
| 20/21 | Hostname resolution begins/fails |
| 22 | No usable address for requested IP family |
| 23/24 | Try to open a native socket / socket creation failed |
| 25 | TCP helper returned failure |
| 26/27 | Host-level TCP connection succeeded/failed |
| 31 | Querying local/remote socket IP failed |
| 32 | Setting receive buffer failed |
| 33 | Keepalive socket configuration failed |
| 34 | Connection was aborted |
| 35 | Default TCP setup succeeded |
| 40/41 | Transport-layer TCP setup starts/fails |
| 42–45 | Transport object/WinPR event setup failed |
| 46 | Transport layer created successfully |

The original SD log writes `app-exit` **before** `socExit()` and
`gfxExit()`. Additional markers `app-soc-exit-enter`,
`app-soc-exit-done`, `app-soc-buffer-free`,
`app-gfx-exit-enter`, `app-gfx-exit-done` distinguish a SOC or GFX
teardown hang from a completed return to Homebrew Launcher. A log ending
before `app-soc-exit-done` points toward `socExit()`; a log ending
before `app-gfx-exit-done` points toward `gfxExit()`. This build
**does not** bypass SOC cleanup, disable NLA, or alter certificate rules.
Hardware behavior remains unverified.

### Hardware log 4: connected TCP, SO_RCVBUF failure and confirmed SOC exit hang

The newest O3DS log shows three consecutive full FreeRDP attempts reaching
the same numeric TCP trace (another was a password-dialog cancellation):

```text
connect-main-soc   00000004
connect-worker-soc 00000004
tcp-stage 00000028 00000058 00000000 # 40: transport start
tcp-stage 00000014 00000058 00000000 # 20: DNS begin
tcp-stage 00000017 00000058 00000000 # 23: open native socket
tcp-stage 0000000A 00000058 00000000 # 10: create temporary event
tcp-stage 0000000E 00000016 00000000 # 14: wait returned WAIT_OBJECT_0
tcp-stage 00000011 00000016 00000000 # 17: TCP helper succeeded
tcp-stage 0000001A 00000016 00000000 # 26: host TCP succeeded
tcp-stage 00000020 00000058 00000000 # 32: growing SO_RCVBUF failed
tcp-stage 00000029 00000058 00000000 # 41: transport setup aborted
connect-worker-end 00000000 00020006 00000000
```

The SOC devoptab is visible from main and worker (`FindDevice("soc:")=4`).
Although `errno=0x58` at the `SO_RCVBUF` failure might be target-specific,
stage 32 proves the exact failing API call in pinned FreeRDP 3.32.2:
after `getsockopt(SO_RCVBUF)` succeeds and reports less than 32 KiB,
`setsockopt(SO_RCVBUF, 32768)` fails. The kernel TCP socket already
connected; this was **not** a remote host refusal or certificate issue.

**Recovery experiment:** `freerdp-3.32.2-3ds-soc-rcvbuf.patch` retains
a strictly positive, successfully queried original SOC receive buffer if
attempting to enlarge to 32 KiB fails. Numeric `tcp-stage 38` records
the existing buffer value before the enlargement attempt, and 36 records
the fallback and value if needed. Every other platform still fails
closed with the unmodified upstream behavior. This does **not** claim
the smaller buffer has passed actual RDP traffic stress testing. Note
that these trace stage values are logged as 8-digit hexadecimal:
`00000020` means stage 32 in decimal, etc.

**Confirmed exit hang:** two independent process runs reached
`app-exit`, then `app-soc-exit-enter 1`, and never produced
`app-soc-exit-done`, proving `socExit()` never returned on the
tested O3DS after FreeRDP use. The process had already completed
FreeRDP context cleanup, so this is separate from the former
timer-worker deadlock.

**Scoped exit workaround:** after at least one full FreeRDP attempt,
skip the blocking SOCU_Shutdown call, mark
`app-soc-exit-deferred`, do not free the memory buffer that is still
backing the SOC shared memory handle, then run `gfxExit()` and return.
Normal probe-only runs still call `socExit()` and free their buffer.
The operating system's process teardown is expected to close the
remaining SOC handles and reclaim memory; this is experimental and
should be monitored for Homebrew Launcher return / subsequent apps.
This workaround intentionally avoids terminating active threads or
closing the underlying service handle while it may be in use.

These changes are on the experimental branch/draft PR only, and
require on-device confirmation; the old log cannot prove the
workaround succeeds.

### Physical O3DS cert IP name mismatch after valid time/date (rdplog 5)

With a correct system clock, the previous SO_RCVBUF workaround gets
past TCP setup. The O3DS log now records `tcp-stage 0x26`
(original kernel receive buffer 0x2000 = 8192 bytes), `tcp-stage 0x24`
(accepted fallback), `tcp-stage 0x23` (successful default TCP
socket setup), `tcp-stage 0x2e` (transport layer created), and
`connect-worker-end 00000000 00020008` (TLS connect failed).
Physical UI reports `RDP certificate IP name mismatch`;
this came from our own `verify_x509_cert()`, which previously
rejected the IP address before asking the user to trust. No
saved endpoints yet because trust creation had not been reached.
Both log sessions finish at `app-soc-exit-deferred` and
`app-gfx-exit-done`, matching the report of normal process exit.

**Certificate-name exception design on experimental branch:**
For a parseable PEM X.509 certificate within its valid time window,
verify IP via OpenSSL `X509_check_ip_asc` (IP SAN) or a hostname via
`X509_check_host` with `NO_PARTIAL_WILDCARDS`. A negative result
from OpenSSL's checker is a hard error (do not offer override).
A nonmatching but otherwise parseable certificate is NOT silently
accepted. For an unknown `host:port`, show an explicit
`CERTIFICATE NAME MISMATCH!` warning on the 3DS and the
complete leaf DER SHA-256 fingerprint. User must independently
verify the fingerprint with the actual Windows RDP host and
press B/START (reject), A (accept once), or X (accept and
persist a host:port-scoped exact fingerprint on SD).
No default acceptance. The trust file is still read back and
validated, and changed saved fingerprints are blocked. A
previously explicitly trusted exact fingerprint can be accepted
on later connections for the *same* host:port despite mismatch;
the trusted fingerprint acts as identity verification in lieu
of hostname matching. Only the name check can be overridden:
invalid date, parser error, changed pin, unexpected callback
flags, missing certificate and hostname/port mismatch remain
rejected.

`cert-name-check` log entry contains mode (1 IP/2 DNS) and
name-valid (0/1), and `cert-approval` logs manual selection
(0 reject, 1 session-only, 2 save) and mismatch/IP flags.
No cert, fingerprint, hostname, username or secret material is
written to opt-in logs. If using a local Windows RDP host,
using the correct certificate DNS hostname instead of its
IP, or issuing a certificate with an appropriate IP SAN, avoids
the mismatch entirely and is preferable to an exception.

**Caveat:** first-use trust without independently verifying
the fingerprint against the Windows machine could permit a
man-in-the-middle. This experiment must be tested on O3DS;
it does not confirm NLA, RDP graphics, input or audio.

### First successful live RDP desktop: higher resolutions and red/yellow tint

Physical O3DS test now achieves a fully connected Windows RDP session
with certificate approval and normal homebrew exit. The user reports
that the decoded desktop is red/yellow tinted with no whites, while
the locally drawn (fake) cursor renders white normally. This
is important evidence: the top-screen RGB565 path is *capable*
of showing white; the bug likely occurs before or while converting
FreeRDP's GDI primary framebuffer, not from a permanently broken LCD
format. However, no pixel sample from hardware has yet proven where.

Existing `rdp_bgrx_to_rgb565` assumes GDI was initialized as
`PIXEL_FORMAT_XRGB32`, hence little-endian B,G,R,X bytes. That
conversion formula is mathematically correct and covered by portable
tests, including white, red, green and blue. Blindly swapping RGB
channels cannot explain missing whites. For the next physical test,
opt-in `rdplog.txt` captures **aggregate** statistics from a 16x12
spatial sample of the GDI source: `gdi-format` (pixel-format enum,
width, height), `gdi-avg-bgr`, `gdi-max-bgr`,
`gdi-high-bgr` (per-channel count >= 192), and
`gdi-white-count` (count of sampled near-white pixels, sample count
and probe index). It logs at most four painted frames per process,
not screenshot content, individual pixels, screen text or
credentials. If blue is absent in the GDI samples, investigation
belongs in FreeRDP color decoding, codecs, or received depth;
if GDI samples include all channels and whites but the LCD does not,
the RGB565 output path must be revisited.

Selectable **remote** desktop sizes now include legacy 400x240 and
640x360 plus 1280x720 and 1920x1080. Existing `3DSRDP2` saved
profile format is unchanged: the existing numeric display_mode
field may now hold 0 through 3; previous 0/1 saves still parse.
The top LCD remains physically 400x240 and software scales the
full remote desktop to it by default. L+Circle Pad increases
cursor-centered local viewport magnification up to 500%; R
toggles approximate native-detail zoom, and Y resets to whole
desktop. Up to 1920x1080 requires at least 8,294,400 bytes for
the main 32-bit GDI primary surface **plus** caches, codecs
and other application memory. On O3DS, 1080p is an experiment
that may fail allocation or run too slowly; 720p should be the
first hardware trial. Do not claim 1080p achieved until measured.

The `rdp_desktop_size_allowed` guard still rejects server-initiated
resizes not matching one of the four exact, user-requested profiles.
Unexpected sizes cannot cause arbitrary GDI allocation. Portable
tests now cover all four modes, full-HD zoom and profile persistence.

### Physical O3DS log 6: color inversion, pre-TCP high-resolution OOM and keyboard text

The user physically reports: successful 400x240 and 640x360 RDP
sessions show **blue as red**, **red as blue**, **yellow as cyan**,
and generally yellow/red-tinted output. The cursor/UI overlays are
independently rendered. The GDI sample at 400x240 (frame 32) had
byte0 average 0, byte1 ~51, byte2 ~126, and similarly at 640x360
byte0=0, byte1~53, byte2~102. Despite the earlier BGRX assumption,
the observed red/blue mapping is consistent with treating actual
source byte0 as red and byte2 as blue in the top-screen blitter.
The native 3DS `GSP_RGB565_OES` output convention is unchanged.
The new blitter defaults to the empirically corrected channel order;
**L+X** during a session toggles corrected vs original conversion
without modifying FreeRDP decoding or the local cursor. The
`display-colors` opt-in log gives the chosen numeric mode; this
still requires physical LCD verification.

For high resolution, `rdplog(6).txt` includes repeated
`rdp-requested-size` of 1280x720 (0x500 / 0x2d0) and 1920x1080
(0x780 / 0x438) with `connect-worker-end 0 0` and **no**
`tcp-stage` checkpoint, even with 16-bit negotiated color depth.
Both smaller modes proceed to TCP/NLA, often successfully. The
pinned FreeRDP 3.32.2 `rdp_client_connect()` runs
`rdp_client_reset_codecs()` **before** TCP. Its planar
`freerdp_bitmap_planar_context_reset` allocates 4+6+4+4 =
**18 bytes per remote pixel** (about 16.6 MiB for 720p and
37.3 MiB for 1080p), BEFORE the GDI primary framebuffer.
It returns `FALSE` without setting a FreeRDP last-error code
when these large allocations fail. This is a concrete, plausible
cause of the exact observed zero-error pre-TCP failures.

To address this, `freerdp-3.32.2-3ds-highres-codecs.patch`
changes O3DS's `freerdp_settings_get_codecs_flags` for requested
width >= 1024 or height >= 600: disables PLANAR and PROGRESSIVE
decoder contexts, retaining other codecs (including ordinary bitmap
interleaved, NSCodec and RFX where available). **This is a
compatibility/resource experiment**; server graphics decoding may
require further work even if TCP/NLA now succeeds. A separate
`rdp-codec-stage` numeric checkpoint reports:
1 enter, 2 codec object allocation failed, 3 codec prepare begins,
4 prepare failed, 5 prepare finished, with effective codec flags
and packed remote dimensions. `rdp-codec-mask` logs flags
before invoking FreeRDP. Nothing relaxes TLS, NLA, or trust policy.

Software keyboard `Text` submitted ASCII Unicode events which the
user reports are not received remotely. New routine maps printable
ASCII to US-QWERTY Set-1 scan codes, including balanced Shift for
capitals/punctuation, and sends through the same FreeRDP keyboard
event path as virtual keys. `keyboard-applet-result` and
`keyboard-ascii-result` log only button result, sent/skipped/error
counts, **not the typed text**. This is **not** generic Unicode/IME
or arbitrary keyboard-layout support and requires testing on Windows.

For heavy downscaling, the renderer can now average a 2x2 grid of
source pixels per target LCD pixel when sampling >=2x in both
directions, which can reduce jagged edges/aliasing at 720p or 1080p.
At near-native zoom it reverts to nearest-source-pixel display so
text stays sharp. **L+Y** toggles the experimental smoothing and
`display-smoothing` logs the mode. It is 4 source reads per LCD
pixel; hardware frame-rate effect is unmeasured and this is *not*
a full-resolution area filter, GPU shader or guaranteed image-quality
improvement.

Use the experimental branch build (not main) and test in this order:
(1) 400x240 color, toggling L+X to compare; (2) virtual keyboard
Text with an empty Notepad window focused, including "Hello123!";
(3) 720p/16-bit and its `rdp-codec-stage`, then 1080p if stable;
(4) 720p smoothing on/off with L+Y while in Fit view.

### Experimental native RGB565 GDI for 720p and 1080p

To remove a second large memory cost on O3DS after codec creation,
`post_connect()` now selects FreeRDP's supported
`PIXEL_FORMAT_RGB16` for requested >=1024x600, while the known-working
400x240 and 640x360 profiles remain on the original
`PIXEL_FORMAT_XRGB32`. Native 16-bit GDI is independent of the
remote `FreeRDP_ColorDepth` negotiation and reduces the primary
desktop surface from ~3.69 MB to ~1.84 MB at 720p, or ~8.29 MB
to ~4.15 MB at 1080p. It is NOT a promise that the full decoder,
cache, network, OpenSSL and application fit in the available O3DS heap.

The top blitter checks the actual GDI format and stride, reads a
little-endian 16-bit RGB565 pixel directly when appropriate, and
supports the same reversible red/blue comparison and four-tap
anti-aliasing in either GDI format. It precomputes horizontal bounds
per column and vertical bounds per scanline to avoid redundant
clamps on each of 96,000 output pixels. Native 16-bit mode is logged
by `gdi-init-format` and `gdi-init-ok` (stride,height), or
`gdi-init-fail` with errno; 32-bit `gdi-avg-bgr` statistics are
intentionally not emitted for packed 16-bit frames. The design
is covered by endian, red/blue, averaged color and white portable
tests. Actual client performance/appearance require an O3DS test.

### Two-stage color path test after PR #15

To separate actual raw FreeRDP desktop corruption from the O3DS RGB565 output stage, the L+SELECT test is extended from a single native row to two 12-pixel-tall rows: direct RGB565 swatches first, then synthetic pixels generated using FreeRDP’s exact active GDI destination format followed by the ordinary 3DS blitter. The expected two rows are identical in red/green/blue/white/yellow/cyan/magenta order, regardless of whether XRGB32 or BGRX32 is selected. L+X intentionally changes the second row to verify that the channel switch truly affects the blitter. To distinguish a genuinely flat decoded surface from a conversion error, `gdi-spatial-change` now logs a **count only**, the number of sparse color sample positions differing from sample zero. No individual sampled pixel values, host text, clipboard or credentials are logged.

The user has not yet physically tested these improvements, so no claim that the underlying color issue is fixed is supported.

### October 2026 physical color-strip result: exact FreeRDP layout bug

The hardware user reported the two-row L+SELECT strip as direct RGB565
`red green blue white yellow cyan magenta`, versus FreeRDP-derived
`green red black yellow yellow red green`. The latter is exactly the
output obtained when interpreting FreeRDP's **XRGB32 [X,R,G,B]** as
[B,G,R,X]. In pinned FreeRDP 3.32.2 `libfreerdp/codec/color.h`,
`FreeRDPWriteColor_int` writes 32-bit colors MSB-first regardless
of ARM's little-endian native integer representation. `FreeRDPGetColor`
in `libfreerdp/codec/color.c` packs XRGB as `0x00RRGGBB`, so bytes
[X,R,G,B]. BGRX is `0xBBGGRR00`, bytes [B,G,R,X]. Previous
conversion code used [B,G,R,X] for XRGB32 and [R,G,B,X] for BGRX32.
The actual desktop blitter and 4-tap box average are now corrected,
and telemetry `gdi-avg-rgb` takes normalized RGB samples in both
formats; synthetic FreeRDP canary is `00D05020` for XRGB32 and
`2050D000` for BGRX32 (R=D0,G=50,B=20).

The earlier samples' channel labels were incorrect, so do not rely
on any prior `gdi-avg-bgr` values as true blue/red measurements.
This bug may also explain a uniform-looking blue Windows desktop
at larger resolutions because the old code entirely ignored byte 3
of XRGB32 (which carries blue). It does **not** yet prove that the
high-resolution GDI decoder works in every mode. Test physical
640×360 then 800×480, 1280×720 and 1920×1080; leave L+X off.

The previous RAM credential cache was accidentally wiped whenever
returning from full_session() to the launcher, even if the user had
explicitly selected Advanced → Cache Password → This Run: On.
The program now retains the opt-in credential across later manual
connections within the same process, matched to full host/port/user/domain,
and wipes immediately when the option is disabled, a connection fails,
or the process exits. The password never persists to SD.

### High-resolution fit-mode single-color root cause

The user observed that 800×480 and 1080p show a single color only at fit/100% and the actual remote image appears when zooming in (about 150% on 720p). Both FreeRDP XRGB32 and BGRX32 now render correctly when zoomed in, so decoder color loss is no longer the likely cause. In `copy_desktop_frame`, `step_x = view.width_q16 / 400` and `step_y = view.height_q16 / 240` are 16.16 Q16 source-pixel steps. Prior four-tap smoothing computed `off_x = step_x >> 2` then subtracted `off_x` from a plain integer `sx`. At 1920×1080 in fit mode, `step_x = 314572` (4.8px per LCD sample), creating `off_x = 78643` **integer source pixels** instead of ~1 pixel. Both X and Y sample corners clamped to the same desktop edges, so the entire downscaled frame was a nearly uniform blend of four corners. The corrected helper `rdp_box4_radius_px` divides the Q16 quarter-footprint by 65536, rounds and uses an integer source-pixel offset. Unit tests cover all six desktop resolutions and zoom=150%; press L+Y to compare smoothed and unfiltered output. Actual device behavior needs retesting.

RAM credential caching is now automatic during the process lifetime (same host/port/user/domain only), with failure/exit cleanup and no SD persistence. The previous Advanced cache toggle has been removed.
