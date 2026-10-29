#ifndef RDP_SESSION_ZOOM_H
#define RDP_SESSION_ZOOM_H
#include <stdbool.h>
#include <stdint.h>

#define SESSION_ZOOM_MIN 1.0f
#define SESSION_ZOOM_MAX 5.0f
#define SESSION_ZOOM_PI 3.14159265358979323846f

/* Zooms only the 400x240 top-screen viewport; source desktop can be 1080p.
 * The remote pointer stays in desktop coordinates. No remote wheel. */
typedef struct {
    float target;
    float visible;
    float last_angle;
    bool angle_valid;
} SessionZoom;

/* Crop in 16.16 remote pixels, including subpixel origin. */
typedef struct {
    int32_t left_q16, top_q16;
    int32_t width_q16, height_q16;
} SessionZoomView;

void session_zoom_init(SessionZoom *state);
/* circle_y is positive UP, as reported by libctru's circlePosition.dy.
 * Rotation is interpreted clockwise as seen looking down at the console.
 * Returns true only if visible magnification changed significantly. */
bool session_zoom_update(SessionZoom *state, bool l_held, int circle_x, int circle_y);
SessionZoomView session_zoom_view(const SessionZoom *state, int mouse_x, int mouse_y,
                                  int desktop_w, int desktop_h);
/* Screen pixel -> original desktop sample (nearest neighbor, clamped). */
void session_zoom_sample(const SessionZoomView *view, int display_x, int display_y,
                         int screen_w, int screen_h, int desktop_w, int desktop_h,
                         int *source_x, int *source_y);
int session_zoom_percent(const SessionZoom *state);
#endif
