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
