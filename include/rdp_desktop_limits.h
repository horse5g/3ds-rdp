#ifndef RDP_DESKTOP_LIMITS_H
#define RDP_DESKTOP_LIMITS_H

/* An original Nintendo 3DS cannot allocate arbitrary server-directed GDI
 * desktops. The negotiated resolution is chosen from validated per-profile
 * settings; reject an unexpected resize before FreeRDP's GDI allocator runs.
 * This is a pure helper, available in portable tests without libctru. */
static inline int rdp_desktop_size_allowed(unsigned width, unsigned height,
                                            unsigned requested_width,
                                            unsigned requested_height)
{
    if (!requested_width || !requested_height ||
        requested_width > 640u || requested_height > 360u)
        return 0;
    return width == requested_width && height == requested_height;
}
#endif
