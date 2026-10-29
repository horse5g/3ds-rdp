/* Native, allocation-free O3DS software UI. No Citro2D dependency.
 * Framebuffer: GSP_RGB565_OES, 240-pixel tall column-major, flipped. */
#include "connection_ui.h"
#include "session_pad.h"
#include <3ds.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define UI_W 320
#define UI_H 240
#define C(r,g,b) (((uint32_t)(r)<<16)|((uint32_t)(g)<<8)|(uint32_t)(b))
#define NAVY C(32,65,108)
#define BLUE C(0,104,189)
#define WHITE C(255,255,255)
#define BG C(241,242,244)
#define BORDER C(176,182,190)
#define INK C(35,42,50)
#define MUTED C(114,122,130)
#define HIGHLIGHT C(217,235,252)
#define PALE C(250,251,252)

static uint16_t *fb;

static uint16_t rgb565(uint32_t rgb)
{
    return (uint16_t)((((rgb>>19)&31)<<11) | (((rgb>>10)&63)<<5) | ((rgb>>3)&31));
}

/* A small embedded uppercase 5x7 font: five vertical columns, bit 0 top.
 * Passwords are never displayed in this UI. Non-ASCII characters show '?'. */
typedef struct { char c; uint8_t column[5]; } Glyph;
static const Glyph font[] = {
    { 'A',{0x7e,0x11,0x11,0x11,0x7e} },{ 'B',{0x7f,0x49,0x49,0x49,0x36} },
    { 'C',{0x3e,0x41,0x41,0x41,0x22} },{ 'D',{0x7f,0x41,0x41,0x22,0x1c} },
    { 'E',{0x7f,0x49,0x49,0x49,0x41} },{ 'F',{0x7f,0x09,0x09,0x09,0x01} },
    { 'G',{0x3e,0x41,0x49,0x49,0x7a} },{ 'H',{0x7f,0x08,0x08,0x08,0x7f} },
    { 'I',{0x00,0x41,0x7f,0x41,0x00} },{ 'J',{0x20,0x40,0x41,0x3f,0x01} },
    { 'K',{0x7f,0x08,0x14,0x22,0x41} },{ 'L',{0x7f,0x40,0x40,0x40,0x40} },
    { 'M',{0x7f,0x02,0x0c,0x02,0x7f} },{ 'N',{0x7f,0x04,0x08,0x10,0x7f} },
    { 'O',{0x3e,0x41,0x41,0x41,0x3e} },{ 'P',{0x7f,0x09,0x09,0x09,0x06} },
    { 'Q',{0x3e,0x41,0x51,0x21,0x5e} },{ 'R',{0x7f,0x09,0x19,0x29,0x46} },
    { 'S',{0x46,0x49,0x49,0x49,0x31} },{ 'T',{0x01,0x01,0x7f,0x01,0x01} },
    { 'U',{0x3f,0x40,0x40,0x40,0x3f} },{ 'V',{0x1f,0x20,0x40,0x20,0x1f} },
    { 'W',{0x7f,0x20,0x18,0x20,0x7f} },{ 'X',{0x63,0x14,0x08,0x14,0x63} },
    { 'Y',{0x07,0x08,0x70,0x08,0x07} },{ 'Z',{0x61,0x51,0x49,0x45,0x43} },
    { '0',{0x3e,0x45,0x49,0x51,0x3e} },{ '1',{0x00,0x42,0x7f,0x40,0x00} },
    { '2',{0x42,0x61,0x51,0x49,0x46} },{ '3',{0x21,0x41,0x45,0x4b,0x31} },
    { '4',{0x18,0x14,0x12,0x7f,0x10} },{ '5',{0x27,0x45,0x45,0x45,0x39} },
    { '6',{0x3c,0x4a,0x49,0x49,0x30} },{ '7',{0x01,0x71,0x09,0x05,0x03} },
    { '8',{0x36,0x49,0x49,0x49,0x36} },{ '9',{0x06,0x49,0x49,0x29,0x1e} },
    { '.',{0x00,0x60,0x60,0x00,0x00} },{ '-',{0x08,0x08,0x08,0x08,0x08} },
    { '_',{0x40,0x40,0x40,0x40,0x40} },{ ':',{0x00,0x36,0x36,0x00,0x00} },
    { '/',{0x20,0x10,0x08,0x04,0x02} },{ '\\',{0x02,0x04,0x08,0x10,0x20} },
    { '@',{0x3e,0x41,0x5d,0x55,0x1e} },{ '(',{0x00,0x1c,0x22,0x41,0x00} },
    { ')',{0x00,0x41,0x22,0x1c,0x00} },{ '+',{0x08,0x08,0x3e,0x08,0x08} },
    { '?',{0x02,0x01,0x51,0x09,0x06} },{ '!',{0x00,0x00,0x5f,0x00,0x00} },
    { '#',{0x14,0x7f,0x14,0x7f,0x14} },{ '*',{0x14,0x08,0x3e,0x08,0x14} },
    { '>',{0x00,0x41,0x22,0x14,0x08} },{ '<',{0x08,0x14,0x22,0x41,0x00} },
    { '^',{0x04,0x02,0x01,0x02,0x04} },
    { '=',{0x14,0x14,0x14,0x14,0x14} },{ ',',{0x00,0x80,0x60,0x00,0x00} },
    { '%',{0x63,0x13,0x08,0x64,0x63} },{ '|',{0x00,0x00,0x7f,0x00,0x00} }
};
static const uint8_t *glyph(char c)
{
    size_t i;
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    for (i = 0; i < sizeof(font)/sizeof(font[0]); ++i)
        if (font[i].c == c) return font[i].column;
    return NULL;
}
static void pixel(int x, int y, uint32_t rgb)
{
    if (!fb || x < 0 || x >= UI_W || y < 0 || y >= UI_H) return;
    fb[(size_t)x * UI_H + (UI_H - 1 - y)] = rgb565(rgb);
}
static void fill(int x, int y, int w, int h, uint32_t color)
{
    int i, j;
    uint16_t c = rgb565(color);
    if (x < 0) {w += x; x = 0;}
    if (y < 0) {h += y; y = 0;}
    if (x + w > UI_W) w = UI_W - x;
    if (y + h > UI_H) h = UI_H - y;
    if (w <= 0 || h <= 0 || !fb) return;
    for (i=x; i<x+w; ++i) {
        uint16_t *p = fb + (size_t)i*UI_H + (UI_H-1-y);
        for (j=0; j<h; ++j) p[-j] = c;
    }
}
static void outline(int x, int y, int w, int h, uint32_t color)
{
    fill(x,y,w,1,color); fill(x,y+h-1,w,1,color);
    fill(x,y,1,h,color); fill(x+w-1,y,1,h,color);
}
static void letter(int x, int y, char c, uint32_t color)
{
    int a,b;
    const uint8_t *bitmap = glyph(c);
    if (!bitmap) return;
    for (a=0; a<5; ++a)
        for (b=0; b<7; ++b)
            if (bitmap[a] & (1u<<b)) pixel(x+a,y+b,color);
}
static void label(int x, int y, const char *s, int limit, uint32_t color)
{
    int i = 0;
    if (!s || limit < 6) return;
    while (*s && x + 6 <= limit) {
        if (i++ > 100) break;
        letter(x,y,*s++,color);
        x += 6;
    }
}
static void button(int x, int y, int w, const char *title, int accent)
{
    fill(x,y,w,26,accent ? BLUE : WHITE);
    outline(x,y,w,26,accent ? NAVY : BORDER);
    label(x+8,y+10,title,x+w-5,accent ? WHITE : INK);
}
/* Compact Windows RDC-inspired chrome: the top screen owns the large
 * branding, leaving the bottom screen as a practical form/dialog. */
