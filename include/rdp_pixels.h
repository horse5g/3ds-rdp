#ifndef RDP_PIXELS_H
#define RDP_PIXELS_H
#include <stdint.h>

/* FreeRDP PIXEL_FORMAT_XRGB32 in little-endian memory is B, G, R, X.
 * libctru's GSP_RGB565_OES is a 16-bit framebuffer, not BGR8.
 * Host portable so the channel conversion can be tested independently. */
static inline uint16_t rdp_bgrx_to_rgb565(const uint8_t *src)
{
    return (uint16_t)((((uint16_t)(src[2] & 0xF8u)) << 8) |
                      (((uint16_t)(src[1] & 0xFCu)) << 3) |
                      ((uint16_t)src[0] >> 3));
}
#endif
