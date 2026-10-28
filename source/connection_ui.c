#include "connection_ui.h"
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { F_COMPUTER, F_USER, F_DOMAIN, F_PORT, F_NAME, F_COUNT };
static const char *labels[] = { "Computer", "User name", "Domain", "Port", "Save as" };

static void tell(ConnectionUI *ui, const char *message)
{
    snprintf(ui->message, sizeof(ui->message), "%s", message);
}
static void reset_pending(ConnectionUI *ui)
{
    ui->pending_delete = -1;
    ui->pending_load = -1;
}
static void clear_profile(ConnectionUI *ui)
{
    memset(&ui->current, 0, sizeof(ui->current));
    ui->current.port = 3389;
    ui->name[0] = 0;
    ui->selected = -1;
    ui->dirty = false;
    ui->focus = 0;
    ui->tab = RDP_UI_GENERAL;
    reset_pending(ui);
    tell(ui, "New connection");
}
static void select_profile(ConnectionUI *ui, int index)
{
    if (index < 0 || (size_t)index >= ui->store.count) return;
    ui->selected = index;
    ui->highlight = index;
    ui->current = ui->store.items[index].config;
    snprintf(ui->name, sizeof(ui->name), "%s", ui->store.items[index].name);
    ui->show_saved = false;
    ui->tab = RDP_UI_GENERAL;
    ui->dirty = false;
    reset_pending(ui);
    tell(ui, "Saved connection loaded");
}
/* Allow Windows-style DOMAIN\user as well as a separate Domain field.
 * Reuse the actual parser so editing and SD loading follow identical rules. */
