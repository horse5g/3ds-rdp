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
    if (x < 7 || x >= 313 || y < 58 || y >= 183) return -1;
    index = (y - 58) / 25;
    offset = (y - 58) % 25;
    if (offset >= 23 || index >= 5 || scroll < 0 || scroll + index >= count) return -1;
    return scroll + index;
}
