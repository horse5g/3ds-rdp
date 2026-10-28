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
#define NAVY C(23,68,122)
#define BLUE C(0,107,190)
#define WHITE C(255,255,255)
#define BG C(240,243,247)
#define BORDER C(174,185,198)
#define INK C(37,47,60)
#define MUTED C(100,113,129)
#define HIGHLIGHT C(217,235,252)
#define PALE C(250,251,253)

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
static void topbar(void)
{
    fill(0,0,320,29,NAVY);
    fill(10,7,14,14,WHITE);
    fill(12,9,4,4,BLUE); fill(18,9,4,4,BLUE);
    fill(12,15,4,4,BLUE); fill(18,15,4,4,BLUE);
    label(31,11,"Remote Desktop Connection",258,WHITE);
    label(268,11,"SAVED",318,WHITE);
}
static void tabs(const ConnectionUI *ui)
{
    static const char *titles[] = {"GENERAL","DISPLAY","LOCAL","EXPERIENCE","ADVANCED"};
    int i;
    for (i=0; i<RDP_UI_TAB_COUNT; ++i) {
        int x = i*64;
        int active = ui->tab == i;
        fill(x,31,64,23,active ? WHITE : C(219,226,235));
        outline(x,31,64,23,active ? BLUE : BORDER);
        label(x+2,40,titles[i],x+63,active ? BLUE : INK);
    }
}
static void field(int row, const char *name, const char *value, bool focus)
{
    int y = 57 + row*25;
    fill(6,y,308,22,focus ? HIGHLIGHT : BG);
    label(12,y+8,name,94,MUTED);
    fill(95,y+2,213,18,WHITE);
    outline(95,y+2,213,18,focus ? BLUE : BORDER);
    if (value && value[0])
        label(101,y+8,value,300,INK);
    else
        label(101,y+8,"(NOT SET)",300,MUTED);
}
static void panel_text(int y,const char *heading,const char *sub)
{
    fill(8,y,304,35,WHITE);
    outline(8,y,304,35,BORDER);
    label(16,y+8,heading,305,INK);
    label(16,y+20,sub,305,MUTED);
}
static void main_panel(const ConnectionUI *ui)
{
    char port[9];
    if (ui->tab == RDP_UI_GENERAL) {
        snprintf(port,sizeof(port),"%u",(unsigned)ui->current.port);
        field(0,"COMPUTER",ui->current.host,ui->focus == 0);
        field(1,"USER NAME",ui->current.username,ui->focus == 1);
        field(2,"DOMAIN",ui->current.domain,ui->focus == 2);
        field(3,"PORT",port,ui->focus == 3);
        field(4,"SAVE AS",ui->name,ui->focus == 4);
        return;
    }
    switch(ui->tab) {
    case RDP_UI_DISPLAY:
        panel_text(63,"DESKTOP SIZE: 400 X 240","O3DS TOP SCREEN NATIVE SIZE");
        panel_text(103,"COLOR: 32 BIT","EXPERIMENTAL CLIENT DEFAULT");
        panel_text(143,"DISPLAY OPTIONS","NOT YET CONFIGURABLE");
        break;
    case RDP_UI_LOCAL:
        panel_text(63,"TOUCH: MOUSE POINTER","A BUTTON: LEFT MOUSE CLICK");
        panel_text(103,"X: SOFTWARE KEYBOARD","PASSWORD PROMPT AT LOGIN");
        panel_text(143,"LOCAL DEVICE REDIRECTION","NOT IMPLEMENTED");
        break;
    case RDP_UI_EXPERIENCE:
        panel_text(63,"NETWORK: DIRECT TCP","NO GATEWAY OR PROXY");
        panel_text(103,"QUALITY PRESETS","NOT YET CONFIGURABLE");
        panel_text(143,"FULL DESKTOP","RENDERER STILL EXPERIMENTAL");
        break;
    default:
        panel_text(63,"SECURITY: NLA / CREDSSP","NO INSECURE RDP FALLBACK");
        panel_text(103,"CERTIFICATE TRUST","UNKNOWN: CONFIRM / SAVE PIN");
        panel_text(143,"SAVED CREDENTIALS","PASSWORD NEVER STORED");
        break;
    }
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
    fill(0,185,320,55,C(220,229,239));
    fill(0,185,320,1,BORDER);
    label(7,193,ui->message,313,MUTED);
    if (ui->show_saved) {
        button(4,209,74,"BACK",0);
        button(82,209,74,"NEW",0);
        button(160,209,76,ui->pending_delete >= 0 ? "SURE?" : "DELETE",0);
        button(240,209,76,"NEXT",1);
    } else {
        button(4,209,74,"SAVE",0);
        button(82,209,74,"SAVED",0);
        button(160,209,64,"NEW",0);
        button(228,209,88,"CONNECT",1);
    }
}


