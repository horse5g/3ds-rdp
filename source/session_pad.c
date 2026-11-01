#include "session_pad.h"
#include <stdlib.h>
#include <stdint.h>
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
    pad->remote_w=RDP_PAD_REMOTE_W;
    pad->remote_h=RDP_PAD_REMOTE_H;
    pad->pointer_x = RDP_PAD_REMOTE_W / 2;
    pad->pointer_y = RDP_PAD_REMOTE_H / 2;
    pad->sensitivity = 2;
    pad->scroll_amount = 1;
    pad->zoom_q8 = 256;
}

/* Keep exactly the old pointer feel for 400x240 at fit. Remote pixels
 * per touch pixel increase with desktop size but decrease with zoom, so
 * the number of VISIBLE screen pixels travelled stays consistent.
 * A signed Q16 remainder ensures slow motion is not rounded away when
 * a highly magnified viewport covers fewer remote pixels than the LCD. */
void session_pad_set_zoom(SessionPad *pad,float visible_zoom)
{
    if (!pad) return;
    if (!(visible_zoom >= 1.0f)) visible_zoom=1.0f;
    if (visible_zoom > 5.0f) visible_zoom=5.0f;
    pad->zoom_q8=(int)(visible_zoom*256.0f+0.5f);
}

static int axis_motion(int delta,int sensitivity,int size,int screen,
                       int zoom_q8,int *fraction)
{
    if (!delta || size<=0 || !fraction) return 0;
    if (zoom_q8<256) zoom_q8=256;
    /* 64-bit product is bounded for native touch deltas and for our
     * user-selected remote size (<=1920x1080). */
    const int64_t q16=(int64_t)delta*sensitivity*size*65536*256 /
                      ((int64_t)screen*zoom_q8);
    const int64_t total=q16 + *fraction;
    /* Round to the nearest native desktop pixel, keeping the signed
     * residual. Truncating instead loses 1/65536 each time when zoom
     * ratios (e.g. 1/5) are not exactly representable in Q16. */
    const int64_t rounded=total+(total>0?32768:(total<0?-32768:0));
    const int motion=(int)(rounded/65536);
    *fraction=(int)(total-(int64_t)motion*65536);
    return motion;
}

int session_pad_nudge(const SessionPad *pad,bool horizontal)
{
    if (!pad)return 8;
    const int size=horizontal?pad->remote_w:pad->remote_h;
    const int screen=horizontal?RDP_PAD_REMOTE_W:RDP_PAD_REMOTE_H;
    const int zoom=pad->zoom_q8>=256?pad->zoom_q8:256;
    const int64_t value=((int64_t)8*size*256+((int64_t)screen*zoom)/2)/
                        ((int64_t)screen*zoom);
    return (int)(value>0?value:1);
}

/* The native 320x240 lower screen is a real PC-style keyboard, not a
 * 4x4 shortcut palette or an OS keyboard applet. Every painted key and its
 * touch region comes from this one data-driven layout. All rows fit x=4..316
 * and y=37..238, below the session toolbar. */