static void topbar(void)
{
    fill(0,0,320,22,C(229,234,240));
    fill(0,21,320,1,BORDER);
    fill(8,6,13,10,BLUE);
    fill(10,8,9,6,C(208,234,254));
    fill(13,16,4,2,NAVY);
    label(27,8,"REMOTE DESKTOP",225,INK);
    fill(263,2,55,18,WHITE);
    outline(263,2,55,18,BORDER);
    label(270,8,"OPEN",315,BLUE);
}
static void tabs(const ConnectionUI *ui)
{
    static const char *titles[] = {"GENERAL","DISPLAY","LOCAL","EXPERIENCE","ADVANCED"};
    for (int i=0;i<RDP_UI_TAB_COUNT;++i) {
        int x=i*64, active=ui->tab==i;
        fill(x,24,64,22,active?WHITE:C(230,232,235));
        outline(x,24,64,22,active?BORDER:C(209,213,219));
        if(active)fill(x+1,43,62,2,BLUE);
        label(x+3,31,titles[i],x+63,active?BLUE:MUTED);
    }
}
static void field(int row, const char *name, const char *value, bool focus, bool combo)
{
    int y=75+row*30;
    label(13,y,name,107,focus?BLUE:INK);
    fill(104,y-2,186,24,WHITE);
    outline(104,y-2,186,24,focus?BLUE:BORDER);
    if(value && value[0])label(109,y+7,value,combo?265:283,INK);
    else label(109,y+7,"(NOT SET)",combo?265:283,MUTED);
    if(combo) {
        fill(267,y-1,22,22,C(237,240,245));
        fill(267,y-1,1,22,BORDER);
        label(273,y+7,"V",288,INK);
    }
}
static void setting_row(int row,const char *name,const char *value,int interactive)
{
    const int y=73+row*30;
    fill(11,y,282,28,interactive?WHITE:C(244,246,249));
    outline(11,y,282,28,interactive?BORDER:C(226,230,235));
    label(17,y+10,name,166,interactive?INK:MUTED);
    if(interactive) {
        fill(166,y+3,120,22,WHITE);
        outline(166,y+3,120,22,BLUE);
        label(172,y+10,value,271,BLUE);
        label(275,y+10,"V",284,BLUE);
    } else {
        label(174,y+10,value,283,MUTED);
    }
}
static void dropdown_overlay(const ConnectionUI *ui)
{
    static const char *sizes[]={"400 X 240","640 X 360","1280 X 720","1920 X 1080","800 X 480","960 X 540"};
    static const char *depth[]={"16 BIT","32 BIT"};
    static const char *speed[]={"1 X","2 X","3 X"};
    static const char *scroll[]={"1 NOTCH","2 NOTCH","3 NOTCH"};
    static const char *bools[]={"OFF","ON"};
    static const char *timeout[]={"8 SECONDS","12 SECONDS","20 SECONDS"};
    const char *const *items=NULL;
    int count=0;
    switch(ui->choice_menu) {
    case 1: items=sizes;count=6;break;
    case 2: items=depth;count=2;break;
    case 3: items=speed;count=3;break;
    case 4: items=scroll;count=3;break;
    case 5: case 6: items=bools;count=2;break;
    case 7: items=timeout;count=3;break;
    default:return;
    }
    fill(27,49,270,146,C(217,222,229));
    fill(30,52,262,139,WHITE);
    outline(30,52,262,139,BLUE);
    label(41,63,ui->choice_scroll?"UP: MORE  B: CLOSE":"SELECT VALUE  B: CLOSE",284,NAVY);
    const int first=ui->choice_scroll;
    const int visible=count-first<5?count-first:5;
    for(int i=0;i<visible;i++) {
        const int n=first+i,y=80+22*i;
        const bool current=n==ui->choice_highlight;
        fill(36,y,251,21,current?HIGHLIGHT:(i&1)?PALE:WHITE);
        outline(36,y,251,21,current?BLUE:C(217,222,229));
        label(46,y+7,items[n],275,current?BLUE:INK);
    }
    if (count>5) {
        fill(284,80,3,108,C(220,226,234));
        const int slider=80+(ui->choice_scroll*108)/(count-4);
        fill(283,slider,5,18,BLUE);
    }
}
static void main_panel(const ConnectionUI *ui)
{
    const RdpUiOptions *o=&ui->options;
    char value[50];
    fill(6,49,308,134,WHITE);
    outline(6,49,308,134,BORDER);
    if(ui->tab==RDP_UI_GENERAL) {
        if(ui->panel_scroll==0) {
            label(13,56,"LOGON SETTINGS",282,NAVY);
            field(0,"COMPUTER",ui->current.host,ui->focus==0,true);
            field(1,"USER NAME",ui->current.username,ui->focus==1,false);
            field(2,"DOMAIN",ui->current.domain,ui->focus==2,false);
        } else {
            label(13,56,"CONNECTION SETTINGS",282,NAVY);
            snprintf(value,sizeof(value),"%u",(unsigned)ui->current.port);
            field(0,"PORT",value,ui->focus==3,false);
            field(1,"SAVE AS",ui->name,ui->focus==4,false);
            label(15,148,"PASSWORDS ARE REQUESTED ON CONNECT",291,MUTED);
        }
        fill(297,56,13,120,C(237,240,244));
        outline(297,56,13,120,BORDER);
        label(301,60,"^",311,ui->panel_scroll==0?BLUE:MUTED);
        fill(300,83+ui->panel_scroll*57,7,47,BLUE);
        label(301,163,"V",311,ui->panel_scroll?MUTED:BLUE);
        label(13,170,ui->panel_scroll?"PAGE 2 / 2":"PAGE 1 / 2",280,MUTED);
    } else {
        const char *group=ui->tab==RDP_UI_DISPLAY?"REMOTE DISPLAY":
                          ui->tab==RDP_UI_LOCAL?"LOCAL RESOURCES":
                          ui->tab==RDP_UI_EXPERIENCE?"CONNECTION EXPERIENCE":
                          "SECURITY AND CONNECTION";
        label(13,56,group,301,NAVY);
        switch(ui->tab) {
        case RDP_UI_DISPLAY: {
            static const char *sizes[]={"400 X 240","640 X 360","1280 X 720","1920 X 1080","800 X 480","960 X 540"};
            setting_row(0,"DESKTOP SIZE",sizes[o->display_mode<6?o->display_mode:0],1);
            snprintf(value,sizeof(value),"%u BIT",(unsigned)o->color_depth);
            setting_row(1,"COLOR DEPTH",value,1);
            setting_row(2,"LOCAL ZOOM","100 - 500%",0);
            break;
        }
        case RDP_UI_LOCAL:
            snprintf(value,sizeof(value),"%u X",(unsigned)o->touch_speed);
            setting_row(0,"POINTER SPEED",value,1);
            snprintf(value,sizeof(value),"%u NOTCH",(unsigned)o->scroll_step);
            setting_row(1,"SCROLL STEP",value,1);
            setting_row(2,"REVERSE SCROLL",o->reverse_scroll?"ON":"OFF",1);
            break;
        case RDP_UI_EXPERIENCE:
            setting_row(0,"LOW BANDWIDTH",o->low_bandwidth?"ON":"OFF",1);
            setting_row(1,"TRANSPORT","DIRECT TCP",0);
            setting_row(2,"CLIPBOARD","NOT ENABLED",0);
            break;
        default:
            snprintf(value,sizeof(value),"%u SEC",(unsigned)o->timeout_seconds);
            setting_row(0,"CONNECT TIMEOUT",value,1);
            setting_row(1,"SECURITY","NLA REQUIRED",0);
            setting_row(2,"CERTIFICATES","L + X TO VIEW",0);
            break;
        }
        label(13,172,"BLUE BOX = SETTING; GRAY = STATUS",294,MUTED);
    }
    if(ui->choice_menu)dropdown_overlay(ui);
}
static void saved_panel(const ConnectionUI *ui)
{
    int i;
    char line[30];
    for (i=0; i<5; ++i) {
        int index = ui->scroll+i, y=58+25*i;
        uint32_t color = index == ui->highlight ? HIGHLIGHT : WHITE;
        fill(7,y,306,23,color);
        outline(7,y,306,23,index == ui->highlight ? BLUE : BORDER);
        if ((size_t)index >= ui->store.count) continue;
        snprintf(line,sizeof(line),"%2d  %.23s",index+1,ui->store.items[index].name);
        label(14,y+8,line,292,INK);
        label(295,y+8,">",310,BLUE);
    }
}
void connection_ui_draw(const ConnectionUI *ui)
{
    u16 width = 0, height = 0;
    if (!ui) return;
    fb = (uint16_t*)gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,&width,&height);
    (void)width; (void)height;
    if (!fb) return;
    fill(0,0,UI_W,UI_H,BG);
    topbar();
    if (!ui->show_saved) {
        tabs(ui);
        main_panel(ui);
    } else {
        char count[24];
        fill(0,31,320,23,WHITE);
        snprintf(count,sizeof(count),"SAVED CONNECTIONS: %u",(unsigned)ui->store.count);
        label(10,40,count,265,INK);
        label(270,40,"UP/DN",319,MUTED);
        saved_panel(ui);
    }
    fill(0,185,320,55,C(231,235,240));
    fill(0,185,320,1,BORDER);
    label(7,193,"STATUS:",54,MUTED);
    label(54,193,ui->message,313,INK);
    if (ui->show_saved) {
        button(4,209,74,"BACK",0);
        button(82,209,74,"NEW",0);
        button(160,209,76,ui->pending_delete >= 0 ? "SURE?" : "DELETE",0);
        button(240,209,76,"NEXT",1);
    } else {
        button(4,209,65,"SAVE",0);
        button(72,209,75,"SAVE AS",0);
        button(150,209,75,"OPEN",0);
        button(228,209,88,"CONNECT",1);
    }
}


