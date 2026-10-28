/* Native, allocation-free O3DS software UI. No Citro2D dependency.
 * Framebuffer: GSP_BGR8, 240-pixel tall column-major, vertically inverted. */
#include "connection_ui.h"
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

static uint8_t *fb;

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
    uint8_t *p;
    if (!fb || x < 0 || x >= UI_W || y < 0 || y >= UI_H) return;
    p = fb + (((size_t)x * UI_H) + (UI_H - 1 - y)) * 3;
    p[0] = (uint8_t)(rgb & 255);
    p[1] = (uint8_t)((rgb >> 8) & 255);
    p[2] = (uint8_t)((rgb >> 16) & 255);
}
static void fill(int x, int y, int w, int h, uint32_t color)
{
    int i, j;
    if (x < 0) {w += x; x = 0;}
    if (y < 0) {h += y; y = 0;}
    if (x + w > UI_W) w = UI_W - x;
    if (y + h > UI_H) h = UI_H - y;
    if (w <= 0 || h <= 0 || !fb) return;
    for (i = x; i < x+w; ++i) {
        uint8_t *p = fb + (((size_t)i * UI_H)+(UI_H-1-y))*3;
        for (j = 0; j < h; ++j, p -= 3) {
            p[0] = (uint8_t)(color & 255);
            p[1] = (uint8_t)((color >> 8) & 255);
            p[2] = (uint8_t)((color >> 16) & 255);
        }
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
        panel_text(103,"CERTIFICATE VALIDATION","UNTRUSTED CERTS REJECTED");
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
    fb = gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,&width,&height);
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
