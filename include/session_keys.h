#ifndef RDP_SESSION_KEYS_H
#define RDP_SESSION_KEYS_H
#include <stddef.h>
#include <stdint.h>
enum {
    SK_ESC=1, SK_TAB, SK_ENTER, SK_BACKSPACE, SK_DELETE,
    SK_LEFT, SK_UP, SK_DOWN, SK_RIGHT,
    SK_HOME, SK_END, SK_PGUP, SK_PGDN,
    SK_CTRL, SK_ALT, SK_SHIFT, SK_WIN, SK_TEXT, SK_CLOSE,
    SK_FN, SK_CTRL_ALT_END, SK_ALT_TAB, SK_WIN_R,
    SK_F1=32, SK_F2, SK_F3, SK_F4, SK_F5, SK_F6,
    SK_F7, SK_F8, SK_F9, SK_F10, SK_F11, SK_F12
};
enum { SK_MOD_CTRL=1, SK_MOD_ALT=2, SK_MOD_SHIFT=4, SK_MOD_WIN=8 };
typedef struct { uint8_t scan; uint8_t extended; uint8_t down; } SessionKeyEvent;
/* A short, balanced sequence. Modifiers are always released in reverse order.
 * Client maps each event to FreeRDP keyboard_event(flags,scan). */
size_t session_key_sequence(int key, unsigned modifiers,
                            SessionKeyEvent *out, size_t capacity);
const char *session_key_label(int key);
/* US-QWERTY layout: map ASCII to Set-1 scan code + optional Shift.
 * Non-ASCII Unicode/IME requires a negotiated Unicode-input path. */
int session_ascii_scan(unsigned char ch, uint8_t *scan, uint8_t *shift);
#endif