/* The session screen shares the menu's tiny font and 16-bit framebuffer.
 * The top 3DS screen remains dedicated to the Windows desktop. */
void session_pad_draw(const SessionPad *pad, int zoom_percent)
{
    u16 width=0, height=0;
    uint32_t active, panel;
    char line[60];
    if (!pad) return;
    fb=(uint16_t*)gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,&width,&height);
    if (!fb) return;
    (void)width;
    (void)height;
    fill(0,0,320,240,BG);

    /* Top row of the *touchscreen*: never obscure the desktop above. */
    fill(0,0,320,32,NAVY);
    active=pad->disconnect_armed ? C(191,46,47) : C(143,42,54);
    fill(2,2,104,28,active);
    label(10,13,pad->disconnect_armed ? "CONFIRM EXIT" : "DISCONNECT",105,WHITE);
    fill(109,2,101,28,pad->settings_open ? BLUE : NAVY);
    outline(109,2,101,28,BORDER);
    label(119,13,pad->settings_open ? "BACK TO PAD" : "SETTINGS",208,WHITE);
    fill(213,2,105,28,BLUE);
    label(222,13,"KEYBOARD",316,WHITE);

    if (pad->settings_open) {
        fill(6,37,308,195,WHITE);
        outline(6,37,308,195,BORDER);
        label(15,46,"SESSION INPUT SETTINGS",305,INK);
        panel=C(233,241,249);
        fill(12,56,296,48,panel);
        outline(12,56,296,48,BORDER);
        snprintf(line,sizeof(line),"CURSOR SPEED: %dX  (TAP TO CHANGE)",pad->sensitivity);
        label(20,76,line,301,INK);
        fill(12,108,296,48,panel);
        outline(12,108,296,48,BORDER);
        snprintf(line,sizeof(line),"SCROLL STEP: %d  (TAP TO CHANGE)",pad->scroll_amount);
        label(20,128,line,301,INK);
        fill(12,160,296,48,panel);
        outline(12,160,296,48,BORDER);
        label(20,180,pad->reverse_scroll ?
              "REVERSE SCROLL: ON" : "REVERSE SCROLL: OFF",301,INK);
        label(16,220,"L+CIRCLE ROTATE: ZOOM 100%-300%",304,MUTED);
        return;
    }

    fill(4,36,262,175,C(249,251,254));
    outline(4,36,262,175,BORDER);
    label(20,45,"TOUCHPAD",256,MUTED);
    snprintf(line,sizeof(line),"ZOOM %d%%",zoom_percent);
    label(190,45,line,263,BLUE);
    label(24,94,"DRAG FINGER TO MOVE MOUSE",258,MUTED);
    label(28,109,"LIFT AND REPOSITION ANYTIME",258,MUTED);
    label(57,126,"TAP TO LEFT CLICK",258,MUTED);
    label(20,177,"L+CIRCLE ROTATE: ZOOM",262,BLUE);
    label(20,193,"A: DRAG  B: RIGHT CLICK",262,MUTED);
    /* Thin inner grip marks, no cursor teleport on contact. */
    fill(16,62,238,1,C(226,234,243));
    fill(16,184,238,1,C(226,234,243));

    /* Vertical scroll controls occupy the entire right edge. */
    fill(270,36,46,42,BLUE);
    outline(270,36,46,42,NAVY);
    label(277,53,"UP",313,WHITE);

    fill(270,80,46,111,C(222,234,247));
    outline(270,80,46,111,BORDER);
    fill(289,88,8,95,BORDER);
    fill(286,129,14,19,BLUE);
    label(274,94,"SC",312,INK);
    label(274,108,"ROLL",312,INK);
    label(274,171,"DRAG",312,INK);

    fill(270,193,46,45,BLUE);
    outline(270,193,46,45,NAVY);
    label(276,213,"DOWN",315,WHITE);

    fill(4,214,128,24,WHITE);
    outline(4,214,128,24,BORDER);
    label(45,223,"LEFT",128,INK);
    fill(137,214,129,24,WHITE);
    outline(137,214,129,24,BORDER);
    label(174,223,"RIGHT",260,INK);
}
