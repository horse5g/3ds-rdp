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
