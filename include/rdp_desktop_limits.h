#ifndef RDP_DESKTOP_LIMITS_H
#define RDP_DESKTOP_LIMITS_H

/* O3DS top-screen display is always 400x240. The remote desktop can be
 * larger and is downsampled locally. Preserve existing profile indices:
 * 0 = 400x240, 1 = 640x360, 2 = 1280x720, 3 = 1920x1080.
 * Full HD is experimental on O3DS and may fail GDI allocation; neither a
 * malicious server nor a corrupt profile can silently raise this budget. */
static inline int rdp_desktop_mode_size(unsigned mode, unsigned *width,
                                        unsigned *height)
{
    static const unsigned widths[] = { 400u, 640u, 1280u, 1920u };
    static const unsigned heights[] = { 240u, 360u, 720u, 1080u };
    if (!width || !height || mode >= 4u)
        return 0;
    *width = widths[mode];
    *height = heights[mode];
    return 1;
}

/* A server-initiated resize is intentionally refused. It would otherwise
 * allocate GDI surfaces and caches outside the user's chosen memory budget. */
static inline int rdp_desktop_size_allowed(unsigned width, unsigned height,
                                            unsigned requested_width,
                                            unsigned requested_height)
{
    unsigned w, h;
    if (!width || !height || width != requested_width ||
        height != requested_height)
        return 0;
    for (unsigned mode = 0; mode < 4u; ++mode) {
        if (rdp_desktop_mode_size(mode, &w, &h) &&
            width == w && height == h)
            return 1;
    }
    return 0;
}
#endif
