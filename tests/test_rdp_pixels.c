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
    puts("RDP RGB565 framebuffer conversion passed");
    return 0;
}
