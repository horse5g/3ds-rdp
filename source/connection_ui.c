#include "connection_ui.h"
#include "display_buffers.h"
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
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
    ui->options=rdp_options_default();
    ui->name[0] = 0;
    ui->selected = -1;
    ui->dirty = false;
    ui->focus = 0;
    ui->panel_scroll = 0;
    ui->choice_menu = 0;
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
    ui->options = ui->store.items[index].options;
    snprintf(ui->name, sizeof(ui->name), "%s", ui->store.items[index].name);
    ui->show_saved = false;
    ui->tab = RDP_UI_GENERAL;
    ui->panel_scroll = 0;
    ui->choice_menu = 0;
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
    rdp_display_restore_after_keyboard();
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
    candidate.items[index].options=ui->options;
    if (rdp_profiles_save(path, &candidate, error, sizeof(error))) {
        tell(ui, error); return;
    }
    ui->store = candidate;
    select_profile(ui, index);
    tell(ui, "Connection saved on SD");
}
/* The saved profile remains selected unless the new name is genuinely
 * unused. Save As must never silently overwrite another connection. */
static int name_matches(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        ++a; ++b;
    }
    return !*a && !*b;
}
static void save_as_profile(ConnectionUI *ui, const char *path)
{
    char previous[RDP_PROFILE_NAME_CAP];
    int selected = ui->selected;
    if (ui->profiles_unavailable) {
        tell(ui, "Repair saved profiles on SD first");
        return;
    }
    memcpy(previous, ui->name, sizeof(previous));
    if (!edit_field(ui, F_NAME)) return;
    for (size_t i=0; i<ui->store.count; ++i) {
        if (name_matches(ui->name,ui->store.items[i].name)) {
            memcpy(ui->name, previous, sizeof(previous));
            ui->selected=selected;
            tell(ui, "Save As needs a different profile name");
            return;
        }
    }
    ui->selected = -1;
    save_profile(ui,path);
}
/* Native option dropdowns are populated from actual persisted preferences,
 * not dummy UI rows. Disabled transport/security features are informational. */
