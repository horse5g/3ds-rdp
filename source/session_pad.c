#include "session_pad.h"
#include <stdlib.h>
#include <string.h>

#define SCROLL_PX_PER_NOTCH 14

#define TAP_MAX_FRAMES 18
#define TAP_MOVEMENT 6

static int clamp(int value, int min, int max)
{
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

void session_pad_init(SessionPad *pad)
{
    if (!pad) return;
    memset(pad, 0, sizeof(*pad));
    pad->pointer_x = RDP_PAD_REMOTE_W / 2;
    pad->pointer_y = RDP_PAD_REMOTE_H / 2;
    pad->sensitivity = 2;
    pad->scroll_amount = 1;
}

/* Four-column touch panel. The final row supplies shortcuts and a text
 * entry button. The key grid uses the same hitboxes as the drawn controls. */
int session_pad_keyboard_key(const SessionPad *pad,int x,int y)
{
    static const int base[16]={
        SK_ESC,SK_TAB,SK_ENTER,SK_BACKSPACE,
        SK_CTRL,SK_ALT,SK_SHIFT,SK_WIN,
        SK_LEFT,SK_UP,SK_DOWN,SK_RIGHT,
        SK_DELETE,SK_FN,SK_TEXT,SK_CLOSE
    };
    static const int fn[16]={
        SK_F1,SK_F2,SK_F3,SK_F4,
        SK_F5,SK_F6,SK_F7,SK_F8,
        SK_F9,SK_F10,SK_F11,SK_F12,
        SK_CTRL_ALT_END,SK_ALT_TAB,SK_WIN_R,SK_CLOSE
    };
    int col,row;
    if(!pad || !pad->keyboard_open || x<4 || x>=316 || y<40 || y>=232)return 0;
    col=(x-4)/78;row=(y-40)/48;
    if(col>=4||row>=4)return 0;
    return pad->fn_page?fn[row*4+col]:base[row*4+col];
}
PadRegion session_pad_hit(const SessionPad *pad, int x, int y)
{
    if (!pad || x < 0 || x >= RDP_PAD_SCREEN_W ||
        y < 0 || y >= RDP_PAD_SCREEN_H)
        return PAD_NONE;
    if (y < 32) {
        if (x < 108) return PAD_DISCONNECT;
        if (x < 212) return PAD_SETTINGS;
        return PAD_KEYBOARD;
    }
    if (pad->keyboard_open) return session_pad_keyboard_key(pad,x,y) ? PAD_AREA : PAD_NONE;
    if (pad->settings_open) {
        if (x >= 12 && x < 308) {
            if (y >= 56 && y < 104) return PAD_SPEED;
            if (y >= 108 && y < 156) return PAD_SCROLL_AMOUNT;
            if (y >= 160 && y < 208) return PAD_REVERSE;
        }
        return PAD_NONE;
    }
    if (x >= 270) {
        if (y < 78) return PAD_SCROLL_UP;
        if (y < 193) return PAD_SCROLL_STRIP;
        return PAD_SCROLL_DOWN;
    }
    if (y >= 214) {
        if (x < 132) return PAD_LEFT_BUTTON;
        if (x >= 137 && x < 266) return PAD_RIGHT_BUTTON;
        return PAD_NONE;
    }
    if (x >= 4 && x < 266 && y >= 36 && y < 211)
        return PAD_AREA;
    return PAD_NONE;
}

static bool same_release_region(const SessionPad *pad, int x, int y)
{
    return pad->captured == session_pad_hit(pad, x, y);
}

void session_pad_update(SessionPad *pad, bool held, int x, int y, PadActions *out)
{
    int dx, dy, steps;
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!pad) return;

    if (held && (x < 0 || x >= RDP_PAD_SCREEN_W ||
                 y < 0 || y >= RDP_PAD_SCREEN_H))
        held = false;

    if (held && !pad->touching) {
        pad->touching = true;
        pad->captured = session_pad_hit(pad, x, y);
        pad->touch_x = pad->start_x = x;
        pad->touch_y = pad->start_y = y;
        pad->hold_frames = 0;
        pad->moved = false;
        pad->scroll_remainder = 0;
        return; /* no cursor teleport on touch down */
    }
    if (held && pad->touching) {
        ++pad->hold_frames;
        dx = x - pad->touch_x;
        dy = y - pad->touch_y;
        pad->touch_x = x;
        pad->touch_y = y;
        if ((x - pad->start_x) * (x - pad->start_x) +
            (y - pad->start_y) * (y - pad->start_y) >=
            TAP_MOVEMENT * TAP_MOVEMENT)
            pad->moved = true;
        if (!pad->keyboard_open && pad->captured == PAD_AREA &&
            session_pad_hit(pad, x, y) == PAD_AREA &&
            (dx || dy)) {
            int nx = clamp(pad->pointer_x + dx * pad->sensitivity,
                           0, RDP_PAD_REMOTE_W - 1);
            int ny = clamp(pad->pointer_y + dy * pad->sensitivity,
                           0, RDP_PAD_REMOTE_H - 1);
            out->pointer_moved = nx != pad->pointer_x || ny != pad->pointer_y;
            pad->pointer_x = nx;
            pad->pointer_y = ny;
        } else if (pad->captured == PAD_SCROLL_STRIP) {
            /* Accumulate little strokes; movement down means scroll down. */
            pad->scroll_remainder += dy;
            steps = pad->scroll_remainder / SCROLL_PX_PER_NOTCH;
            pad->scroll_remainder -= steps * SCROLL_PX_PER_NOTCH;
            if (steps) {
                out->wheel_notches = clamp(
                    steps * (pad->reverse_scroll ? 1 : -1), -8, 8);
            }
        }
        return;
    }
    if (!held && pad->touching) {
        PadRegion pressed = pad->captured;
        bool release_inside = same_release_region(pad, pad->touch_x, pad->touch_y);
        int key = pad->keyboard_open ? session_pad_keyboard_key(pad,pad->start_x,pad->start_y) : 0;
        pad->touching = false;
        pad->captured = PAD_NONE;
        pad->scroll_remainder = 0;
        if (pad->keyboard_open && key && !pad->moved && release_inside) {
            if(key==SK_CLOSE){pad->keyboard_open=false;pad->fn_page=false;out->redraw=true;}
            else if(key==SK_FN){pad->fn_page=true;out->redraw=true;}
            else if(key==SK_TEXT){out->text_input=true;out->redraw=true;}
            else if(key==SK_CTRL||key==SK_ALT||key==SK_SHIFT||key==SK_WIN){
                unsigned bit=1u<<(key-SK_CTRL);
                pad->key_mods^=bit;out->redraw=true;
            } else {out->key=key;out->modifiers=pad->key_mods;out->redraw=true;}
        } else if (pressed == PAD_AREA && !pad->keyboard_open && !pad->moved &&
            pad->hold_frames <= TAP_MAX_FRAMES && release_inside) {
            out->left_click = true;
        } else if (release_inside && !pad->moved) {
            switch (pressed) {
                case PAD_SCROLL_UP:
                    out->wheel_notches = pad->scroll_amount *
                        (pad->reverse_scroll ? -1 : 1);
                    break;
                case PAD_SCROLL_DOWN:
                    out->wheel_notches = pad->scroll_amount *
                        (pad->reverse_scroll ? 1 : -1);
                    break;
                case PAD_LEFT_BUTTON: out->left_click = true; break;
                case PAD_RIGHT_BUTTON: out->right_click = true; break;
                case PAD_KEYBOARD:
                    pad->keyboard_open = !pad->keyboard_open;
                    if(!pad->keyboard_open)pad->fn_page=false;
                    out->keyboard = true;
                    pad->disconnect_armed = false;
                    out->redraw = true;
                    break;
                case PAD_SETTINGS:
                    pad->settings_open = !pad->settings_open;
                    pad->disconnect_armed = false;
                    out->redraw = true;
                    break;
                case PAD_DISCONNECT:
                    if (pad->disconnect_armed) out->disconnect = true;
                    else {
                        pad->disconnect_armed = true;
                        out->redraw = true;
                    }
                    break;
                case PAD_SPEED:
                    pad->sensitivity = pad->sensitivity % 3 + 1;
                    out->redraw = true;
                    break;
                case PAD_SCROLL_AMOUNT:
                    pad->scroll_amount = pad->scroll_amount % 3 + 1;
                    out->redraw = true;
                    break;
                case PAD_REVERSE:
                    pad->reverse_scroll = !pad->reverse_scroll;
                    out->redraw = true;
                    break;
                default: break;
            }
        }
        if (pressed != PAD_DISCONNECT && pad->disconnect_armed) {
            pad->disconnect_armed = false;
            out->redraw = true;
        }
    }
}
