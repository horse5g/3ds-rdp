#include "session_cursor.h"
#include <string.h>
void session_cursor_reset(SessionCursor *c) { if(c)memset(c,0,sizeof(*c)); }
bool session_cursor_set(SessionCursor *c,const uint8_t *bgra,int w,int h,int hx,int hy)
{
    if(!c||!bgra||w<1||h<1||w>RDP_CURSOR_MAX||h>RDP_CURSOR_MAX||
       hx<0||hy<0||hx>=w||hy>=h)return false;
    memcpy(c->bgra,bgra,(size_t)w*h*4);
    c->width=w;c->height=h;c->hot_x=hx;c->hot_y=hy;
    c->shape_known=true;c->visible=true;
    return true;
}
void session_cursor_overlay(uint16_t *frame,int out_w,int out_h,
    const SessionCursor *c,int px,int py,int32_t left,int32_t top,
    int32_t width,int32_t height)
{
    int start_x,start_y,draw_w,draw_h,dx,dy;
    if(!frame||!c||!c->visible||!c->shape_known||!width||!height||
       out_w<=0||out_h<=0)return;
    /* All positions are remote-desktop coordinates. Scale cursor pixels by
       the same factor as the zoomed desktop and honor Windows hotspot. */
    start_x=(int)(((int64_t)(px-c->hot_x)*65536-left)*out_w/width);
    start_y=(int)(((int64_t)(py-c->hot_y)*65536-top)*out_h/height);
    draw_w=(int)((int64_t)c->width*out_w*65536/width);
    draw_h=(int)((int64_t)c->height*out_h*65536/height);
    if(draw_w<1)draw_w=1;if(draw_h<1)draw_h=1;
    for(dy=0;dy<draw_h;++dy) {
        int y=start_y+dy;
        int sy=dy*c->height/draw_h;
        if(y<0||y>=out_h)continue;
        for(dx=0;dx<draw_w;++dx) {
            int x=start_x+dx,sx=dx*c->width/draw_w;
            const uint8_t *p;uint16_t old,native;
            unsigned a,r,g,b;
            if(x<0||x>=out_w)continue;
            p=&c->bgra[((size_t)sy*c->width+sx)*4];a=p[3];
            if(!a)continue;
            native=frame[(size_t)x*out_h+(out_h-1-y)];
            if(a==255){r=p[2];g=p[1];b=p[0];}
            else {
                unsigned br=((native>>11)&31)*255/31;
                unsigned bg=((native>>5)&63)*255/63;
                unsigned bb=(native&31)*255/31;
                r=(p[2]*a+br*(255-a))/255;
                g=(p[1]*a+bg*(255-a))/255;
                b=(p[0]*a+bb*(255-a))/255;
            }
            old=(uint16_t)(((r>>3)<<11)|((g>>2)<<5)|(b>>3));
            frame[(size_t)x*out_h+(out_h-1-y)]=old;
        }
    }
}
