#include "rdp_desktop_limits.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    unsigned w=0, h=0;
    static const unsigned sizes[][2] = {
        { 400, 240 }, { 640, 360 }, { 1280, 720 }, { 1920, 1080 },
        { 800, 480 }, { 960, 540 }
    };
    /* All selectable resolutions must map to exactly one bounded size. */
    for (unsigned i=0; i<6; ++i) {
        assert(rdp_desktop_mode_size(i,&w,&h));
        assert(w==sizes[i][0] && h==sizes[i][1]);
        assert(rdp_desktop_size_allowed(w,h,w,h));
    }
    assert(!rdp_desktop_mode_size(6,&w,&h));
    assert(!rdp_desktop_mode_size(255,&w,&h));
    assert(!rdp_desktop_mode_size(1,NULL,&h));
    assert(!rdp_desktop_mode_size(1,&w,NULL));
    assert(!rdp_desktop_size_allowed(800,600,800,600));
    assert(!rdp_desktop_size_allowed(800,480,960,540));
    assert(!rdp_desktop_size_allowed(960,540,800,480));
    assert(!rdp_desktop_size_allowed(2560,1440,2560,1440));
    assert(!rdp_desktop_size_allowed(8192,8192,1920,1080));
    assert(!rdp_desktop_size_allowed(65535,65535,400,240));
    assert(!rdp_desktop_size_allowed(0,240,400,240));
    assert(!rdp_desktop_size_allowed(400,0,400,240));
    assert(!rdp_desktop_size_allowed(400,240,0,240));
    assert(!rdp_desktop_size_allowed(400,240,400,0));
    /* Server-initiated mismatched size must never be allowed. */
    assert(!rdp_desktop_size_allowed(1280,720,1920,1080));
    assert(!rdp_desktop_size_allowed(1920,1080,1280,720));
    assert(!rdp_desktop_size_allowed(640,240,640,360));
    puts("O3DS bounded six-mode desktop guard passed");
    return 0;
}