/* The session screen shares the menu's tiny font and 16-bit framebuffer.
 * The top 3DS screen remains dedicated to the Windows desktop. */
void session_pad_draw(const SessionPad *pad, int zoom_percent)
{
    u16 width=0,height=0;
    char line[64];
    if (!pad) return;
    fb=(uint16_t*)gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,&width,&height);
    if(!fb)return;
    (void)width;(void)height;
    fill(0,0,320,240,C(221,225,231));

    /* Four compact toolbar buttons: keep the desktop unobstructed above. */
    static const char *titles[]={"DISCONNECT","SETTINGS","KEYBOARD","HELP"};
    for(int i=0;i<4;++i){
        const int x=i*80;
        const bool active=(i==0 && pad->disconnect_armed) ||
                          (i==1 && pad->settings_open) ||
                          (i==2 && pad->keyboard_open) ||
                          (i==3 && pad->help_open);
        const uint32_t fill_color=i==0 && pad->disconnect_armed ?
            C(160,61,67):active?BLUE:PALE;
        fill(x,0,80,32,fill_color);
        outline(x,0,80,32,BORDER);
        label(x+7,13,i==0 && pad->disconnect_armed?"CONFIRM?":titles[i],
              x+79,active?WHITE:INK);
    }

    if(pad->keyboard_open){
        for(int row=0;row<4;++row)for(int col=0;col<4;++col){
            const int x=4+col*78,y=40+row*48;
            const int key=session_pad_keyboard_key(pad,x+20,y+20);
            const char *name=(key==SK_FN && pad->fn_page)?
                             "BASIC":session_key_label(key);
            const bool selected=(key==SK_CTRL && (pad->key_mods&SK_MOD_CTRL)) ||
                (key==SK_ALT && (pad->key_mods&SK_MOD_ALT)) ||
                (key==SK_SHIFT && (pad->key_mods&SK_MOD_SHIFT)) ||
                (key==SK_WIN && (pad->key_mods&SK_MOD_WIN));
            fill(x,y,74,44,selected?BLUE:WHITE);
            outline(x,y,74,44,selected?NAVY:BORDER);
            label(x+6,y+19,name,x+73,selected?WHITE:INK);
        }
        return;
    }
    if(pad->help_open){
        fill(5,37,310,196,WHITE);
        outline(5,37,310,196,BORDER);
        label(15,46,"TOUCHPAD HELP",306,NAVY);
        label(15,66,"SWIPE: MOVE MOUSE",307,INK);
        label(15,82,"TAP: LEFT CLICK",307,INK);
        label(15,98,"A + SWIPE: DRAG  /  B: RIGHT",307,INK);
        label(15,114,"SCROLL: USE RIGHT-HAND STRIP",307,INK);
        label(15,130,"X OR KEYBOARD: TYPE / SHORTCUTS",307,INK);
        label(15,146,"L + CIRCLE ROTATION: SMOOTH ZOOM",307,INK);
        label(15,162,"Y: FIT    R: DETAIL / FIT",307,INK);
        label(15,178,"L+X: COLOR SWAP  L+Y: SMOOTH",307,INK);
        fill(14,199,292,1,C(224,230,238));
        label(17,211,"START: EXIT  /  HELP: CLOSE",304,MUTED);
        return;
    }
    if(pad->settings_open){
        fill(5,37,310,195,WHITE);
        outline(5,37,310,195,BORDER);
        label(14,46,"INPUT SETTINGS",307,NAVY);
        fill(12,56,296,48,WHITE);outline(12,56,296,48,BORDER);
        snprintf(line,sizeof(line),"CURSOR SPEED: %d X",pad->sensitivity);
        label(20,76,line,290,BLUE);label(277,76,"V",301,BLUE);
        fill(12,108,296,48,WHITE);outline(12,108,296,48,BORDER);
        snprintf(line,sizeof(line),"SCROLL STEP: %d NOTCH",pad->scroll_amount);
        label(20,128,line,291,BLUE);label(277,128,"V",301,BLUE);
        fill(12,160,296,48,WHITE);outline(12,160,296,48,BORDER);
        label(20,180,pad->reverse_scroll ?
              "REVERSE SCROLL: ON" : "REVERSE SCROLL: OFF",291,BLUE);
        label(277,180,"V",301,BLUE);
        label(16,220,"BLUE LABELS ARE EDITABLE",304,MUTED);
        return;
    }

    /* Laptop trackpad: soft outer lip, recessed gray contact surface.
     * Instructions are deliberately hidden behind the Help button. */
    fill(3,35,264,177,C(180,187,196));
    outline(3,35,264,177,C(139,149,161));
    fill(6,38,258,170,C(211,216,224));
    outline(6,38,258,170,C(158,169,182));
    fill(9,41,252,164,C(221,225,232));
    fill(9,41,252,1,C(242,244,246));
    fill(9,204,252,1,C(182,189,199));
    fill(15,47,16,1,C(167,175,186));
    fill(15,47,1,16,C(167,175,186));
    fill(240,47,15,1,C(167,175,186));
    fill(254,47,1,16,C(167,175,186));
    fill(15,193,16,1,C(167,175,186));
    fill(15,178,1,16,C(167,175,186));
    fill(240,193,15,1,C(167,175,186));
    fill(254,178,1,16,C(167,175,186));
    snprintf(line,sizeof(line),"ZOOM %d%%",zoom_percent);
    label(19,53,line,251,MUTED);
    label(68,120,"TOUCHPAD",205,C(147,155,166));
    label(67,140,"SWIPE TO MOVE",225,C(160,168,178));
    if(pad->touching && pad->captured==PAD_AREA)
        outline(8,40,254,166,C(105,145,187));

    /* Proper independent vertical scroll control, not a fake slider
     * that moves the desktop viewport rather than remote wheel notches. */
    fill(270,36,46,42,WHITE);outline(270,36,46,42,BORDER);
    label(285,53,"UP",313,BLUE);
    fill(270,80,46,111,C(229,233,239));
    outline(270,80,46,111,BORDER);
    fill(290,88,5,94,C(190,199,210));
    fill(285,122,15,29,C(135,155,178));
    outline(285,122,15,29,C(107,127,150));
    fill(270,193,46,45,WHITE);outline(270,193,46,45,BORDER);
    label(279,212,"DOWN",314,BLUE);

    /* Two inset mechanical-button zones; same touch geometry as before. */
    fill(4,214,128,24,C(240,243,246));
    outline(4,214,128,24,C(159,168,179));
    fill(137,214,129,24,C(240,243,246));
    outline(137,214,129,24,C(159,168,179));
    label(50,223,"LEFT",127,INK);
    label(176,223,"RIGHT",260,INK);
}

