#ifndef RDP_SESSION_CURSOR_H
#define RDP_SESSION_CURSOR_H
#include <stdbool.h>
#include <stdint.h>
#define RDP_CURSOR_MAX 128
typedef struct {
    uint8_t bgra[RDP_CURSOR_MAX*RDP_CURSOR_MAX*4];
    int width,height,hot_x,hot_y;
    bool visible,shape_known;
} SessionCursor;
void session_cursor_reset(SessionCursor *cursor);
bool session_cursor_set(SessionCursor *cursor,const uint8_t *bgra,
                        int width,int height,int hot_x,int hot_y);
void session_cursor_overlay(uint16_t *frame,int out_w,int out_h,
                            const SessionCursor *cursor,int x,int y,
                            int32_t left_q16,int32_t top_q16,
                            int32_t width_q16,int32_t height_q16);
#endif