static bool normalize_username(ConnectionUI *ui, const char *input)
{
    char text[512], error[96];
    RdpConfig parsed;
    int n = snprintf(text,sizeof(text),"host=rdp.local\n%s%s%s%s%s%s",
                     input[0] ? "username=" : "", input,
                     input[0] ? "\n" : "",
                     ui->current.domain[0] ? "domain=" : "",
                     ui->current.domain,
                     ui->current.domain[0] ? "\n" : "");
    if (n < 0 || (size_t)n >= sizeof(text) ||
        rdp_config_parse(text,&parsed,error,sizeof(error))) {
        tell(ui,n < 0 || (size_t)n >= sizeof(text) ?
             "User name too long" : error);
        return false;
    }
    snprintf(ui->current.username,sizeof(ui->current.username),"%s",parsed.username);
    snprintf(ui->current.domain,sizeof(ui->current.domain),"%s",parsed.domain);
    return true;
}
static bool edit_field(ConnectionUI *ui, int field)
{
    char buffer[256] = {0}, hint[60];
    SwkbdState keyboard;
    SwkbdButton button;
    char *target = NULL;
    size_t cap = 0;
    int kind = SWKBD_TYPE_NORMAL;
    switch (field) {
        case F_COMPUTER: target = ui->current.host; cap = sizeof(ui->current.host); break;
        case F_USER: target = ui->current.username; cap = sizeof(ui->current.username); break;
        case F_DOMAIN: target = ui->current.domain; cap = sizeof(ui->current.domain); break;
        case F_NAME: target = ui->name; cap = sizeof(ui->name); break;
        case F_PORT:
            snprintf(buffer, sizeof(buffer), "%u", (unsigned)ui->current.port);
            kind = SWKBD_TYPE_NUMPAD;
            break;
        default: return false;
    }
    if (target) snprintf(buffer, sizeof(buffer), "%s", target);
    snprintf(hint, sizeof(hint), "%s", labels[field]);
    swkbdInit(&keyboard, kind, 2, (int)(cap ? cap - 1 : 5));
    swkbdSetHintText(&keyboard, hint);
    swkbdSetInitialText(&keyboard, buffer);
    button = swkbdInputText(&keyboard, buffer, sizeof(buffer));
    if (button != SWKBD_BUTTON_CONFIRM) return false;
    if (field == F_PORT) {
        char *end = NULL;
        unsigned long port = strtoul(buffer, &end, 10);
        if (!buffer[0] || !end || *end || port == 0 || port > 65535) {
            tell(ui, "Port must be 1-65535"); return false;
        }
        ui->current.port = (uint16_t)port;
    } else if (target) {
        if (strlen(buffer) >= cap) {
            tell(ui, "Text is too long"); return false;
        }
        if (field == F_USER) {
            if (!normalize_username(ui,buffer)) return false;
        } else snprintf(target, cap, "%s", buffer);
    }
    ui->focus = field;
    ui->dirty = true;
    reset_pending(ui);
    tell(ui, "Edited - save to keep changes");
    return true;
}
static void save_profile(ConnectionUI *ui, const char *path)
{
    if (ui->profiles_unavailable) {
        tell(ui, "Repair the saved profiles on SD first");
        return;
    }
    RdpProfileStore candidate = ui->store;
    char error[96];
    int index;
    if (!ui->name[0] && !edit_field(ui, F_NAME)) {
        tell(ui, "Save cancelled: enter a name");
        return;
    }
    index = rdp_profiles_upsert(&candidate, ui->name, &ui->current, error, sizeof(error));
    if (index < 0) { tell(ui, error); return; }
    if (rdp_profiles_save(path, &candidate, error, sizeof(error))) {
        tell(ui, error); return;
    }
    ui->store = candidate;
    select_profile(ui, index);
    tell(ui, "Connection saved on SD");
}
static void saved_view(ConnectionUI *ui)
{
    ui->show_saved = true;
    ui->highlight = ui->selected >= 0 ? ui->selected : 0;
    if (ui->highlight >= (int)ui->store.count && ui->store.count)
        ui->highlight = (int)ui->store.count - 1;
    ui->scroll = (ui->highlight / 5) * 5;
    reset_pending(ui);
    tell(ui, "Tap a computer to load");
}
static void request_load(ConnectionUI *ui, int index)
{
    if (index < 0 || (size_t)index >= ui->store.count) return;
    if (ui->dirty && ui->pending_load != index) {
        ui->pending_load = index;
        ui->pending_delete = -1;
        tell(ui, "Unsaved edits: tap again to load");
        return;
    }
    select_profile(ui, index);
}
static void delete_profile(ConnectionUI *ui, const char *path)
{
    if (ui->profiles_unavailable) {
        tell(ui, "Repair the saved profiles on SD first");
        return;
    }
    RdpProfileStore candidate = ui->store;
    char error[96];
    int index = ui->show_saved ? ui->highlight : ui->selected;
    if (index < 0 || (size_t)index >= candidate.count) {
        tell(ui, "No stored computer selected"); return;
    }
    if (ui->pending_delete != index) {
        ui->pending_delete = index;
        ui->pending_load = -1;
        tell(ui, "Press Delete again to confirm");
        return;
    }
    rdp_profiles_delete(&candidate, (size_t)index);
    if (rdp_profiles_save(path, &candidate, error, sizeof(error))) {
        tell(ui, error); return;
    }
    ui->store = candidate;
    if (ui->selected == index) {
        clear_profile(ui);
    } else if (ui->selected > index) {
        --ui->selected;
    }
    ui->show_saved = true;
    if (ui->highlight >= (int)candidate.count)
        ui->highlight = candidate.count ? (int)candidate.count - 1 : 0;
    ui->scroll = (ui->highlight / 5) * 5;
    reset_pending(ui);
    tell(ui, "Saved connection deleted");
}
static void change_tab(ConnectionUI *ui, int tab)
{
    if (tab < 0 || tab >= RDP_UI_TAB_COUNT) return;
    ui->tab = tab;
    reset_pending(ui);
    if (tab != RDP_UI_GENERAL)
        tell(ui, "Info only - advanced options pending");
    else
        tell(ui, "Touch a field to edit");
}
void connection_ui_init(ConnectionUI *ui, const char *legacy_file, const char *profile_file)
{
    FILE *file;
    char buffer[4097], error[96];
    size_t size;
    memset(ui, 0, sizeof(*ui));
    clear_profile(ui);
    if (rdp_profiles_load(profile_file, &ui->store, error, sizeof(error))) {
        /* A damaged profile store must not be overwritten by an empty UI.
         * Preserve both the primary and backup for offline recovery. */
        ui->profiles_unavailable = true;
        tell(ui, "Profiles damaged; repair the SD file");
        return;
    }
    if (ui->store.count) { select_profile(ui, 0); return; }
    file = fopen(legacy_file, "rb");
    if (!file) return;
    size = fread(buffer, 1, sizeof(buffer), file);
    if (ferror(file) || size >= sizeof(buffer) || memchr(buffer, 0, size)) {
        fclose(file);
        tell(ui, "Cannot import host.txt");
        return;
    }
    fclose(file);
    buffer[size] = 0;
    if (rdp_config_parse(buffer, &ui->current, error, sizeof(error)) == 0) {
        snprintf(ui->name, sizeof(ui->name), "%s", "Imported host.txt");
        ui->selected = -1;
        ui->dirty = true;
        tell(ui, "Imported host.txt; Save to keep");
    } else {
        clear_profile(ui);
        tell(ui, "Check host.txt or enter a computer");
    }
}
bool connection_ui_ready(const ConnectionUI *ui, char *error, size_t cap)
{
    return ui && rdp_profile_validate(&ui->current, error, cap) == 0;
}
void connection_ui_key(ConnectionUI *ui, uint32_t keys, const char *path)
{
    if (ui->show_saved) {
        if (keys & KEY_X) { ui->show_saved = false; reset_pending(ui); return; }
        if (keys & KEY_L) { clear_profile(ui); ui->show_saved = false; return; }
        if (keys & KEY_DUP) {
            if (ui->highlight > 0) --ui->highlight;
            ui->scroll = (ui->highlight/5)*5;
            reset_pending(ui);
        }
        if (keys & KEY_DDOWN) {
            if ((size_t)(ui->highlight+1) < ui->store.count) ++ui->highlight;
            ui->scroll = (ui->highlight/5)*5;
            reset_pending(ui);
        }
        if ((keys & KEY_A) && ui->store.count) request_load(ui, ui->highlight);
        if (keys & KEY_SELECT) delete_profile(ui, path);
        return;
    }
    if (keys & KEY_DLEFT) change_tab(ui, (ui->tab + RDP_UI_TAB_COUNT - 1) % RDP_UI_TAB_COUNT);
    if (keys & KEY_DRIGHT) change_tab(ui, (ui->tab + 1) % RDP_UI_TAB_COUNT);
    if (ui->tab == RDP_UI_GENERAL) {
        if (keys & KEY_DUP) ui->focus = (ui->focus + F_COUNT - 1) % F_COUNT;
        if (keys & KEY_DDOWN) ui->focus = (ui->focus + 1) % F_COUNT;
        if (keys & KEY_A) edit_field(ui, ui->focus);
    }
    if (keys & KEY_Y) save_profile(ui, path);
    if (keys & KEY_X) saved_view(ui);
    if (keys & KEY_L) clear_profile(ui);
    if (keys & KEY_SELECT) delete_profile(ui, path);
}
void connection_ui_touch(ConnectionUI *ui, int x, int y, const char *path)
{
    int hit;
    if (x < 0 || x >= 320 || y < 0 || y >= 240) return;
    if (y < 29 && x >= 260) { saved_view(ui); return; }
    if (ui->show_saved) {
        hit = connection_ui_saved_at(x,y,ui->scroll,(int)ui->store.count);
        if (hit >= 0) {
            ui->highlight = hit;
            request_load(ui,hit);
            return;
        }
        if (y >= 209) {
            if (x < 80) { ui->show_saved = false; reset_pending(ui); }
            else if (x < 158) { clear_profile(ui); ui->show_saved = false; }
            else if (x < 238) delete_profile(ui,path);
            else {
                ui->scroll += 5;
                if ((size_t)ui->scroll >= ui->store.count) ui->scroll = 0;
                ui->highlight = ui->scroll;
                reset_pending(ui);
                tell(ui, "Next saved computers");
            }
        }
        return;
    }
    hit = connection_ui_tab_at(x,y);
    if (hit >= 0) { change_tab(ui,hit); return; }
    if (ui->tab == RDP_UI_GENERAL) {
        hit = connection_ui_field_at(x,y);
        if (hit >= 0) {
            ui->focus = hit;
            edit_field(ui,hit);
            return;
        }
    }
    if (y >= 209) {
        if (x < 80) save_profile(ui,path);
        else if (x < 158) saved_view(ui);
        else if (x < 226) { clear_profile(ui); ui->show_saved = false; }
        else {
            char error[96];
            if (connection_ui_ready(ui,error,sizeof(error))) {
                ui->connect_requested = true;
                tell(ui, "Connecting to selected computer");
            } else tell(ui,error);
        }
    }
}
