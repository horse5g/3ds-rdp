#include "session_pad.h"
#include <assert.h>
#include <stdio.h>

static PadActions touch(SessionPad *pad, int x, int y)
{
    PadActions actions;
    session_pad_update(pad, true, x, y, &actions);
    return actions;
}
static PadActions lift(SessionPad *pad)
{
    PadActions actions;
    session_pad_update(pad, false, -1, -1, &actions);
    return actions;
}
static PadActions tap(SessionPad *pad, int x, int y)
{
    touch(pad,x,y);
    return lift(pad);
}

static SessionKeyboardTile keyboard_tile(SessionPad *pad,int key)
{
    SessionKeyboardTile t;
    for(int i=0;session_pad_keyboard_tile(pad,i,&t);++i)
        if(t.key==key) return t;
    assert(!"requested key absent from keyboard");
    return (SessionKeyboardTile){0};
}
static PadActions tap_key(SessionPad *pad,int key)
{
    const SessionKeyboardTile t=keyboard_tile(pad,key);
    return tap(pad,t.x+t.w/2,t.y+t.h/2);
}

int main(void)
{
    SessionPad pad;
    PadActions a;
    int i;

    session_pad_init(&pad);
    assert(pad.pointer_x == 200 && pad.pointer_y == 120);
    assert(pad.sensitivity == 2 && pad.scroll_amount == 1);
    assert(session_pad_hit(&pad,200,15) == PAD_KEYBOARD);
    assert(session_pad_hit(&pad,280,15) == PAD_HELP);
    assert(session_pad_hit(&pad,300,60) == PAD_SCROLL_UP);
    assert(session_pad_hit(&pad,300,100) == PAD_SCROLL_STRIP);
    assert(session_pad_hit(&pad,300,220) == PAD_SCROLL_DOWN);
    assert(session_pad_hit(&pad,300,34) == PAD_NONE);
    assert(session_pad_hit(&pad,300,79) == PAD_NONE);
    assert(session_pad_hit(&pad,300,192) == PAD_NONE);
    assert(session_pad_hit(&pad,300,239) == PAD_NONE);
    assert(session_pad_hit(&pad,30,215) == PAD_LEFT_BUTTON);
    assert(session_pad_hit(&pad,200,220) == PAD_RIGHT_BUTTON);
    assert(session_pad_hit(&pad,-1,20) == PAD_NONE);

    /* First contact and lift never snap to touch coordinates. */
    a=touch(&pad,20,65);
    assert(!a.pointer_moved && a.redraw);
    a=touch(&pad,26,68);
    assert(a.pointer_moved && pad.pointer_x==212 && pad.pointer_y==126);
    a=lift(&pad);
    assert(!a.left_click && a.redraw);
    a=touch(&pad,150,160);
    assert(!a.pointer_moved && pad.pointer_x==212);
    a=touch(&pad,155,160);
    assert(a.pointer_moved && pad.pointer_x==222);
    lift(&pad);

    /* Small quick tap clicks; long or dragging touches do not. */
    a=tap(&pad,100,100);
    assert(a.left_click && !a.pointer_moved);
    touch(&pad,100,100);
    for(i=0;i<22;++i) touch(&pad,100,100);
    a=lift(&pad);
    assert(!a.left_click);
    a=touch(&pad,80,225);
    assert(a.redraw && pad.touching && pad.captured==PAD_LEFT_BUTTON);
    a=lift(&pad);
    assert(a.left_click && a.redraw);
    a=touch(&pad,150,225);
    assert(a.redraw && pad.captured==PAD_RIGHT_BUTTON);
    a=lift(&pad);
    assert(a.right_click && a.redraw);

    a=tap(&pad,290,55);
    assert(a.wheel_notches == 1);
    a=tap(&pad,290,210);
    assert(a.wheel_notches == -1);
    a=touch(&pad,280,110);
    assert(a.redraw && pad.captured==PAD_SCROLL_STRIP);
    a=touch(&pad,280,145);
    assert(a.wheel_notches == -2);
    a=touch(&pad,280,152);
    assert(a.wheel_notches == -1);
    a=lift(&pad);
    assert(a.redraw);

    /* Settings: cycling speed/scroll step and reversing direction. */
    a=tap(&pad,150,15);
    assert(a.redraw && pad.settings_open);
    assert(session_pad_hit(&pad,80,79) == PAD_SPEED);
    a=tap(&pad,50,78);
    assert(a.redraw && pad.sensitivity == 3);
    a=tap(&pad,50,128);
    assert(pad.scroll_amount == 2);
    a=tap(&pad,50,180);
    assert(pad.reverse_scroll);
    a=tap(&pad,150,15);
    assert(!pad.settings_open);
    a=tap(&pad,290,55);
    assert(a.wheel_notches == -2);
    a=tap(&pad,270,210);
    assert(a.wheel_notches == 2);

    a=tap(&pad,280,15);
    assert(a.redraw && pad.help_open);
    assert(session_pad_hit(&pad,100,100) == PAD_NONE);
    a=tap(&pad,280,15);
    assert(a.redraw && !pad.help_open);
    a=tap(&pad,200,15);
    assert(a.keyboard && a.redraw);
    a=tap(&pad,50,15);
    assert(!a.disconnect && pad.disconnect_armed);
    a=tap(&pad,150,15);
    assert(!pad.disconnect_armed);
    a=tap(&pad,50,15);
    assert(pad.disconnect_armed && !a.disconnect);
    a=tap(&pad,50,15);
    assert(a.disconnect);
    /* Real PC keyboard: every visible tile uses the same non-overlapping
     * bounds as its hit-test region. All keys stay within the 320x240 LCD. */
    pad.keyboard_open=true;
    pad.fn_page=false;
    pad.sym_page=false;
    int count=0;
    SessionKeyboardTile t,other;
    while(session_pad_keyboard_tile(&pad,count,&t)) {
        assert(t.x>=0 && t.y>=32 && t.x+t.w<=320 && t.y+t.h<=240);
        assert(t.w>=19 && t.h>=19);
        assert(session_pad_keyboard_key(&pad,t.x+t.w/2,t.y+t.h/2)==t.key);
        for(int j=0;j<count;++j) {
            assert(session_pad_keyboard_tile(&pad,j,&other));
            assert(t.x>=other.x+other.w || other.x>=t.x+t.w ||
                   t.y>=other.y+other.h || other.y>=t.y+t.h);
        }
        ++count;
    }
    assert(count==67);
    assert(session_pad_keyboard_key(&pad,0,65)==0); /* margin */
    assert(session_pad_keyboard_key(&pad,10,64)==0); /* row gap */
    assert(session_pad_keyboard_key(&pad,50,239)==0);
    assert(session_pad_keyboard_key(&pad,5,38)==SK_CHAR('1'));
    assert(session_pad_keyboard_key(&pad,6,68)==SK_TAB);
    assert(session_pad_keyboard_key(&pad,60,68)==SK_CHAR('q'));
    assert(session_pad_keyboard_key(&pad,300,99)==SK_ENTER);
    assert(session_pad_keyboard_key(&pad,140,170)==SK_CHAR(' '));

    /* A 2px glide from key 1 to key 2 must never send key 1, even
     * though both touch positions share the generic PAD_AREA region. */
    a=touch(&pad,25,50);
    assert(a.redraw && pad.captured==PAD_AREA);
    a=touch(&pad,27,50);
    assert(!pad.moved && a.redraw);
    a=lift(&pad);
    assert(a.key==0 && a.redraw);
    a=tap_key(&pad,SK_CHAR('2'));
    assert(a.key==SK_CHAR('2') && !a.modifiers);
    touch(&pad,40,77);
    touch(&pad,145,77);
    a=lift(&pad);
    assert(a.key==0);

    /* Fn replaces number row with F1-F12 without hiding letters,
     * modifiers, navigation, or the Backspace key. */
    a=tap_key(&pad,SK_FN);
    assert(a.redraw && pad.fn_page && !pad.sym_page);
    assert(session_pad_keyboard_key(&pad,5,40)==SK_F1);
    a=tap_key(&pad,SK_F12);
    assert(a.key==SK_F12 && pad.fn_page);
    a=tap_key(&pad,SK_FN);
    assert(a.redraw && !pad.fn_page);
    assert(session_pad_keyboard_key(&pad,5,40)==SK_CHAR('1'));

    /* Symbol page is mutually exclusive with Fn and keeps all other
     * keyboard rows unchanged. */
    a=tap_key(&pad,SK_SYM);
    assert(a.redraw && pad.sym_page);
    assert(session_pad_keyboard_key(&pad,5,40)==SK_CHAR(96));
    a=tap_key(&pad,SK_CHAR('['));
    assert(a.key==SK_CHAR('[') && pad.sym_page);
    a=tap_key(&pad,SK_FN);
    assert(pad.fn_page && !pad.sym_page);
    a=tap_key(&pad,SK_SYM);
    assert(!pad.fn_page && pad.sym_page);
    a=tap_key(&pad,SK_SYM);
    assert(!pad.sym_page);

    /* Latching Ctrl+Shift then pressing C produces exactly one chord
     * and clears both modifiers, leaving the next key unmodified. */
    a=tap_key(&pad,SK_CTRL);
    assert(a.redraw && pad.key_mods==SK_MOD_CTRL);
    a=tap_key(&pad,SK_SHIFT);
    assert(pad.key_mods==(SK_MOD_CTRL|SK_MOD_SHIFT));
    a=tap_key(&pad,SK_CHAR('c'));
    assert(a.key==SK_CHAR('c') && a.modifiers==(SK_MOD_CTRL|SK_MOD_SHIFT));
    assert(pad.key_mods==0);
    a=tap_key(&pad,SK_CHAR('v'));
    assert(a.key==SK_CHAR('v') && a.modifiers==0);
    a=tap_key(&pad,SK_ALT);
    a=tap_key(&pad,SK_TAB);
    assert(a.key==SK_TAB && a.modifiers==SK_MOD_ALT && !pad.key_mods);
    a=tap_key(&pad,SK_CTRL_ALT_END);
    assert(a.key==SK_CTRL_ALT_END && !pad.key_mods);

    /* Tapping the Keyboard toolbar tab returns to the touchpad, and
     * leaving it clears pending sticky modifiers and Fn/Sym layers. */
    a=tap_key(&pad,SK_CTRL);
    a=tap_key(&pad,SK_FN);
    a=tap(&pad,200,15);
    assert(a.redraw && !pad.keyboard_open && pad.key_mods==0);
    assert(!pad.fn_page && !pad.sym_page);
    a=tap(&pad,200,15);
    assert(a.redraw && pad.keyboard_open);
    a=tap_key(&pad,SK_WIN);
    a=tap(&pad,150,15);
    assert(a.redraw && !pad.keyboard_open && pad.settings_open);
    assert(pad.key_mods==0);
    a=tap(&pad,200,15);
    assert(a.redraw && pad.keyboard_open && !pad.settings_open);
    pad.keyboard_open=false; /* return to touchpad after keyboard test */
    /* x clamping and negative speed deltas */
    pad.settings_open=false;
    pad.pointer_x=399; pad.pointer_y=0;
    touch(&pad,10,40);
    a=touch(&pad,200,40);
    assert(!a.pointer_moved && pad.pointer_x==399); /* Cursor remains clamped */
    lift(&pad);
    touch(&pad,200,100);
    a=touch(&pad,10,40);
    assert(a.pointer_moved && pad.pointer_x==0 && pad.pointer_y==0);
    lift(&pad);
    /* At 720p/1080p a given touch swipe should travel the same amount
     * of VISIBLE LCD space as at 400x240, including after cursor zoom.
     * Earlier fixed-pixel movement felt extremely slow on large desktops. */
    session_pad_init(&pad);
    pad.remote_w=1280;pad.remote_h=720;
    pad.pointer_x=640;pad.pointer_y=360;
    session_pad_set_zoom(&pad,1.0f);
    touch(&pad,40,70);
    a=touch(&pad,50,80);
    assert(a.pointer_moved && pad.pointer_x==704 && pad.pointer_y==420);
    lift(&pad);
    assert(session_pad_nudge(&pad,true)==26);
    assert(session_pad_nudge(&pad,false)==24);
    session_pad_set_zoom(&pad,4.0f);
    assert(session_pad_nudge(&pad,true)==6);
    assert(session_pad_nudge(&pad,false)==6);
    touch(&pad,40,70);
    a=touch(&pad,50,80);
    assert(a.pointer_moved && pad.pointer_x==720 && pad.pointer_y==435);
    lift(&pad);

    session_pad_init(&pad);
    pad.remote_w=1920;pad.remote_h=1080;
    pad.pointer_x=960;pad.pointer_y=540;
    session_pad_set_zoom(&pad,1.0f);
    touch(&pad,40,70);
    a=touch(&pad,50,80);
    assert(a.pointer_moved && pad.pointer_x==1056 && pad.pointer_y==630);
    lift(&pad);
    assert(session_pad_nudge(&pad,true)==38);
    assert(session_pad_nudge(&pad,false)==36);
    session_pad_set_zoom(&pad,5.0f);
    touch(&pad,40,70);
    a=touch(&pad,50,80);
    assert(a.pointer_moved && pad.pointer_x==1075 && pad.pointer_y==648);
    lift(&pad);

    /* Fractional relative motion must accumulate in detail view instead
     * of rounding each small swipe down to zero. */
    session_pad_init(&pad);
    pad.sensitivity=1;
    session_pad_set_zoom(&pad,5.0f);
    pad.pointer_x=200;
    touch(&pad,20,65);
    for(i=1;i<=5;++i) {
        a=touch(&pad,20+i,65);
        if (i<3) assert(!a.pointer_moved);
        if (i==3) assert(a.pointer_moved);
    }
    assert(pad.pointer_x==201);
    /* Reverse direction must be symmetric and free of residual drift. */
    for(i=4;i>=0;--i) touch(&pad,20+i,65);
    assert(pad.pointer_x==200);
    lift(&pad);
    session_pad_set_zoom(&pad,0.0f);
    assert(session_pad_nudge(&pad,true)==8);
    session_pad_set_zoom(&pad,100.0f);
    assert(session_pad_nudge(&pad,true)==2);

    puts("RDP session touchpad input, adaptive 720p/1080p movement and zoom tests passed");
    return 0;
}
