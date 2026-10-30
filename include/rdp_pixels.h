#ifndef RDP_PIXELS_H
#define RDP_PIXELS_H
#include <stdint.h>

/* FreeRDP 3.32.2 writes 32-bit pixel colors in MOST-SIGNIFICANT-BYTE-FIRST
 * order: FreeRDPWriteColor_int() assigns dst[0]=color>>24, dst[1]=>>
 * 16, dst[2]=>>8, dst[3]=color. Its GetColor() also depends on the
 * declared pixel format, NOT the ARM's native little-endian uint32_t.
 *
 * PIXEL_FORMAT_XRGB32 bytes = X,R,G,B (not B,G,R,X).
 * PIXEL_FORMAT_BGRX32 bytes = B,G,R,X (not R,G,B,X).
 * Both formats encode the same full RGB color; the 3DS LCD expects
 * conventional RGB565 (red top five bits, blue bottom five bits).
 * flip_rb is strictly an optional post-decode diagnostic (L+X).
 */
static inline uint16_t rdp_gdi32_to_rgb565(const uint8_t *src,
                                            int bgrx, int flip_rb)
{
    const unsigned r_index=bgrx ? 2u : 1u;
    const unsigned g_index=bgrx ? 1u : 2u;
    const unsigned b_index=bgrx ? 0u : 3u;
    const unsigned red=src[flip_rb?b_index:r_index];
    const unsigned green=src[g_index];
    const unsigned blue=src[flip_rb?r_index:b_index];
    return (uint16_t)(((red & 0xF8u)<<8) |
                      ((green & 0xFCu)<<3) | (blue>>3));
}

/* Four-tap box sampling with source-channel offsets applied BEFORE averaging.
 * No per-pixel heap allocations and sums are bounded to 4*255. */
static inline uint16_t rdp_gdi32_box4_to_rgb565(const uint8_t *a,
    const uint8_t *b, const uint8_t *c, const uint8_t *d,
    int bgrx, int flip_rb)
{
    const unsigned r_index=bgrx ? 2u : 1u;
    const unsigned g_index=bgrx ? 1u : 2u;
    const unsigned b_index=bgrx ? 0u : 3u;
    const unsigned ri=flip_rb?b_index:r_index;
    const unsigned bi=flip_rb?r_index:b_index;
    const unsigned red=(a[ri]+b[ri]+c[ri]+d[ri]+2u)>>2;
    const unsigned green=(a[g_index]+b[g_index]+c[g_index]+d[g_index]+2u)>>2;
    const unsigned blue=(a[bi]+b[bi]+c[bi]+d[bi]+2u)>>2;
    return (uint16_t)(((red & 0xF8u)<<8) |
                      ((green & 0xFCu)<<3) | (blue>>3));
}

/* Retained for possible future memory-constrained GDI RGB16 experiments;
 * current tested client uses 32-bit GDI at all resolutions. */
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
#endif
