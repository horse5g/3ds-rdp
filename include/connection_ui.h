#ifndef RDP_CONNECTION_UI_H
#define RDP_CONNECTION_UI_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "profile_store.h"

typedef struct {
    RdpProfileStore store;
    RdpConfig current;
    char name[RDP_PROFILE_NAME_CAP];
    char message[80];
    int selected; /* -1 = unsaved connection */
    int focus;
    bool show_saved;
    bool dirty;
} ConnectionUI;

/* Bottom-screen console UI. TOP is reserved for diagnostics / RDP rendering. */
void connection_ui_init(ConnectionUI *ui, const char *legacy_file, const char *profile_file);
void connection_ui_draw(const ConnectionUI *ui);
void connection_ui_key(ConnectionUI *ui, uint32_t keys, const char *profile_file);
void connection_ui_touch(ConnectionUI *ui, int x, int y, const char *profile_file);
bool connection_ui_ready(const ConnectionUI *ui, char *error, size_t cap);
#endif
