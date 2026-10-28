#include <3ds.h>
#include <malloc.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "connection_ui.h"
#include "network.h"
#include "horizon_sync.h"
#include "loopback_pair.h"
#include "horizon_rng.h"
#include "freerdp_session.h"

#define HOST_FILE "sdmc:/3ds/3ds-rdp/host.txt"
#define PROFILES_FILE "sdmc:/3ds/3ds-rdp/connections.txt"
#define SOC_SIZE (1024*1024)

static PrintConsole top;
static ConnectionUI ui;
static char status[100] = "Ready", detail[100] = "Edit a computer on the touch screen";

static void draw_status(int network_ready)
{
    char validation[96];
    bool valid = connection_ui_ready(&ui,validation,sizeof(validation));
    consoleSelect(&top);
    printf("\x1b[2J\x1b[H3DS Remote Desktop (O3DS)\n");
    printf("=========================\n\n");
    printf("Computer: %.38s\n", valid ? ui.current.host : "(not configured)");
    printf("Port: %u\n", (unsigned)ui.current.port);
    printf("User: %.30s\n",ui.current.username[0] ? ui.current.username : "(not set)");
    printf("Domain: %.30s\n\n",ui.current.domain[0] ? ui.current.domain : "(not set)");
    printf("Status: %.36s\n",status);
    printf("Detail: %.36s\n\n",detail);
    if (!network_ready) printf("Network initialization failed.\n");
    else if (!valid) printf("Set a valid host before connecting.\n");
#ifdef RDP_FULL_CLIENT
    printf("R: Probe  L+R: Experimental RDP\n");
#else
    printf("R: RDP negotiation probe only\n");
    printf("Full client not linked.\n");
#endif
    printf("B: Events  L+Y: SOC  L+SEL: RNG\n");
    printf("START: Exit\n\n");
    printf("Credentials are never saved.\n");
}

static void probe(const ConnectionUI *state)
{
    RdpNegResult answer;
    char error[96];
    snprintf(status,sizeof(status),"Probing RDP security...");
    if (network_rdp_probe(state->current.host,state->current.port,&answer,error,sizeof(error))) {
        snprintf(status,sizeof(status),"Probe failed");
        snprintf(detail,sizeof(detail),"%s",error);
    } else if (answer.kind == RDP_NEG_SELECTED) {
        snprintf(status,sizeof(status),"RDP negotiation succeeded");
        snprintf(detail,sizeof(detail),"%s",rdp_neg_protocol_name(answer.value));
    } else if (answer.kind == RDP_NEG_FAILURE) {
        snprintf(status,sizeof(status),"RDP server refused");
        snprintf(detail,sizeof(detail),"%s",rdp_neg_failure_name(answer.value));
    } else {
        snprintf(status,sizeof(status),"No security negotiation");
        snprintf(detail,sizeof(detail),"Legacy response");
    }
}

#ifdef RDP_FULL_CLIENT
static void full_session(const ConnectionUI *state)
{
    char message[100] = {0};
    int result;
    snprintf(status,sizeof(status),"Experimental FreeRDP...");
    snprintf(detail,sizeof(detail),"B cancels; X enters text");
    draw_status(1);
    connection_ui_draw(state);
    gfxFlushBuffers(); gfxSwapBuffers(); gspWaitForVBlank();
    result = freerdp_3ds_run(&state->current,message,sizeof(message));
    snprintf(status,sizeof(status),"%s",result == 0 ? "Session exited" : "RDP session error");
    snprintf(detail,sizeof(detail),"%s",message);
}
#endif

int main(int argc, char **argv)
{
    void *buffer;
    int netok = 0;
    (void)argc; (void)argv;
    gfxInitDefault();
    gfxSetScreenFormat(GFX_BOTTOM,GSP_BGR8);
    consoleInit(GFX_TOP,&top);
    buffer = memalign(0x1000,SOC_SIZE);
    if (buffer && R_SUCCEEDED(socInit(buffer,SOC_SIZE))) netok=1;
    connection_ui_init(&ui,HOST_FILE,PROFILES_FILE);
    if (!netok) {
        snprintf(status,sizeof(status),"Network initialization failed");
        snprintf(detail,sizeof(detail),"SOC unavailable");
    }
    while (aptMainLoop()) {
        u32 down, held, menu_keys;
        bool call_probe, call_full;
        bool touch_connect;
        char validation[96];
        hidScanInput();
        down = hidKeysDown();
        held = hidKeysHeld();
        if (down & KEY_START) break;
        if (down & KEY_TOUCH) {
            touchPosition touch;
            hidTouchRead(&touch);
            connection_ui_touch(&ui,touch.px,touch.py,PROFILES_FILE);
        }
        /* L is a diagnostic/session modifier, never an editor shortcut. */
        menu_keys = down & ~KEY_L;
        if (held & KEY_L) menu_keys &= ~(KEY_Y|KEY_SELECT);
        connection_ui_key(&ui,menu_keys,PROFILES_FILE);
        touch_connect = ui.connect_requested;
        ui.connect_requested = false;
        call_full = !!((down & KEY_R) && (held & KEY_L));
        call_probe = !!((down & KEY_R) && !(held & KEY_L));
#ifdef RDP_FULL_CLIENT
        if (touch_connect) call_full = true;
#else
        if (touch_connect) call_probe = true;
        (void)call_full;
#endif
        if (down & KEY_B) {
            char result[96];
            bool passed = horizon_sync_selftest(result,sizeof(result));
            snprintf(status,sizeof(status),"Kernel events %s",passed ? "PASS" : "FAIL");
            snprintf(detail,sizeof(detail),"%s",result);
        }
        if ((down & KEY_Y) && (held & KEY_L)) {
            char result[96] = "SOC init failed";
            bool passed = netok && loopback_pair_selftest(result,sizeof(result));
            snprintf(status,sizeof(status),"SOC loopback %s",passed ? "PASS" : "FAIL");
            snprintf(detail,sizeof(detail),"%s",result);
        }
        if ((down & KEY_SELECT) && (held & KEY_L)) {
            char result[96];
            bool passed = horizon_rng_selftest(result,sizeof(result));
            snprintf(status,sizeof(status),"PS entropy %s",passed ? "PASS" : "FAIL");
            snprintf(detail,sizeof(detail),"%s",result);
        }
        if (call_probe || call_full) {
            if (!netok || !connection_ui_ready(&ui,validation,sizeof(validation))) {
                snprintf(status,sizeof(status),"Connection not ready");
                snprintf(detail,sizeof(detail),"%s",
                         netok ? validation : "Network initialization failed");
            } else if (call_probe) {
                draw_status(netok);
                connection_ui_draw(&ui);
                gfxFlushBuffers(); gfxSwapBuffers(); gspWaitForVBlank();
                probe(&ui);
            }
#ifdef RDP_FULL_CLIENT
            else full_session(&ui);
#endif
        }
        draw_status(netok);
        connection_ui_draw(&ui);
        gfxFlushBuffers(); gfxSwapBuffers(); gspWaitForVBlank();
    }
    if (netok) socExit();
    free(buffer);
    gfxExit();
    return 0;
}
