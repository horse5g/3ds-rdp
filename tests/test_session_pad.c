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
    assert(session_pad_hit(&pad,30,215) == PAD_LEFT_BUTTON);
    assert(session_pad_hit(&pad,200,220) == PAD_RIGHT_BUTTON);
    assert(session_pad_hit(&pad,-1,20) == PAD_NONE);

    /* First contact and lift never snap to touch coordinates. */
    a=touch(&pad,20,65);
    assert(!a.pointer_moved);
    a=touch(&pad,26,68);
    assert(a.pointer_moved && pad.pointer_x==212 && pad.pointer_y==126);
    a=lift(&pad);
    assert(!a.left_click);
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
    a=tap(&pad,80,225);
    assert(a.left_click);
    a=tap(&pad,150,225);
    assert(a.right_click);

    a=tap(&pad,290,55);
    assert(a.wheel_notches == 1);
    a=tap(&pad,290,210);
    assert(a.wheel_notches == -1);
    touch(&pad,280,110);
    a=touch(&pad,280,145);
    assert(a.wheel_notches == -2);
    a=touch(&pad,280,152);
    assert(a.wheel_notches == -1);
    lift(&pad);

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
    /* F1-F12 page must have an exit back to Esc/Tab/arrow/modifier keys.
       Prior version had only CLOSE, trapping active sticky modifiers. */
    pad.keyboard_open=true;
    pad.fn_page=false;
    a=tap(&pad, 110, 214); /* Basic row 4 col 2: F1-F12 */
    assert(a.redraw && pad.fn_page);
    assert(session_pad_keyboard_key(&pad,6,45)==SK_F1);
    assert(session_pad_keyboard_key(&pad,110,214)==SK_FN);
    a=tap(&pad,110,214); /* Function row 4 col 2: BASIC */
    assert(a.redraw && !pad.fn_page);
    assert(session_pad_keyboard_key(&pad,6,45)==SK_ESC);
    /* Sticky modifier state must not survive closing the keyboard. */
    pad.settings_open=false; /* start from a visible keyboard, not a prior toolbar state */
    pad.keyboard_open=true;
    pad.fn_page=false;
    a=tap(&pad, 40, 108); /* row 2, col 1: Ctrl */
    assert(pad.key_mods&SK_MOD_CTRL);
    a=tap(&pad, 240, 215); /* last row: CLOSE */
    assert(a.redraw && !pad.keyboard_open && pad.key_mods==0);
    pad.keyboard_open=true;
    a=tap(&pad, 42, 108);
    assert(pad.key_mods&SK_MOD_CTRL);
    a=tap(&pad, 150, 15); /* Settings should replace keyboard overlay */
    assert(a.redraw && !pad.keyboard_open && pad.settings_open);
    assert(!pad.fn_page && pad.key_mods==0);
    a=tap(&pad, 200, 15); /* Keyboard should replace settings */
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
    puts("RDP session touchpad input tests passed");
    return 0;
}
