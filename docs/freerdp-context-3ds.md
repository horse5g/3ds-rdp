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
freerdp_timer_new(). Its last constructor step in core/timer.c uses
WinPR CreateThread, which internally calls pthread_create. That
platform pthread path is not verified on original 3DS.

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
