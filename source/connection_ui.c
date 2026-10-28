#include "connection_ui.h"
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { FIELD_COMPUTER, FIELD_PORT, FIELD_USER, FIELD_DOMAIN, FIELD_NAME, FIELD_COUNT };
static const char *labels[] = { "Computer", "Port", "User name", "Domain", "Save as" };

static void tell(ConnectionUI *ui, const char *message) {
    snprintf(ui->message, sizeof(ui->message), "%s", message);
}
static void clear_profile(ConnectionUI *ui) {
    memset(&ui->current, 0, sizeof(ui->current));
    ui->current.port = 3389;
    ui->name[0] = 0;
    ui->selected = -1;
    ui->dirty = false;
    tell(ui, "New connection");
}
static void select_profile(ConnectionUI *ui, int index) {
    if (index < 0 || (size_t)index >= ui->store.count) return;
    ui->selected = index;
    ui->current = ui->store.items[index].config;
    snprintf(ui->name, sizeof(ui->name), "%s", ui->store.items[index].name);
    ui->show_saved = false;
    ui->dirty = false;
    tell(ui, "Saved connection loaded");
}
static void edit_field(ConnectionUI *ui, int focus) {
    char buffer[256] = {0};
    SwkbdState keyboard;
    SwkbdButton button;
    char *target = NULL;
    size_t cap = 0;
    int kind = SWKBD_TYPE_NORMAL;
    char hint[60];
    if (focus == FIELD_COMPUTER) { target = ui->current.host; cap = sizeof(ui->current.host); }
    if (focus == FIELD_USER) { target = ui->current.username; cap = sizeof(ui->current.username); }
    if (focus == FIELD_DOMAIN) { target = ui->current.domain; cap = sizeof(ui->current.domain); }
    if (focus == FIELD_NAME) { target = ui->name; cap = sizeof(ui->name); }
    if (focus == FIELD_PORT) {
        snprintf(buffer, sizeof(buffer), "%u", (unsigned)ui->current.port);
        kind = SWKBD_TYPE_NUMPAD;
    } else if (target) snprintf(buffer, sizeof(buffer), "%s", target);
    else return;
    snprintf(hint, sizeof(hint), "%s", labels[focus]);
    swkbdInit(&keyboard, kind, 2, (int)(cap ? cap - 1 : 5));
    swkbdSetHintText(&keyboard, hint);
    swkbdSetInitialText(&keyboard, buffer);
    button = swkbdInputText(&keyboard, buffer, sizeof(buffer));
    if (button != SWKBD_BUTTON_CONFIRM) return;
    if (focus == FIELD_PORT) {
        char *end = NULL;
        unsigned long port = strtoul(buffer, &end, 10);
        if (!buffer[0] || !end || *end || port == 0 || port > 65535) {
            tell(ui, "Port must be 1-65535"); return;
        }
        ui->current.port = (uint16_t)port;
    } else if (target) {
        if (strlen(buffer) >= cap) { tell(ui, "Text too long"); return; }
        snprintf(target, cap, "%s", buffer);
    }
    ui->dirty = true;
    tell(ui, "Edited (save to keep changes)");
}
static void save_profile(ConnectionUI *ui, const char *path) {
    RdpProfileStore candidate = ui->store;
    char error[96];
    int index;
    if (!ui->name[0]) {
        ui->focus = FIELD_NAME;
        tell(ui, "Enter a Save as name first");
        return;
    }
    index = rdp_profiles_upsert(&candidate, ui->name, &ui->current, error, sizeof(error));
    if (index < 0) { tell(ui, error); return; }
    if (rdp_profiles_save(path, &candidate, error, sizeof(error))) {
        tell(ui, error); return;
    }
    ui->store = candidate;
    select_profile(ui, index);
    tell(ui, "Connection saved to SD");
}
static void delete_profile(ConnectionUI *ui, const char *path) {
    RdpProfileStore candidate = ui->store;
    char error[96];
    if (ui->selected < 0 || (size_t)ui->selected >= candidate.count) {
        tell(ui, "Select a saved profile first"); return;
    }
    rdp_profiles_delete(&candidate, (size_t)ui->selected);
    if (rdp_profiles_save(path, &candidate, error, sizeof(error))) {
        tell(ui, error); return;
    }
    ui->store = candidate;
    clear_profile(ui);
    ui->show_saved = true;
    tell(ui, "Saved connection deleted");
}
void connection_ui_init(ConnectionUI *ui, const char *legacy_file, const char *profile_file) {
    FILE *file;
    char buffer[4097], error[96];
    size_t size;
    memset(ui, 0, sizeof(*ui));
    clear_profile(ui);
    if (rdp_profiles_load(profile_file, &ui->store, error, sizeof(error))) tell(ui, error);
    if (ui->store.count) { select_profile(ui, 0); return; }
    file = fopen(legacy_file, "rb");
    if (!file) return;
    size = fread(buffer, 1, sizeof(buffer), file);
    fclose(file);
    if (size >= sizeof(buffer) || memchr(buffer, 0, size)) return;
    buffer[size] = 0;
    if (rdp_config_parse(buffer, &ui->current, error, sizeof(error)) == 0) {
        snprintf(ui->name, sizeof(ui->name), "%s", "Imported host.txt");
        ui->selected = -1;
        ui->dirty = true;
        tell(ui, "Imported host.txt - press Y to save");
    } else clear_profile(ui);
}
bool connection_ui_ready(const ConnectionUI *ui, char *error, size_t cap) {
    return rdp_profile_validate(&ui->current, error, cap) == 0;
}
void connection_ui_draw(const ConnectionUI *ui) {
    unsigned i;
    printf("\x1b[2J\x1b[H");
    printf(" Remote Desktop Connection\n");
    printf(" ------------------------------\n");
    if (ui->show_saved) {
        printf(" Saved computers (%u/%u)\n", (unsigned)ui->store.count, RDP_PROFILE_LIMIT);
        for (i = 0; i < ui->store.count; ++i)
            printf(" %c%2u %.25s\n", (int)i == ui->selected ? '>' : ' ', i + 1, ui->store.items[i].name);
        printf("\n Touch a profile to load it.\n");
        printf(" X: New   L: Editor   SELECT: Delete\n");
    } else {
        char port[8];
        const char *fields[FIELD_COUNT];
        snprintf(port, sizeof(port), "%u", (unsigned)ui->current.port);
        fields[0] = ui->current.host; fields[1] = port;
        fields[2] = ui->current.username; fields[3] = ui->current.domain;
        fields[4] = ui->name;
        for (i = 0; i < FIELD_COUNT; ++i)
            printf(" %c%-10s %.19s\n", ui->focus == (int)i ? '>' : ' ',
                   labels[i], fields[i][0] ? fields[i] : "(empty)");
        printf("\n A: Edit   Y: Save   X: Saved\n");
        printf(" L: New    R: Probe   SELECT: Delete\n");
    }
    printf("\n %.34s\n", ui->message);
    printf(" Passwords are NEVER saved.\n");
    printf(" START: Exit\n");
}
void connection_ui_key(ConnectionUI *ui, uint32_t keys, const char *path) {
    if (ui->show_saved) {
        if (keys & KEY_L) { ui->show_saved = false; return; }
        if (keys & KEY_X) { clear_profile(ui); ui->show_saved = false; return; }
        if (keys & KEY_DUP) {
            if (ui->selected > 0) select_profile(ui, ui->selected - 1);
            ui->show_saved = true;
        }
        if (keys & KEY_DDOWN) {
            if ((size_t)(ui->selected + 1) < ui->store.count)
                select_profile(ui, ui->selected + 1);
            ui->show_saved = true;
        }
        if ((keys & KEY_A) && ui->store.count) {
            select_profile(ui, ui->selected < 0 ? 0 : ui->selected);
        }
        if (keys & KEY_SELECT) delete_profile(ui, path);
        return;
    }
    if (keys & KEY_DUP) ui->focus = (ui->focus + FIELD_COUNT - 1) % FIELD_COUNT;
    if (keys & KEY_DDOWN) ui->focus = (ui->focus + 1) % FIELD_COUNT;
    if (keys & KEY_A) edit_field(ui, ui->focus);
    if (keys & KEY_Y) save_profile(ui, path);
    if (keys & KEY_X) ui->show_saved = true;
    if (keys & KEY_L) clear_profile(ui);
    if (keys & KEY_SELECT) delete_profile(ui, path);
}
void connection_ui_touch(ConnectionUI *ui, int x, int y, const char *path) {
    int row;
    (void)path;
    if (x < 0 || x >= 320 || y < 0 || y >= 240) return;
    /* libctru console uses 8x8 glyphs; fields begin on row 2. */
    row = y / 8;
    if (ui->show_saved) {
        if (row >= 3 && row < 3 + (int)ui->store.count)
            select_profile(ui, row - 3);
    } else if (row >= 2 && row < 2 + FIELD_COUNT) {
        ui->focus = row - 2;
        edit_field(ui, ui->focus);
    }
}
