#include "rdp_pixels.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>

static void check(uint8_t r,uint8_t g,uint8_t b,uint16_t expect)
{
    /* FreeRDP 3.32.2's MSB-first WriteColor layout, NOT native-endian.
     * XRGB32 = [X,R,G,B]; BGRX32 = [B,G,R,X]. */
    const uint8_t xrgb[4]={0x00,r,g,b};
    const uint8_t bgrx[4]={b,g,r,0x00};
    assert(rdp_gdi32_to_rgb565(xrgb,0,0)==expect);
    assert(rdp_gdi32_to_rgb565(bgrx,1,0)==expect);
    assert(rdp_gdi32_to_rgb565(xrgb,0,0)==
           rdp_gdi32_to_rgb565(bgrx,1,0));
    const uint16_t inv=rdp_gdi32_to_rgb565(xrgb,0,1);
    assert(inv==rdp_gdi32_to_rgb565(bgrx,1,1));
    assert(inv==(uint16_t)(((expect&0x001F)<<11)|
                           (expect&0x07E0)|((expect&0xF800)>>11)));
    assert(rdp_gdi32_box4_to_rgb565(xrgb,xrgb,xrgb,xrgb,0,0)==expect);
    assert(rdp_gdi32_box4_to_rgb565(bgrx,bgrx,bgrx,bgrx,1,0)==expect);
}
int main(void)
{
    /* The seven RGB swatches mirror L+SELECT's top direct LCD row
     * and bottom FreeRDP color-writing path. This catches the exact
     * previous top red->green, green->red, blue->black result. */
    check(0xFF,0,0,0xF800);
    check(0,0xFF,0,0x07E0);
    check(0,0,0xFF,0x001F);
    check(0xFF,0xFF,0xFF,0xFFFF);
    check(0xFF,0xFF,0,0xFFE0);
    check(0,0xFF,0xFF,0x07FF);
    check(0xFF,0,0xFF,0xF81F);
    check(0,0,0,0x0000);
    check(0xF8,0xFC,0xF8,0xFFFF);
    /* Raw FreeRDP canary R=D0,G=50,B=20 */
    check(0xD0,0x50,0x20,0xD284);

    const uint8_t xr[4]={0,255,0,0};
    const uint8_t xg[4]={0,0,255,0};
    const uint8_t xb[4]={0,0,0,255};
    const uint8_t xw[4]={0,255,255,255};
    const uint8_t br[4]={0,0,255,0};
    const uint8_t bg[4]={0,255,0,0};
    const uint8_t bb[4]={255,0,0,0};
    const uint8_t bw[4]={255,255,255,0};
    assert(rdp_gdi32_box4_to_rgb565(xr,xg,xb,xw,0,0)==0x8410);
    assert(rdp_gdi32_box4_to_rgb565(br,bg,bb,bw,1,0)==0x8410);
    assert(rdp_gdi32_box4_to_rgb565(xr,xg,xb,xw,0,0)==
           rdp_gdi32_box4_to_rgb565(br,bg,bb,bw,1,0));

    const uint8_t red565[2]={0x00,0xF8};
    const uint8_t blue565[2]={0x1F,0x00};
    const uint8_t green565[2]={0xE0,0x07};
    const uint8_t white565[2]={0xFF,0xFF};
    assert(rdp_desktop_native565(red565,1)==0xF800);
    assert(rdp_desktop_native565(red565,0)==0x001F);
    assert(rdp_desktop_native565(blue565,1)==0x001F);
    assert(rdp_desktop_native565(green565,1)==0x07E0);
    assert(rdp_desktop_native565_box4(red565,red565,red565,red565,1)==0xF800);
    assert(rdp_desktop_native565_box4(red565,blue565,green565,white565,1)==0x8410);
    assert(rdp_desktop_native565_box4(white565,white565,white565,white565,1)==0xFFFF);

    /* Four-sample source offsets are integer pixels, NOT 16.16 units.
     * Prior bug: at 1920x1080 fit, 4.8px/sample yielded radius
     * 78,643 pixels, so the taps all clamped to desktop edges. */
    {
        static const struct {int w,h;} modes[]={
            {400,240},{640,360},{800,480},{960,540},
            {1280,720},{1920,1080}
        };
        for(unsigned i=0;i<sizeof(modes)/sizeof(modes[0]);++i) {
            const int32_t sx=(int32_t)(((int64_t)modes[i].w*65536)/400);
            const int32_t sy=(int32_t)(((int64_t)modes[i].h*65536)/240);
            const int rx=rdp_box4_radius_px(sx);
            const int ry=rdp_box4_radius_px(sy);
            if(modes[i].w<800)assert(rx==0);
            if(modes[i].w>=800) {
                assert(rx>=1 && rx<=2);
                assert(ry>=1 && ry<=2);
                /* A center pixel must sample nearby interior pixels,
                 * not image borders or corners. */
                assert(modes[i].w/2-rx>0);
                assert(modes[i].w/2+rx<modes[i].w-1);
                assert(modes[i].h/2-ry>0);
                assert(modes[i].h/2+ry<modes[i].h-1);
            }
        }
        /* Zooming to 150% reduces fit sampling footprint; a 720p
         * viewport no longer chooses a huge offscreen radius. */
        assert(rdp_box4_radius_px((int32_t)(1280.0f/1.5f*65536.0f/400))<=1);
        assert(rdp_box4_radius_px((int32_t)(1920.0f/1.5f*65536.0f/400))<=1);
        assert(rdp_box4_radius_px(65536)==0);
        assert(rdp_box4_radius_px(2*65536)==1);
        assert(rdp_box4_radius_px(5*65536)==1);
        assert(rdp_box4_radius_px(6*65536)==2);
    }

    puts("FreeRDP colors, Q16 fit-mode smoothing offsets and zoom regressions passed");
    return 0;
}
