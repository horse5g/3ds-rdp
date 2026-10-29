#ifndef RDP_SESSION_PAD_H
#define RDP_SESSION_PAD_H

#include <stdbool.h>
#include "session_keys.h"

/* 320x240 bottom screen becomes a laptop-style touchpad after RDP connects.
 * This state machine intentionally has no libctru / FreeRDP dependencies. */
#define RDP_PAD_SCREEN_W 320
#define RDP_PAD_SCREEN_H 240
#define RDP_PAD_REMOTE_W 400
#define RDP_PAD_REMOTE_H 240

typedef enum {
    PAD_NONE = 0,
    PAD_AREA,
    PAD_SCROLL_UP,
    PAD_SCROLL_STRIP,
    PAD_SCROLL_DOWN,
    PAD_LEFT_BUTTON,
    PAD_RIGHT_BUTTON,
    PAD_DISCONNECT,
    PAD_SETTINGS,
    PAD_KEYBOARD,
    PAD_HELP,
    PAD_SPEED,
    PAD_SCROLL_AMOUNT,
    PAD_REVERSE
} PadRegion;

typedef struct {
    int pointer_x, pointer_y;
    int remote_w, remote_h;
    int sensitivity;          /* 1, 2, 3 desktop pixels per touch pixel */
    int scroll_amount;        /* 1, 2, 3 wheel notches per button press */
    bool reverse_scroll;
    bool settings_open;
    bool keyboard_open;
    bool help_open;
    bool fn_page;
    unsigned key_mods;
    bool disconnect_armed;
    bool touching;
    bool moved;
    PadRegion captured;
    int touch_x, touch_y, start_x, start_y;
    int hold_frames, scroll_remainder;
    int zoom_q8;             /* 256=fit, max 1280=5x zoom */
    int pointer_fraction_x;   /* signed Q16 accumulated subpixel motion */
    int pointer_fraction_y;
} SessionPad;

typedef struct {
    bool pointer_moved;
    bool left_click, right_click;
    int wheel_notches;        /* + is up, - is down */
    bool keyboard, disconnect;
    int key;                      /* SK_* physical scancode action */
    unsigned modifiers;
    bool text_input;
    bool redraw;
} PadActions;

void session_pad_init(SessionPad *pad);
/* Keep touch and D-pad pointer travel consistent across 400p..1080p
 * and fit/native zoom, without changing the baseline 400x240 behavior. */
void session_pad_set_zoom(SessionPad *pad, float visible_zoom);
int session_pad_nudge(const SessionPad *pad, bool horizontal);
PadRegion session_pad_hit(const SessionPad *pad, int x, int y);
void session_pad_update(SessionPad *pad, bool held, int x, int y, PadActions *out);

/* Implementation is in connection_ui_draw.c; draws GSP_RGB565_OES bottom
 * screen through the same small font / framebuffer renderer as the menu. */
void session_pad_draw(const SessionPad *pad, int zoom_percent);
int session_pad_keyboard_key(const SessionPad *pad,int x,int y);

#endif
