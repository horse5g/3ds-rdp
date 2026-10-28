#include "session_zoom.h"
#include <math.h>

#define ZOOM_DEADZONE_SQ (38*38)
#define ZOOM_SMOOTHING 0.18f
#define ZOOM_EPSILON 0.002f
#define ZOOM_MAX_FRAME_ROTATION 1.20f

static float clampf(float value,float low,float high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}
static int clampi(int value, int low, int high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

void session_zoom_init(SessionZoom *state)
{
    if (!state) return;
    state->target=SESSION_ZOOM_MIN;
    state->visible=SESSION_ZOOM_MIN;
    state->last_angle=0;
    state->angle_valid=false;
}

bool session_zoom_update(SessionZoom *state, bool l_held, int circle_x, int circle_y)
{
    float before, next;
    if (!state) return false;
    before = state->visible;
    if (!l_held || circle_x*circle_x+circle_y*circle_y < ZOOM_DEADZONE_SQ) {
        /* Re-centering the stick must not cause a zoom jump. */
        state->angle_valid=false;
    } else {
        /* Convert positive-up stick Y to positive-down screen Y.
           Screen-space atan2 increases on clockwise rotation. */
        next=atan2f((float)-circle_y,(float)circle_x);
        if (state->angle_valid) {
            float delta=next-state->last_angle;
            if (delta > SESSION_ZOOM_PI) delta-=2.0f*SESSION_ZOOM_PI;
            if (delta < -SESSION_ZOOM_PI) delta+=2.0f*SESSION_ZOOM_PI;
            if (fabsf(delta) < ZOOM_MAX_FRAME_ROTATION)
                state->target=clampf(state->target+
                  delta/(2.0f*SESSION_ZOOM_PI),SESSION_ZOOM_MIN,SESSION_ZOOM_MAX);
        }
        state->last_angle=next;
        state->angle_valid=true;
    }
    /* Update even after releasing L, finishing the easing animation. */
    next=state->target-state->visible;
    if (fabsf(next) < ZOOM_EPSILON) state->visible=state->target;
    else state->visible+=next*ZOOM_SMOOTHING;
    return fabsf(before-state->visible)>0.00001f;
}

SessionZoomView session_zoom_view(const SessionZoom *state, int mouse_x, int mouse_y,
                                 int desktop_w, int desktop_h)
{
    SessionZoomView view={0,0,0,0};
    float scale=state ? clampf(state->visible,SESSION_ZOOM_MIN,SESSION_ZOOM_MAX):1.0f;
    float crop_w, crop_h, left, top;
    if (desktop_w <= 0 || desktop_h <= 0) return view;
    crop_w=(float)desktop_w/scale;
    crop_h=(float)desktop_h/scale;
    mouse_x=clampi(mouse_x,0,desktop_w-1);
    mouse_y=clampi(mouse_y,0,desktop_h-1);
    left=clampf((float)mouse_x-crop_w*0.5f,0,(float)desktop_w-crop_w);
    top=clampf((float)mouse_y-crop_h*0.5f,0,(float)desktop_h-crop_h);
    view.left_q16=(int32_t)(left*65536.0f);
    view.top_q16=(int32_t)(top*65536.0f);
    view.width_q16=(int32_t)(crop_w*65536.0f);
    view.height_q16=(int32_t)(crop_h*65536.0f);
    return view;
}

void session_zoom_sample(const SessionZoomView *view, int x, int y,
                         int screen_w, int screen_h, int desktop_w, int desktop_h,
                         int *sx, int *sy)
{
    int64_t pixel_x=0, pixel_y=0;
    if (!sx || !sy) return;
    if (view && screen_w > 0 && screen_h > 0 && desktop_w > 0 && desktop_h > 0) {
        pixel_x=((int64_t)view->left_q16+
                 (int64_t)x*view->width_q16/screen_w+32768)>>16;
        pixel_y=((int64_t)view->top_q16+
                 (int64_t)y*view->height_q16/screen_h+32768)>>16;
    }
    *sx=clampi((int)pixel_x,0,desktop_w>0?desktop_w-1:0);
    *sy=clampi((int)pixel_y,0,desktop_h>0?desktop_h-1:0);
}

int session_zoom_percent(const SessionZoom *state)
{
    float value=state ? state->visible : 1.0f;
    return (int)(clampf(value,SESSION_ZOOM_MIN,SESSION_ZOOM_MAX)*100.0f+0.5f);
}