void connection_ui_draw_status(const char *heading,const char *message,bool retry)
{
    u16 w=0,h=0;
    fb=(uint16_t*)gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,&w,&h);
    if(!fb)return;
    fill(0,0,320,240,BG);topbar();
    fill(10,40,300,136,WHITE);outline(10,40,300,136,BORDER);
    label(20,55,heading?heading:"RDP STATUS",307,BLUE);
    /* Long connection failure reasons include important numeric error codes.
       Wrap onto up to three rows rather than clipping the code at the edge. */
    if (message) {
        const char *cursor=message;
        for (int row=0; row<3 && *cursor; ++row) {
            char line[45];
            size_t n=strlen(cursor);
            if (n>44) {
                n=44;
                while (n>20 && cursor[n]!=' ') --n;
                if (n<=20) n=44;
            }
            memcpy(line,cursor,n);
            line[n]=0;
            label(20,88+row*20,line,306,INK);
            cursor+=n;
            while (*cursor==' ') ++cursor;
        }
    }
    if(retry) {
        button(10,205,145,"RETRY (A)",1);
        button(165,205,145,"BACK (B)",0);
    } else {
        button(10,205,300,"PLEASE WAIT",0);
    }
}
void connection_ui_draw_cert_manager(const char *title,const char *info,
                                      const char *notice,int confirmation)
{
    u16 w=0,h=0;
    fb=(uint16_t*)gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,&w,&h);
    if(!fb)return;
    fill(0,0,320,240,BG);topbar();
    fill(8,38,304,154,WHITE);outline(8,38,304,154,BORDER);
    label(16,51,title?title:"TRUSTED CERTIFICATES",308,BLUE);
    label(16,91,info?info:"",306,INK);
    label(16,157,notice?notice:"",304,MUTED);
    if(confirmation){
        button(8,206,146,"REMOVE (A)",1);
        button(164,206,148,"CANCEL (B)",0);
    }else {
        button(8,206,98,"VIEW (A)",1);
        button(111,206,98,"DELETE (X)",0);
        button(214,206,98,"BACK (B)",0);
    }
}