static int option_menu_for_row(const ConnectionUI *ui, int row)
{
    if (row < 0 || row > 2) return 0;
    switch (ui->tab) {
    case RDP_UI_DISPLAY: return row == 0 ? 1 : row == 1 ? 2 : 0;
    case RDP_UI_LOCAL: return row + 3;
    case RDP_UI_EXPERIENCE: return row == 0 ? 6 : 0;
    case RDP_UI_ADVANCED: return row == 0 ? 7 : 0;
    default: return 0;
    }
}
static int choice_count(int menu)
{
    switch (menu) {
    case 1: return 6;
    case 2: case 5: case 6: return 2;
    case 3: case 4: case 7: return 3;
    default: return 0;
    }
}
static void choose_option(ConnectionUI *ui, int choice)
{
    RdpUiOptions *o = &ui->options;
    if (choice < 0 || choice >= choice_count(ui->choice_menu)) return;
    switch(ui->choice_menu) {
    case 1: o->display_mode=(uint8_t)choice; break;
    case 2: o->color_depth=(uint8_t)(choice ? 32 : 16); break;
    case 3: o->touch_speed=(uint8_t)(choice+1); break;
    case 4: o->scroll_step=(uint8_t)(choice+1); break;
    case 5: o->reverse_scroll=(uint8_t)choice; break;
    case 6: o->low_bandwidth=(uint8_t)choice; break;
    case 7: o->timeout_seconds=(uint8_t)(choice==0?8:choice==1?12:20); break;
    }
    ui->choice_menu=0;
    ui->dirty=true;
    tell(ui,"Option updated - Save to keep");
}
static void saved_view(ConnectionUI *ui)
{
    ui->show_saved = true;
    ui->choice_menu = 0;
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
static void open_option(ConnectionUI *ui, int row)
{
    const int menu = option_menu_for_row(ui,row);
    if (menu) {
        ui->choice_menu=menu;
        ui->focus=row;
        switch(menu){
            case 1: ui->choice_highlight=ui->options.display_mode; break;
            case 2: ui->choice_highlight=ui->options.color_depth==32?1:0; break;
            case 3: ui->choice_highlight=ui->options.touch_speed-1; break;
            case 4: ui->choice_highlight=ui->options.scroll_step-1; break;
            case 5: ui->choice_highlight=ui->options.reverse_scroll; break;
            case 6: ui->choice_highlight=ui->options.low_bandwidth; break;
            case 7: ui->choice_highlight=ui->options.timeout_seconds==8?0:
                                         ui->options.timeout_seconds==12?1:2; break;
            default: ui->choice_highlight=0;
        }
        if (ui->choice_highlight<0 || ui->choice_highlight>=choice_count(menu))
            ui->choice_highlight=0;
        ui->choice_scroll=ui->choice_highlight>=5?ui->choice_highlight-4:0;
        tell(ui,"Use D-pad or tap to choose a value");
    }
}
static void change_tab(ConnectionUI *ui, int tab)
{
    if (tab < 0 || tab >= RDP_UI_TAB_COUNT) return;
    ui->tab = tab;
    ui->choice_menu=0;
    ui->panel_scroll=0;
    reset_pending(ui);
    ui->focus=0;
    if (tab != RDP_UI_GENERAL)
        tell(ui, "Tap a setting to cycle options");
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
        if (keys & KEY_X) { ui->show_saved=false; reset_pending(ui); return; }
        if (keys & KEY_L) { clear_profile(ui); ui->show_saved=false; return; }
        if ((keys & KEY_DUP) && ui->highlight>0) --ui->highlight;
        if ((keys & KEY_DDOWN) && (size_t)(ui->highlight+1)<ui->store.count)
            ++ui->highlight;
        ui->scroll=(ui->highlight/5)*5;
        if ((keys & KEY_A) && ui->store.count) request_load(ui,ui->highlight);
        if (keys & KEY_SELECT) delete_profile(ui,path);
        return;
    }
    if (ui->choice_menu) {
        if(keys & (KEY_B | KEY_X)){ui->choice_menu=0;return;}
        if((keys & KEY_DUP) && ui->choice_highlight>0)
            --ui->choice_highlight;
        if((keys & KEY_DDOWN) &&
           ui->choice_highlight+1<choice_count(ui->choice_menu))
            ++ui->choice_highlight;
        if(ui->choice_highlight<ui->choice_scroll)
            ui->choice_scroll=ui->choice_highlight;
        if(ui->choice_highlight>=ui->choice_scroll+5)
            ui->choice_scroll=ui->choice_highlight-4;
        if(keys & KEY_A){choose_option(ui,ui->choice_highlight);return;}
        return;
    }
    if (keys & KEY_DLEFT) change_tab(ui,(ui->tab+RDP_UI_TAB_COUNT-1)%RDP_UI_TAB_COUNT);
    if (keys & KEY_DRIGHT) change_tab(ui,(ui->tab+1)%RDP_UI_TAB_COUNT);
    if (ui->tab==RDP_UI_GENERAL) {
        if(keys & KEY_DUP)ui->focus=(ui->focus+F_COUNT-1)%F_COUNT;
        if(keys & KEY_DDOWN)ui->focus=(ui->focus+1)%F_COUNT;
        ui->panel_scroll=ui->focus>=3?1:0;
        if(keys & KEY_A)edit_field(ui,ui->focus);
    } else {
        if(keys & KEY_DUP)ui->focus=(ui->focus+2)%3;
        if(keys & KEY_DDOWN)ui->focus=(ui->focus+1)%3;
        if(keys & KEY_A)open_option(ui,ui->focus);
    }
    if(keys & KEY_Y)save_profile(ui,path);
    if(keys & KEY_X)saved_view(ui);
    if(keys & KEY_L)clear_profile(ui);
    if(keys & KEY_SELECT)delete_profile(ui,path);
}
void connection_ui_touch(ConnectionUI *ui, int x, int y, const char *path)
{
    int hit;
    if (x<0 || x>=320 || y<0 || y>=240) return;
    if (ui->choice_menu) {
        /* Modal list: no background controls can be activated through it. */
        const int visible=choice_count(ui->choice_menu)<5?
                          choice_count(ui->choice_menu):5;
        if(x>=280 && x<299 && y>=80 && y<190 &&
           choice_count(ui->choice_menu)>5) {
            if(y<135 && ui->choice_scroll>0) --ui->choice_scroll;
            if(y>=135 && ui->choice_scroll+5<choice_count(ui->choice_menu))
                ++ui->choice_scroll;
            ui->choice_highlight=ui->choice_scroll;
        } else if(x>=30 && x<280 && y>=80 && y<80+22*visible) {
            choose_option(ui,ui->choice_scroll+(y-80)/22);
        } else ui->choice_menu=0;
        return;
    }
    if (y<23 && x>=260) { saved_view(ui); return; }
    if (ui->show_saved) {
        hit=connection_ui_saved_at(x,y,ui->scroll,(int)ui->store.count);
        if(hit>=0){ui->highlight=hit;request_load(ui,hit);return;}
        if(y>=209) {
            if(x<80){ui->show_saved=false;reset_pending(ui);}
            else if(x<158){clear_profile(ui);ui->show_saved=false;}
            else if(x<238)delete_profile(ui,path);
            else {
                ui->scroll+=5;
                if((size_t)ui->scroll>=ui->store.count)ui->scroll=0;
                ui->highlight=ui->scroll;
                reset_pending(ui);
                tell(ui,"Next saved computers");
            }
        }
        return;
    }
    hit=connection_ui_tab_at(x,y);
    if(hit>=0){change_tab(ui,hit);return;}
    if(ui->tab==RDP_UI_GENERAL) {
        if(x>=295 && y>=53 && y<185) {
            ui->panel_scroll = y<115 ? 0 : 1;
            ui->focus=ui->panel_scroll ? 3 : 0;
            return;
        }
        hit=connection_ui_field_at(x,y);
        if(hit>=0) {
            const int field=hit+ui->panel_scroll*3;
            if(field<F_COUNT) {
                ui->focus=field;
                if(field==F_COMPUTER && x>=264) saved_view(ui);
                else edit_field(ui,field);
            }
            return;
        }
    } else if (y>=75 && y<165 && x>=9 && x<295) {
        open_option(ui,(y-75)/30);
        return;
    }
    if(y>=209) {
        if(x<71)save_profile(ui,path);
        else if(x<150)save_as_profile(ui,path);
        else if(x<228)saved_view(ui);
        else {
            char error[96];
            if(connection_ui_ready(ui,error,sizeof(error))) {
                ui->connect_requested=true;
                tell(ui,"Connecting to selected computer");
            } else tell(ui,error);
        }
    }
}
