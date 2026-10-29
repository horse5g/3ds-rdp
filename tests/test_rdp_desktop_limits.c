#include "rdp_desktop_limits.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    /* Only the requested, bounded profile resolution may be allocated. */
    assert(rdp_desktop_size_allowed(400,240,400,240));
    assert(rdp_desktop_size_allowed(640,360,640,360));
    assert(!rdp_desktop_size_allowed(800,600,640,360));
    assert(!rdp_desktop_size_allowed(8192,8192,640,360));
    assert(!rdp_desktop_size_allowed(65535,65535,400,240));
    assert(!rdp_desktop_size_allowed(0,240,400,240));
    assert(!rdp_desktop_size_allowed(400,0,400,240));
    assert(!rdp_desktop_size_allowed(400,240,0,240));
    assert(!rdp_desktop_size_allowed(400,240,400,0));
    assert(!rdp_desktop_size_allowed(640,360,1024,768));
    assert(!rdp_desktop_size_allowed(640,240,640,360));
    puts("O3DS remote desktop allocation guard passed");
    return 0;
}