typedef struct { int key; const char *label; int w; } KeySpec;
#define CH(c,l,w) {SK_CHAR(c),l,w}
static const KeySpec number_keys[] = {
    CH('1',"1",22),CH('2',"2",22),CH('3',"3",22),
    CH('4',"4",22),CH('5',"5",22),CH('6',"6",22),
    CH('7',"7",22),CH('8',"8",22),CH('9',"9",22),
    CH('0',"0",22),CH('-',"-",22),CH('=',"=",22),
    {SK_BACKSPACE,"BKSP",48}
};
static const KeySpec function_keys[] = {
    {SK_F1,"F1",22},{SK_F2,"F2",22},{SK_F3,"F3",22},
    {SK_F4,"F4",22},{SK_F5,"F5",22},{SK_F6,"F6",22},
    {SK_F7,"F7",22},{SK_F8,"F8",22},{SK_F9,"F9",22},
    {SK_F10,"F10",22},{SK_F11,"F11",22},{SK_F12,"F12",22},
    {SK_BACKSPACE,"BKSP",48}
};
static const KeySpec symbol_keys[] = {
    CH(96,"GRV",22),CH('~',"~",22),CH('[',"[",22),
    CH(']',"]",22),CH(92,"BS",22),CH('|',"|",22),
    CH('{',"{",22),CH('}',"}",22),CH(';',";",22),
    CH(39,"'",22),CH(':',":",22),CH(34,"DQT",22),
    {SK_BACKSPACE,"BKSP",48}
};
static const KeySpec qwerty_keys[] = {
    {SK_TAB,"TAB",32},
    CH('q',"Q",28),CH('w',"W",28),CH('e',"E",28),
    CH('r',"R",28),CH('t',"T",28),CH('y',"Y",28),
    CH('u',"U",28),CH('i',"I",28),CH('o',"O",28),
    CH('p',"P",28)
};
static const KeySpec home_keys[] = {
    CH('a',"A",25),CH('s',"S",25),CH('d',"D",25),
    CH('f',"F",25),CH('g',"G",25),CH('h',"H",25),
    CH('j',"J",25),CH('k',"K",25),CH('l',"L",25),
    CH(';',";",22),CH(39,"'",22),{SK_ENTER,"ENTER",41}
};
static const KeySpec shift_keys[] = {
    {SK_SHIFT,"SHIFT",45},
    CH('z',"Z",29),CH('x',"X",29),CH('c',"C",29),
    CH('v',"V",29),CH('b',"B",29),CH('n',"N",29),
    CH('m',"M",29),CH(',',",",21),CH('.'," .",21),
    CH('/',"/",21)
};
static const KeySpec modifier_keys[] = {
    {SK_CTRL,"CTRL",44},{SK_WIN,"WIN",39},{SK_ALT,"ALT",39},
    CH(' ',"SPACE",125),{SK_FN,"FN",32},{SK_SYM,"SYM",33}
};
static const KeySpec navigation_keys[] = {
    {SK_ESC,"ESC",28},{SK_TAB,"TAB",28},{SK_DELETE,"DEL",28},
    {SK_HOME,"HOME",28},{SK_END,"END",28},
    {SK_PGUP,"PGUP",28},{SK_PGDN,"PGDN",28},
    {SK_LEFT,"<",28},{SK_UP,"^",28},
    {SK_DOWN,"V",28},{SK_RIGHT,">",28}
};
static const KeySpec shortcut_keys[] = {
    {SK_CTRL_ALT_END,"CTRL+ALT+END",104},
    {SK_ALT_TAB,"ALT+TAB",104},{SK_WIN_R,"WIN+R",104}
};
#undef CH

bool session_pad_keyboard_tile(const SessionPad *pad, int index,
                               SessionKeyboardTile *tile)
{
    static const int row_y[]={37,66,95,124,153,186,219};
    static const int row_h[]={26,26,26,26,30,29,19};
    static const int row_x[]={4,4,6,4,4,6,4};
    static const int row_count[]={13,11,12,11,6,11,3};
    const KeySpec *row_keys;
    int row,first=0,col,x;
    if (!pad || !tile || index<0) return false;
    for(row=0;row<7;++row) {
        if(index<first+row_count[row]) break;
        first+=row_count[row];
    }
    if(row>=7) return false;
    switch(row) {
    case 0: row_keys=pad->fn_page?function_keys:
                     pad->sym_page?symbol_keys:number_keys;break;
    case 1: row_keys=qwerty_keys;break;
    case 2: row_keys=home_keys;break;
    case 3: row_keys=shift_keys;break;
    case 4: row_keys=modifier_keys;break;
    case 5: row_keys=navigation_keys;break;
    default:row_keys=shortcut_keys;break;
    }
    col=index-first;
    x=row_x[row];
    for(int i=0;i<col;++i) x+=row_keys[i].w;
    tile->x=x;
    tile->y=row_y[row];
    tile->w=row_keys[col].w;
    tile->h=row_h[row];
    tile->key=row_keys[col].key;
    tile->label=row_keys[col].label;
    return true;
}

