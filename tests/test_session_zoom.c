#include "session_zoom.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static void clockwise(SessionZoom *zoom, int revolutions)
{
    int i;
    for (i=0;i<=32*revolutions;i++) {
        float angle=2.0f*SESSION_ZOOM_PI*(float)i/32.0f;
        int dx=(int)(120.0f*cosf(angle));
        int dy=-(int)(120.0f*sinf(angle));
        session_zoom_update(zoom,true,dx,dy);
    }
}
static void counterclockwise(SessionZoom *zoom, int revolutions)
{
    int i;
    for (i=0;i<=32*revolutions;i++) {
        float angle=-2.0f*SESSION_ZOOM_PI*(float)i/32.0f;
        int dx=(int)(120.0f*cosf(angle));
        int dy=-(int)(120.0f*sinf(angle));
        session_zoom_update(zoom,true,dx,dy);
    }
}
static void settle(SessionZoom *zoom)
{
    int i;
    for (i=0;i<100;i++) session_zoom_update(zoom,false,0,0);
}
int main(void)
{
    SessionZoom z;
    SessionZoomView v;
    int x,y;

    session_zoom_init(&z);
    assert(session_zoom_percent(&z)==100);
    v=session_zoom_view(&z,200,120,400,240);
    assert(v.left_q16==0 && v.top_q16==0);
    session_zoom_sample(&v,100,50,400,240,400,240,&x,&y);
    assert(x==100 && y==50);

    /* Without L no rotation changes target. */
    session_zoom_update(&z,false,110,0);
    session_zoom_update(&z,false,0,-110);
    assert(z.target==1.0f);
    clockwise(&z,1);
    assert(z.target > 1.93f && z.target < 2.07f);
    settle(&z);
    assert(session_zoom_percent(&z)>=194 && session_zoom_percent(&z)<=205);
    v=session_zoom_view(&z,200,120,400,240);
    session_zoom_sample(&v,200,120,400,240,400,240,&x,&y);
    assert(x==200 && y==120); /* zoom focuses at pointer, not desktop center */
    session_zoom_sample(&v,0,0,400,240,400,240,&x,&y);
    assert(x>=96 && x<=104 && y>=57 && y<=63);

    v=session_zoom_view(&z,10,12,400,240);
    assert(v.left_q16==0 && v.top_q16==0); /* clamp near upper-left */
    session_zoom_sample(&v,200,120,400,240,400,240,&x,&y);
    assert(x>10 && y>12); /* edge clamping prevents off-screen samples */
    v=session_zoom_view(&z,399,239,400,240);
    session_zoom_sample(&v,400,240,400,240,400,240,&x,&y);
    assert(x==399 && y==239);

    /* A new touch in a different stick angle must not jump. */
    session_zoom_update(&z,false,0,0);
    {
        float value=z.target;
        session_zoom_update(&z,true,0,110);
        assert(z.target==value);
        session_zoom_update(&z,true,1,1); /* recenters / cancels turn */
        session_zoom_update(&z,true,-110,0);
        assert(z.target==value);
    }

    session_zoom_update(&z,false,0,0);
    counterclockwise(&z,1);
    assert(z.target>=1.0f && z.target<1.07f);
    settle(&z);
    assert(session_zoom_percent(&z)==100);

    /* Maximum 500% permits native-detail panning at 1080p. */
    session_zoom_update(&z,false,0,0);
    clockwise(&z,6);
    assert(z.target==5.0f);
    settle(&z);
    assert(session_zoom_percent(&z)==500);
    v=session_zoom_view(&z,200,120,400,240);
    session_zoom_sample(&v,200,120,400,240,400,240,&x,&y);
    assert(x==200 && y==120);
    session_zoom_sample(&v,-100,1000,400,240,400,240,&x,&y);
    assert(x>=0 && x<400 && y>=0 && y<240);

    session_zoom_update(&z,false,0,0);
    counterclockwise(&z,4);
    assert(z.target==1.0f);
    settle(&z);
    assert(session_zoom_percent(&z)==100);
    /* A 1080p source can be fully viewed or cropped to 1:1 via native zoom. */
    z.visible=1.0f;
    v=session_zoom_view(&z,960,540,1920,1080);
    session_zoom_sample(&v,200,120,400,240,1920,1080,&x,&y);
    assert(x==960 && y==540);
    z.visible=4.8f;
    v=session_zoom_view(&z,960,540,1920,1080);
    session_zoom_sample(&v,200,120,400,240,1920,1080,&x,&y);
    assert(x==960 && y==540);
    puts("Circle Pad clockwise/counterclockwise cursor-centered zoom tests passed");
    return 0;
}
