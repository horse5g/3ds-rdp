#include "session_keys.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    SessionKeyEvent events[10];size_t n;
    n=session_key_sequence(SK_CAPS_LOCK,0,events,10);
    assert(n==2 && events[0].scan==0x3A && !events[0].extended &&
           events[0].down && !events[1].down);
    n=session_key_sequence(SK_ENTER,0,events,10);
    assert(n==2 && events[0].scan==0x1C && events[0].down && !events[1].down);
    n=session_key_sequence(SK_DELETE,SK_MOD_CTRL|SK_MOD_ALT,events,10);
    assert(n==6 && events[0].scan==0x1D && events[1].scan==0x38);
    assert(events[2].scan==0x53 && events[2].extended && events[3].extended);
    assert(!events[5].down && !events[4].down);
    n=session_key_sequence(SK_CTRL_ALT_END,0,events,10);
    assert(n==6 && events[2].scan==0x4F && events[2].extended);
    n=session_key_sequence(SK_ALT_TAB,0,events,10);
    assert(n==4 && events[0].scan==0x38 && events[1].scan==0x0F);
    n=session_key_sequence(SK_WIN_R,0,events,10);
    assert(n==4 && events[0].scan==0x5B && events[0].extended);
    assert(events[1].scan==0x13);
    n=session_key_sequence(SK_F12,SK_MOD_SHIFT,events,10);
    assert(n==4 && events[1].scan==0x58 && events[2].scan==0x58);
    /* QWERTY ASCII keys use the same Set-1 paths as Windows desktop input.
     * The painted label is uppercase but the unshifted key identity is not. */
    n=session_key_sequence(SK_CHAR('c'),0,events,10);
    assert(n==2 && events[0].scan==0x2E && !events[0].extended);
    assert(events[0].down && !events[1].down);
    n=session_key_sequence(SK_CHAR('c'),SK_MOD_CTRL|SK_MOD_SHIFT,events,10);
    assert(n==6 && events[0].scan==0x1D && events[1].scan==0x2A);
    assert(events[2].scan==0x2E && events[3].scan==0x2E);
    assert(events[4].scan==0x2A && !events[4].down);
    assert(events[5].scan==0x1D && !events[5].down);
    n=session_key_sequence(SK_CHAR('!'),0,events,10);
    assert(n==4 && events[0].scan==0x2A && events[1].scan==0x02);
    assert(!events[2].down && !events[3].down);
    n=session_key_sequence(SK_CHAR(' '),0,events,10);
    assert(n==2 && events[0].scan==0x39);
    n=session_key_sequence(SK_CHAR('['),0,events,10);
    assert(n==2 && events[0].scan==0x1A);
    n=session_key_sequence(SK_CHAR(0xE9),0,events,10);
    assert(n==0); /* Unicode is not implemented via US scan codes. */
    assert(session_key_sequence(SK_SYM,0,events,10)==0);
    assert(session_key_sequence(SK_TEXT,0,events,10)==0);
    assert(session_key_sequence(SK_F1,0,events,1)==0);
    { uint8_t scan=0,shift=0;
      assert(session_ascii_scan('a',&scan,&shift) && scan==0x1E && !shift);
      assert(session_ascii_scan('A',&scan,&shift) && scan==0x1E && shift);
      assert(session_ascii_scan('Z',&scan,&shift) && scan==0x2C && shift);
      assert(session_ascii_scan('!',&scan,&shift) && scan==0x02 && shift);
      assert(session_ascii_scan('0',&scan,&shift) && scan==0x0B && !shift);
      assert(session_ascii_scan('?',&scan,&shift) && scan==0x35 && shift);
      assert(session_ascii_scan('@',&scan,&shift) && scan==0x03 && shift);
      assert(session_ascii_scan(' ',&scan,&shift) && scan==0x39 && !shift);
      assert(!session_ascii_scan(0xE9,&scan,&shift));
      assert(!session_ascii_scan('X',NULL,&shift));
    }
    puts("Keyboard key, shortcut and software text scancodes passed");
    return 0;
}
