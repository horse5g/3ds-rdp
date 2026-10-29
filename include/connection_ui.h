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
    bool cache_password_for_run; /* opt-in: RAM only, never persisted */
    bool gdi_bgrx_for_run; /* experimental 32-bit output order; not saved */
    bool profiles_unavailable; /* refuse to overwrite a corrupt SD profile store */
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
bool connection_ui_ready(const ConnectionUI *ui, char *error, size_t cap);

/* Pure hit testing shared by app and portable host tests. */
int connection_ui_field_at(int x, int y);
int connection_ui_tab_at(int x, int y);
int connection_ui_saved_at(int x, int y, int scroll, int count);

#endif
