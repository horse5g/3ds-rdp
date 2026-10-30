#ifndef RDP_CONNECTION_UI_H
#define RDP_CONNECTION_UI_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "profile_store.h"

typedef enum {
    RDP_UI_GENERAL = 0,
    RDP_UI_DISPLAY,
    RDP_UI_LOCAL,
    RDP_UI_EXPERIENCE,
    RDP_UI_ADVANCED,
    RDP_UI_TAB_COUNT
} RdpUiTab;

/* Shared display and input geometry for 320x240 launcher scrollbars. */
#define RDP_UI_SAVED_BAR_X 295
#define RDP_UI_SAVED_BAR_Y 58
#define RDP_UI_SAVED_BAR_H 125
#define RDP_UI_CHOICE_BAR_X 280
#define RDP_UI_CHOICE_BAR_Y 80
#define RDP_UI_CHOICE_BAR_H 110
#define RDP_UI_GENERAL_BAR_X 296
#define RDP_UI_GENERAL_BAR_Y 56
#define RDP_UI_GENERAL_BAR_H 120

typedef enum {
    RDP_UI_DRAG_NONE = 0,
    RDP_UI_DRAG_GENERAL,
    RDP_UI_DRAG_GENERAL_BAR,
    RDP_UI_DRAG_SAVED,
    RDP_UI_DRAG_SAVED_BAR,
    RDP_UI_DRAG_CHOICE,
    RDP_UI_DRAG_CHOICE_BAR
} RdpUiDragRegion;

typedef struct {
    RdpProfileStore store;
    RdpConfig current;
    RdpUiOptions options;
    char name[RDP_PROFILE_NAME_CAP];
    char message[80];
    int selected;       /* -1 = unsaved connection */
    int focus;          /* General field 0..4, settings row 0..2 */
    int panel_scroll;   /* General editor page: 0 logon, 1 connection */
    int choice_menu;    /* 0=closed; other IDs are active option dropdowns */
    int choice_highlight; /* D-pad selection within the open dropdown */
    int choice_scroll;    /* first of up to 5 displayed choices */
    int tab;
    int scroll;         /* saved-connections list first visible item */
    int highlight;      /* selected row in saved connections view */
    int pending_load;   /* second tap confirms discarding unsaved edits */
    int pending_delete; /* -1 normally; requires second deliberate action */
    bool show_saved;
    bool dirty;
    bool connect_requested;
    bool gdi_bgrx_for_run; /* experimental 32-bit output order; not saved */
    bool profiles_unavailable; /* refuse to overwrite a corrupt SD profile store */
    bool touch_active;
    bool touch_dragged;
    RdpUiDragRegion touch_region;
    int touch_start_x, touch_start_y;
    int touch_start_scroll;
    int touch_thumb_offset; /* stylus offset from thumb center; prevents jumps */
} ConnectionUI;

/* O3DS bottom-screen framebuffer UI, top console reserved for diagnostics.
 * Fields edit through the 3DS on-screen keyboard, no secrets are persisted. */
void connection_ui_init(ConnectionUI *ui, const char *legacy_file, const char *profile_file);
void connection_ui_draw(const ConnectionUI *ui);
void connection_ui_draw_status(const char *heading,const char *message, bool retry);
void connection_ui_draw_cert_manager(const char *title,const char *info,
                                      const char *notice,int confirmation);
void connection_ui_key(ConnectionUI *ui, uint32_t keys, const char *profile_file);
void connection_ui_touch(ConnectionUI *ui, int x, int y, const char *profile_file);
/* Continuous touch state; dispatch taps on release, scroll on stylus drag. */
void connection_ui_touch_update(ConnectionUI *ui, bool held, int x, int y,
                                const char *profile_file);
bool connection_ui_ready(const ConnectionUI *ui, char *error, size_t cap);

/* Pure hit testing shared by app and portable host tests. */
int connection_ui_field_at(int x, int y);
int connection_ui_tab_at(int x, int y);
int connection_ui_saved_at(int x, int y, int scroll, int count);
int connection_ui_scroll_clamp(int first, int count, int visible);
int connection_ui_scroll_thumb_h(int height, int count, int visible);
int connection_ui_scroll_thumb_y(int top, int height, int count,
                                 int visible, int first);
int connection_ui_scroll_at(int y, int top, int height, int count, int visible);
int connection_ui_scroll_drag(int first, int dy, int pixels_per_row,
                              int count, int visible);

#endif
