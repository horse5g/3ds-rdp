#ifndef RDP_DISPLAY_BUFFERS_H
#define RDP_DISPLAY_BUFFERS_H

#include <3ds.h>

/* libctru consoleInit() sets its screen to *single buffering* and retains
 * one framebuffer pointer. Our software UI, desktop renderer and top-screen
 * console share gfxSwapBuffers(), so each screen must instead draw into the
 * backbuffer and the console must be rebound before every new frame.
 *
 * Call this AFTER any consoleInit(), never before it.
 */
static inline void rdp_display_enable_double_buffering(void)
{
    gfxSetDoubleBuffering(GFX_TOP, true);
    gfxSetDoubleBuffering(GFX_BOTTOM, true);
}

/* consoleInit()'s cached frameBuffer becomes stale after the next swap. */
static inline void rdp_display_bind_console(PrintConsole *console, gfxScreen_t screen)
{
    if (console)
        console->frameBuffer =
            (u16 *)gfxGetFramebuffer(screen, GFX_LEFT, NULL, NULL);
}

#endif
