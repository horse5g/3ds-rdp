#include "session_cursor.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    SessionCursor cursor;
    uint16_t frame[16*16];
    const uint8_t pixel[4]={0,0,255,255}; /* opaque red BGRA */
    session_cursor_reset(&cursor);
    assert(!cursor.visible);
    assert(!session_cursor_set(&cursor,pixel,129,1,0,0));
    assert(session_cursor_set(&cursor,pixel,1,1,0,0));
    memset(frame,0,sizeof(frame));
    session_cursor_overlay(frame,16,16,&cursor,8,9,0,0,16<<16,16<<16);
    assert(frame[8*16+(15-9)]==0xF800);
    assert(frame[7*16+(15-9)]==0);
    cursor.visible=false;
    memset(frame,0,sizeof(frame));
    session_cursor_overlay(frame,16,16,&cursor,8,9,0,0,16<<16,16<<16);
    assert(frame[8*16+(15-9)]==0);
    puts("Server-supplied cursor hotspot, compositing and hide tests passed");
    return 0;
}
