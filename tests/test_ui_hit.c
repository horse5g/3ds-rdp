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
    puts("Touch geometry tests passed");
    return 0;
}
