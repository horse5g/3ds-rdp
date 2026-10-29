#ifndef RDP_PIXELS_H
#define RDP_PIXELS_H
#include <stdint.h>

/* O3DS physical test: remote red appeared blue, blue appeared red and
 * yellow appeared cyan. The first and third bytes in the GDI primary
 * buffer need reversing relative to the previous screen conversion.
 * The native 3DS LCD remains standard RGB565 (red in bits 15..11).
 * Do not modify the local cursor or native UI's color conversion. */
static inline uint16_t rdp_desktop_to_rgb565(const uint8_t *src, int swap_rb)
{
    const uint8_t r=src[swap_rb ? 0 : 2];
    const uint8_t g=src[1];
    const uint8_t b=src[swap_rb ? 2 : 0];
    return (uint16_t)(((uint16_t)(r & 0xF8u) << 8) |
                      ((uint16_t)(g & 0xFCu) << 3) |
                      ((uint16_t)b >> 3));
}

/* Fast spatial 2x2 averaging of four raw 32-bit source pixels.
 * Use only for substantially downscaled views: 4 reads per destination
 * instead of nearest-neighbour's 1. Each channel sum is <= 1020. */
static inline uint16_t rdp_desktop_box4_to_rgb565(const uint8_t *a,
    const uint8_t *b,const uint8_t *c,const uint8_t *d,int swap_rb)
{
    const unsigned red=swap_rb?0u:2u,blue=swap_rb?2u:0u;
    const unsigned r=(a[red]+b[red]+c[red]+d[red]+2u)>>2;
    const unsigned g=(a[1]+b[1]+c[1]+d[1]+2u)>>2;
    const unsigned bl=(a[blue]+b[blue]+c[blue]+d[blue]+2u)>>2;
    return (uint16_t)(((r & 0xF8u)<<8)|((g & 0xFCu)<<3)|(bl>>3));
}

/* Native FreeRDP PIXEL_FORMAT_RGB16 uses the conventional little-endian
 * 5:6:5 memory layout. This can be copied to GSP_RGB565_OES without an
 * intermediate 32-bit desktop. Keep the same on-device L+X diagnostic:
 * corrected=1 means native RGB; corrected=0 reverses red and blue. */
static inline uint16_t rdp_desktop_native565(const uint8_t *src,int corrected)
{
    const uint16_t v=(uint16_t)(src[0] | ((uint16_t)src[1] << 8));
    if (corrected) return v;
    return (uint16_t)((v & 0x07E0u) |
                      ((v & 0x001Fu) << 11) |
                      ((v & 0xF800u) >> 11));
}
static inline uint16_t rdp_desktop_native565_box4(const uint8_t *a,
    const uint8_t *b,const uint8_t *c,const uint8_t *d,int corrected)
{
    const uint16_t v0=(uint16_t)(a[0] | ((uint16_t)a[1] << 8));
    const uint16_t v1=(uint16_t)(b[0] | ((uint16_t)b[1] << 8));
    const uint16_t v2=(uint16_t)(c[0] | ((uint16_t)c[1] << 8));
    const uint16_t v3=(uint16_t)(d[0] | ((uint16_t)d[1] << 8));
    const unsigned r=(((v0>>11)&31u)+((v1>>11)&31u)+
                      ((v2>>11)&31u)+((v3>>11)&31u)+2u)>>2;
    const unsigned g=(((v0>>5)&63u)+((v1>>5)&63u)+
                      ((v2>>5)&63u)+((v3>>5)&63u)+2u)>>2;
    const unsigned bl=((v0&31u)+(v1&31u)+(v2&31u)+(v3&31u)+2u)>>2;
    const uint16_t avg=(uint16_t)((r<<11)|(g<<5)|bl);
    if (corrected) return avg;
    return (uint16_t)(((avg&31u)<<11)|(avg&0x07E0u)|((avg>>11)&31u));
}

/* Old converter retained for comparison/debug and existing consumers. */
static inline uint16_t rdp_bgrx_to_rgb565(const uint8_t *src)
{
    return rdp_desktop_to_rgb565(src,0);
}
#endif
