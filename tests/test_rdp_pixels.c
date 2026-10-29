#include "rdp_pixels.h"
#include <assert.h>
#include <stdio.h>

static void check(uint8_t b, uint8_t g, uint8_t r, uint16_t expected)
{
    const uint8_t bgrx[4] = { b, g, r, 0xA5 };
    assert(rdp_bgrx_to_rgb565(bgrx) == expected);
}

int main(void)
{
    check(0x00,0x00,0x00,0x0000); /* Black */
    check(0xFF,0xFF,0xFF,0xFFFF); /* White */
    check(0x00,0x00,0xFF,0xF800); /* Red */
    check(0x00,0xFF,0x00,0x07E0); /* Green */
    check(0xFF,0x00,0x00,0x001F); /* Blue */
    check(0xFF,0xFF,0x00,0x07FF); /* Cyan */
    check(0x00,0x00,0xF8,0xF800); /* Red precision */
    check(0xF8,0xFC,0xF8,0xFFFF); /* Channel mask precision */
    /* A known blue Windows bitmap reached 3DS GDI with red in byte 0.
     * User observed blue->red/yellow->cyan with the old path. Verify both
     * reversible screen modes before trusting on-device display. */
    {
        const uint8_t blue[4]={0,0,255,0}, red[4]={255,0,0,0};
        const uint8_t yellow[4]={255,255,0,0};
        assert(rdp_desktop_to_rgb565(blue,1)==0x001F);
        assert(rdp_desktop_to_rgb565(red,1)==0xF800);
        assert(rdp_desktop_to_rgb565(yellow,1)==0xFFE0);
        assert(rdp_desktop_to_rgb565(blue,0)==0xF800);
        assert(rdp_desktop_to_rgb565(red,0)==0x001F);
    }
    {
        const uint8_t red[4]={255,0,0,0},blue[4]={0,0,255,0};
        const uint8_t green[4]={0,255,0,0},white[4]={255,255,255,0};
        const uint16_t avg=rdp_desktop_box4_to_rgb565(red,blue,green,white,1);
        assert(avg==0x8410 || avg==0x8411); /* average near mid-gray */
        assert(rdp_desktop_box4_to_rgb565(white,white,white,white,1)==0xFFFF);
        assert(rdp_desktop_box4_to_rgb565(red,red,red,red,1)==0xF800);
    }
    puts("RDP RGB565 conversion, channel reversal and box downscale passed");
    return 0;
}