int session_pad_keyboard_key(const SessionPad *pad,int x,int y)
{
    SessionKeyboardTile tile;
    if(!pad || !pad->keyboard_open || x<0 || x>=320 || y<32 || y>=240)
        return 0;
    for(int i=0;session_pad_keyboard_tile(pad,i,&tile);++i)
        if(x>=tile.x && x<tile.x+tile.w &&
           y>=tile.y && y<tile.y+tile.h) return tile.key;
    return 0;
}
PadRegion session_pad_hit(const SessionPad *pad, int x, int y)
{
    if (!pad || x < 0 || x >= RDP_PAD_SCREEN_W ||
        y < 0 || y >= RDP_PAD_SCREEN_H)
        return PAD_NONE;
    if (y < 32) {
        if (x < 80) return PAD_DISCONNECT;
        if (x < 160) return PAD_SETTINGS;
        if (x < 240) return PAD_KEYBOARD;
        return PAD_HELP;
    }
    if (pad->help_open) return PAD_NONE;
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
        /* Match the three drawn control rectangles, not the blank 1-3px
         * gaps or the bottom edge of the touchscreen. */
        if (y >= 36 && y < 78) return PAD_SCROLL_UP;
        if (y >= 80 && y < 191) return PAD_SCROLL_STRIP;
        if (y >= 193 && y < 238) return PAD_SCROLL_DOWN;
        return PAD_NONE;
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
        out->redraw = pad->captured != PAD_NONE;
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
            const int step_x=axis_motion(dx,pad->sensitivity,
                         pad->remote_w,RDP_PAD_REMOTE_W,
                         pad->zoom_q8,&pad->pointer_fraction_x);
            const int step_y=axis_motion(dy,pad->sensitivity,
                         pad->remote_h,RDP_PAD_REMOTE_H,
                         pad->zoom_q8,&pad->pointer_fraction_y);
            const int nx=clamp(pad->pointer_x+step_x,0,pad->remote_w-1);
            const int ny=clamp(pad->pointer_y+step_y,0,pad->remote_h-1);
            /* Discard fraction at a hard edge: do not carry a hidden
             * overscroll into the next swipe away from the boundary. */
            if ((nx==0 && step_x<0) || (nx==pad->remote_w-1 && step_x>0))
                pad->pointer_fraction_x=0;
            if ((ny==0 && step_y<0) || (ny==pad->remote_h-1 && step_y>0))
                pad->pointer_fraction_y=0;
            out->pointer_moved=nx!=pad->pointer_x || ny!=pad->pointer_y;
            pad->pointer_x=nx;
            pad->pointer_y=ny;
        } else if (pad->keyboard_open && pad->captured == PAD_AREA &&
                   (dx || dy)) {
            out->redraw = true; /* clear highlight on any key-crossing glide */
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
        /* Clear pressed-key/button borders on release, even when no click
         * or wheel event is generated (for example after a drag). */
        if (pressed != PAD_NONE) out->redraw = true;
        if (pad->keyboard_open && key && !pad->moved && release_inside &&
            session_pad_keyboard_key(pad,pad->touch_x,pad->touch_y)==key) {
            /* PAD_AREA spans all virtual keys. A tiny stylus slip from
             * one cell to another must not send the first cell's key. */
            if(key==SK_FN){
                pad->fn_page=!pad->fn_page;
                if(pad->fn_page) pad->sym_page=false;
                out->redraw=true;
            } else if(key==SK_SYM){
                pad->sym_page=!pad->sym_page;
                if(pad->sym_page) pad->fn_page=false;
                out->redraw=true;
            } else if(key==SK_CTRL||key==SK_ALT||key==SK_SHIFT||key==SK_WIN){
                unsigned bit=1u<<(key-SK_CTRL);
                pad->key_mods^=bit;
                out->redraw=true;
            } else {
                out->key=key;
                out->modifiers=pad->key_mods;
                /* One-shot modifiers: tapping Ctrl then C produces Ctrl+C
                 * without leaving Ctrl active for subsequent normal keys. */
                pad->key_mods=0;
                out->redraw=true;
            }
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
                    pad->help_open = false;
                    if(pad->keyboard_open)pad->settings_open=false;
                    else {pad->fn_page=false;pad->sym_page=false;pad->key_mods=0;}
                    out->keyboard = true;
                    pad->disconnect_armed = false;
                    out->redraw = true;
                    break;
                case PAD_SETTINGS:
                    pad->settings_open = !pad->settings_open;
                    pad->help_open = false;
                    pad->keyboard_open=false;
                    pad->fn_page=false;
                    pad->sym_page=false;
                    pad->key_mods=0;
                    pad->disconnect_armed = false;
                    out->redraw = true;
                    break;
                case PAD_HELP:
                    pad->help_open = !pad->help_open;
                    pad->settings_open=false;
                    pad->keyboard_open=false;
                    pad->fn_page=false;
                    pad->sym_page=false;
                    pad->key_mods=0;
                    pad->disconnect_armed=false;
                    out->redraw=true;
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
