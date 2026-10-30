#include "connection_ui.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    int i;
    for (i=0; i<RDP_UI_TAB_COUNT; ++i) {
        assert(connection_ui_tab_at(i*64+32,40) == i);
    }
    assert(connection_ui_tab_at(0,20) == -1);
    assert(connection_ui_tab_at(320,40) == -1);
    for (i=0; i<3; ++i) {
        assert(connection_ui_field_at(110,75+i*30+10) == i);
        assert(connection_ui_saved_at(100,58+i*25+10,5,12) == i+5);
    }
    assert(connection_ui_field_at(110,74) == -1);
    assert(connection_ui_field_at(110,165) == -1);
    assert(connection_ui_field_at(300,110) == -1);
    assert(connection_ui_tab_at(20,23)==0);
    assert(connection_ui_tab_at(20,46)==-1);
    assert(connection_ui_field_at(110,185) == -1);
    assert(connection_ui_saved_at(100,58+2*25,10,12) == -1);
    assert(connection_ui_saved_at(100,58+1*25,10,12) == 11);
    assert(connection_ui_saved_at(100,209,0,12) == -1);
    assert(connection_ui_saved_at(300,58,0,12) == -1); /* right scrollbar is not a row */
    /* Scrollbar locations, short lists, exact bottom, and clamping. */
    assert(connection_ui_scroll_clamp(-12,12,5)==0);
    assert(connection_ui_scroll_clamp(100,12,5)==7);
    assert(connection_ui_scroll_clamp(5,3,5)==0);
    assert(connection_ui_scroll_thumb_h(125,0,5)==125);
    assert(connection_ui_scroll_thumb_h(125,3,5)==125);
    assert(connection_ui_scroll_thumb_h(125,10,5)==62);
    assert(connection_ui_scroll_thumb_y(58,125,10,5,0)==58);
    assert(connection_ui_scroll_thumb_y(58,125,10,5,5)==121);
    assert(connection_ui_scroll_at(58,58,125,10,5)==0);
    assert(connection_ui_scroll_at(182,58,125,10,5)==5);
    assert(connection_ui_scroll_at(999,58,125,10,5)==5);
    assert(connection_ui_scroll_at(-1,58,125,10,5)==0);
    assert(connection_ui_scroll_at(189,80,110,6,5)==1);
    assert(connection_ui_scroll_at(80,80,110,6,5)==0);
    assert(connection_ui_scroll_drag(1,-50,25,12,5)==3);
    assert(connection_ui_scroll_drag(3,50,25,12,5)==1);
    assert(connection_ui_scroll_drag(3,-500,25,12,5)==7);
    assert(connection_ui_scroll_drag(3,500,25,12,5)==0);
    assert(connection_ui_scroll_drag(0,-50,25,2,5)==0);
    puts("Touch geometry and scrollbar mapping tests passed");
    return 0;
}
