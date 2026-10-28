#include "connection_ui.h"

int connection_ui_tab_at(int x, int y)
{
    if (x < 0 || x >= 320 || y < 30 || y >= 54) return -1;
    return x / 64;
}

int connection_ui_field_at(int x, int y)
{
    int index, offset;
    if (x < 5 || x >= 315 || y < 57 || y >= 182) return -1;
    index = (y - 57) / 25;
    offset = (y - 57) % 25;
    return index < 5 && offset < 22 ? index : -1;
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
