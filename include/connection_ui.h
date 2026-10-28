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
    char name[RDP_PROFILE_NAME_CAP];
    char message[80];
    int selected;       /* -1 = unsaved connection */
    int focus;          /* General tab field 0..4 */
    int tab;
    int scroll;         /* saved-connections list first visible item */
    int highlight;      /* selected row in saved connections view */
    int pending_load;   /* second tap confirms discarding unsaved edits */
    int pending_delete; /* -1 normally; requires second deliberate action */
    bool show_saved;
    bool dirty;
    bool connect_requested;
    bool profiles_unavailable; /* refuse to overwrite a corrupt SD profile store */
} ConnectionUI;

/* O3DS bottom-screen framebuffer UI, top console reserved for diagnostics.
 * Fields edit through the 3DS on-screen keyboard, no secrets are persisted. */
void connection_ui_init(ConnectionUI *ui, const char *legacy_file, const char *profile_file);
void connection_ui_draw(const ConnectionUI *ui);
void connection_ui_key(ConnectionUI *ui, uint32_t keys, const char *profile_file);
void connection_ui_touch(ConnectionUI *ui, int x, int y, const char *profile_file);
bool connection_ui_ready(const ConnectionUI *ui, char *error, size_t cap);

/* Pure hit testing shared by app and portable host tests. */
int connection_ui_field_at(int x, int y);
int connection_ui_tab_at(int x, int y);
int connection_ui_saved_at(int x, int y, int scroll, int count);

#endif
