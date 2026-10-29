# O3DS framebuffer artifact investigation

Symptoms observed on hardware: white corruption on the bottom-screen connection
menu and black corruption on the upper-screen status console; opening the
software keyboard changes or clears the corruption.

## Root cause addressed

`consoleInit(GFX_TOP, ...)` and `consoleInit(GFX_BOTTOM, ...)` in libctru
force the selected display to **single buffering**, whereas the graphical
launcher and FreeRDP top-screen renderer were designed to draw and present
complete frames via `gfxSwapBuffers()`. A full screen can be observed while
our software renderer is still filling it, producing tearing/artifacts.

Additionally, a libctru `PrintConsole` stores the framebuffer address once
at initialization. Simply enabling double buffering without rebinding it
leaves future console writes pointing to the wrong buffer.

The display fix:

- Calls `gfxSetDoubleBuffering(..., true)` on **both screens after** the
  initial top/bottom console initialization.
- Rebinds the top status console to `gfxGetFramebuffer(GFX_TOP, GFX_LEFT, ...)`
  before every frame, and the trusted-certificates console before each redraw.
- Reuses the already initialized top console for FreeRDP's connection worker,
  instead of calling `consoleInit` mid-session (which also swaps **both**
  displays).
- Rebinds the connection/certificate console on every redraw.
- Draws a fresh bottom framebuffer alongside the top connection/certificate
  prompts when the application swaps both screens.
- Retains the 16-bit RGB565 pixel format on both screens and the existing
  FreeRDP render path. Certificate and authentication policy is unchanged.

## Hardware verification checklist

Install the experimental fixed build on an original 3DS, open it without
connecting, and watch both screens for 20–30 seconds. Open and cancel the
keyboard, and verify neither screen develops corruption afterwards. Switch
tabs and saved profiles, open the trusted-certificates view, and test one RDP
connection/certificate prompt (when you have a trusted test server).

Record whether corruption is stationary, moves with screen redraws, flickers
only while touching, or appears only after the software keyboard. A photograph
of **both screens** is useful if artifacts persist.

**CI is a cross-build check, not a physical O3DS display validation.**
