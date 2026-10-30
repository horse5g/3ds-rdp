#include "connection_ui.h"

int connection_ui_tab_at(int x, int y)
{
    if (x < 0 || x >= 320 || y < 23 || y >= 46) return -1;
    return x / 64;
}

int connection_ui_field_at(int x, int y)
{
    /* The three visible edit rows share one hit geometry on both pages.
     * Page -> absolute field conversion lives in the controller. */
    if (x < 8 || x >= 295 || y < 75 || y >= 165) return -1;
    return (y - 75) / 30;
}

int connection_ui_saved_at(int x, int y, int scroll, int count)
{
    int index, offset;
    if (x < 7 || x >= 292 || y < 58 || y >= 183) return -1;
    index = (y - 58) / 25;
    offset = (y - 58) % 25;
    if (offset >= 23 || index >= 5 || scroll < 0 || scroll + index >= count) return -1;
    return scroll + index;
}

/* All scrollbar math is pure: host tests exercise it without libctru.
 * Thumb travel and touch mapping use the same clamped range as the list. */
int connection_ui_scroll_clamp(int first, int count, int visible)
{
    const int max = count > visible ? count - visible : 0;
    if (first < 0) return 0;
    if (first > max) return max;
    return first;
}

int connection_ui_scroll_thumb_h(int height, int count, int visible)
{
    int h;
    if (height <= 0) return 0;
    if (count <= visible || count <= 0) return height;
    h = (int)(((int64_t)height * visible) / count);
    if (h < 16) h = 16;
    if (h > height) h = height;
    return h;
}

int connection_ui_scroll_thumb_y(int top, int height, int count,
                                 int visible, int first)
{
    const int max = count > visible ? count - visible : 0;
    const int travel = height - connection_ui_scroll_thumb_h(height,count,visible);
    if (max <= 0 || travel <= 0) return top;
    return top + (connection_ui_scroll_clamp(first,count,visible)*travel + max/2)/max;
}

int connection_ui_scroll_at(int y, int top, int height, int count, int visible)
{
    const int max = count > visible ? count - visible : 0;
    const int travel = height - connection_ui_scroll_thumb_h(height,count,visible);
    int offset;
    if (max <= 0 || travel <= 0) return 0;
    offset = y - top - connection_ui_scroll_thumb_h(height,count,visible)/2;
    if (offset < 0) offset = 0;
    if (offset > travel) offset = travel;
    return (offset*max + travel/2)/travel;
}

int connection_ui_scroll_drag(int first, int dy, int pixels_per_row,
                              int count, int visible)
{
    int delta;
    if (pixels_per_row <= 0) return connection_ui_scroll_clamp(first,count,visible);
    /* Dragging the content upward reveals later rows. Round symmetrically. */
    if (dy < 0) delta = ((-dy) + pixels_per_row/2) / pixels_per_row;
    else delta = -((dy + pixels_per_row/2) / pixels_per_row);
    return connection_ui_scroll_clamp(first + delta,count,visible);
}
