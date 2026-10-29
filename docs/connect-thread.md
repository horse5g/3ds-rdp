# Experimental RDP connection thread (O3DS)

The graphical UI, fingerprint confirmation, HID scan, software keyboard,
and framebuffer writes must run on the **foreground** thread. Before this
change, `freerdp_connect()` blocked that thread throughout TCP/TLS/NLA, and
the trust prompt itself ran inside FreeRDP's certificate callback.

The opt-in full client now:
1. Validates the saved connection and certificate stores, then prompts for a
   password on the main thread (unchanged).
2. Creates a dedicated ARM11 connection worker on the default application CPU.
   Only the worker runs blocking `freerdp_connect()`, WinPR, OpenSSL, and
   FreeRDP certificate verification.
3. When a valid unknown certificate is encountered, the worker posts its
   host/port and full SHA-256 to a `LightLock`-protected challenge. It waits
   on a `LightEvent` for the main thread's explicit B/START, A, or X
   decision. **No graphics or HID calls occur in the worker.**
4. B/START during the connection-progress screen sets the cancellation
   request and calls FreeRDP's `freerdp_abort_connect_context`. The program
   waits for the worker to exit before releasing the FreeRDP context, so
   cancellation cannot cause a use-after-free.
5. After a successful `threadJoin`, the existing foreground event loop
   owns FreeRDP, input and the GDI framebuffer as before.

## Important limitations

- `freerdp_abort_connect_context` is best-effort. If a WinPR/SOC blocking
  socket call does not observe the abort, there is no safe force-kill: the
  client may remain on the "Cancelling..." screen. The 12-second TCP connect
  timeout does **not** impose an absolute bound on TLS/NLA.
- FreeRDP's connection callback, internal buffers and GDI initialization
  are cross-thread in this build; this is compiled by CI, **not** proven
  safe with real RDP servers on original 3DS hardware.
- The **in-session** software keyboard now starts a short-lived network-only
  worker. During `swkbdInputText`, it waits on WinPR events, processes FreeRDP
  updates and detects disconnects, but never touches HID, gfx or remote input.
  The foreground joins this worker before sending the entered Unicode text
  or rendering a framebuffer. This is not guaranteed to keep networking
  active if Horizon suspends all application threads during the applet.
  Both FreeRDP thread handoffs and OS applet scheduling need hardware tests.
- Don't assume this makes keyboard applets or asynchronous network processing
  functional during HOME menu suspension. Applet and thread scheduling
  require physical hardware tests.
- Certificate trust remains fail-closed: accepting an unknown certificate
  still requires verifying its fingerprint against a trusted Windows host.
