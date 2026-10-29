#include "session_keys.h"
#include <string.h>
static int keyscan(int key, uint8_t *code, uint8_t *ext)
{
    *ext=0;
    switch(key) {
        case SK_ESC:*code=0x01;break; case SK_TAB:*code=0x0F;break;
        case SK_ENTER:*code=0x1C;break;case SK_BACKSPACE:*code=0x0E;break;
        case SK_DELETE:*code=0x53;*ext=1;break;
        case SK_LEFT:*code=0x4B;*ext=1;break;
        case SK_UP:*code=0x48;*ext=1;break;
        case SK_DOWN:*code=0x50;*ext=1;break;
        case SK_RIGHT:*code=0x4D;*ext=1;break;
        case SK_HOME:*code=0x47;*ext=1;break;
        case SK_END:*code=0x4F;*ext=1;break;
        case SK_PGUP:*code=0x49;*ext=1;break;
        case SK_PGDN:*code=0x51;*ext=1;break;
        case SK_F1:case SK_F2:case SK_F3:case SK_F4:case SK_F5:
        case SK_F6:case SK_F7:case SK_F8:case SK_F9:case SK_F10:
            *code=(uint8_t)(0x3B+key-SK_F1);break;
        case SK_F11:*code=0x57;break;case SK_F12:*code=0x58;break;
        default:return 0;
    }
    return 1;
}
static int emit(SessionKeyEvent *out,size_t cap,size_t *n,int code,int ext,int down)
{
    if (*n>=cap) return 0;
    out[*n].scan=(uint8_t)code;out[*n].extended=(uint8_t)ext;
    out[*n].down=(uint8_t)down;++*n;return 1;
}
size_t session_key_sequence(int key,unsigned modifiers,SessionKeyEvent *out,size_t cap)
{
    size_t n=0;int scan,ext; unsigned i;
    const int codes[]={0x1D,0x38,0x2A,0x5B};
    const int flags[]={0,0,0,1};
    if(!out || cap<10) return 0;
    if(key==SK_CTRL_ALT_END) { modifiers=SK_MOD_CTRL|SK_MOD_ALT;key=SK_END; }
    if(key==SK_ALT_TAB) { modifiers=SK_MOD_ALT;key=SK_TAB; }
    if(key==SK_WIN_R) { modifiers=SK_MOD_WIN;scan=0x13;ext=0; }
    else if(!keyscan(key,(uint8_t *)&(uint8_t){0},(uint8_t *)&(uint8_t){0})) return 0;
    for(i=0;i<4;++i)
        if(modifiers&(1u<<i)) if(!emit(out,cap,&n,codes[i],flags[i],1))return 0;
    if(key!=SK_WIN_R) {
        uint8_t c,e;
        if(!keyscan(key,&c,&e))return 0;
        scan=c;ext=e;
    }
    if(!emit(out,cap,&n,scan,ext,1) ||
       !emit(out,cap,&n,scan,ext,0))return 0;
    for(i=4;i>0;--i) {
        unsigned j=i-1;
        if(modifiers&(1u<<j))
            if(!emit(out,cap,&n,codes[j],flags[j],0))return 0;
    }
    return n;
}
const char *session_key_label(int k)
{
    static const char* const fs[]={"F1","F2","F3","F4","F5","F6",
                                  "F7","F8","F9","F10","F11","F12"};
    if(k>=SK_F1 && k<=SK_F12)return fs[k-SK_F1];
    switch(k) {
      case SK_ESC:return "ESC";case SK_TAB:return "TAB";
      case SK_ENTER:return "ENTER";case SK_BACKSPACE:return "BKSP";
      case SK_DELETE:return "DEL";case SK_LEFT:return "LEFT";
      case SK_UP:return "UP";case SK_DOWN:return "DOWN";case SK_RIGHT:return "RIGHT";
      case SK_CTRL:return "CTRL";case SK_ALT:return "ALT";
      case SK_SHIFT:return "SHIFT";case SK_WIN:return "WIN";
      case SK_TEXT:return "TEXT";case SK_CLOSE:return "CLOSE";case SK_FN:return "F1-F12";
      case SK_CTRL_ALT_END:return "CTRL ALT END";
      case SK_ALT_TAB:return "ALT TAB";case SK_WIN_R:return "WIN R";
      case SK_HOME:return "HOME";case SK_END:return "END";
      case SK_PGUP:return "PGUP";case SK_PGDN:return "PGDN";
      default:return "?";
    }
}

/* ASCII text from the built-in 3DS software keyboard. The NLA session's
 * ordinary physical keyboard path already uses Set-1 scan codes, whereas
 * the generic FreeRDP Unicode events were not observed reaching Windows.
 * Use conventional US-QWERTY scan codes, including Shift for punctuation.
 * No keyboard layout guessing or password persistence. */
int session_ascii_scan(unsigned char ch,uint8_t *scan,uint8_t *shift)
{
    static const uint8_t alpha[26]={
        0x1E,0x30,0x2E,0x20,0x12,0x21,0x22,0x23,0x17,0x24,
        0x25,0x26,0x32,0x31,0x18,0x19,0x10,0x13,0x1F,0x14,
        0x16,0x2F,0x11,0x2D,0x15,0x2C
    };
    static const uint8_t number[10]={0x0B,0x02,0x03,0x04,0x05,
                                      0x06,0x07,0x08,0x09,0x0A};
    const char *plain="`-=[]\\;',./";
    const char *raised="~_+{}|:\"<>?";
    const char *digit_shift=")!@#$%^&*(";
    if(!scan||!shift)return 0;
    *shift=0;
    if(ch>='a'&&ch<='z'){*scan=alpha[ch-'a'];return 1;}
    if(ch>='A'&&ch<='Z'){*scan=alpha[ch-'A'];*shift=1;return 1;}
    if(ch>='0'&&ch<='9'){*scan=number[ch-'0'];return 1;}
    for(unsigned i=0;i<10;++i)
        if(ch==(unsigned char)digit_shift[i]) {
            *scan=number[i];*shift=1;return 1;
        }
    static const uint8_t punctuation[]={
        0x29,0x0C,0x0D,0x1A,0x1B,0x2B,0x27,0x28,0x33,0x34,0x35
    };
    for(unsigned i=0;i<11;++i){
        if(ch==(unsigned char)plain[i]){*scan=punctuation[i];return 1;}
        if(ch==(unsigned char)raised[i]){*scan=punctuation[i];*shift=1;return 1;}
    }
    if(ch==' '){*scan=0x39;return 1;}
    if(ch=='\n'||ch=='\r'){*scan=0x1C;return 1;}
    if(ch=='\t'){*scan=0x0F;return 1;}
    return 0;
}
