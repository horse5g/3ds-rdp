#include <3ds.h>
#include <malloc.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "connection_ui.h"
#include "display_buffers.h"
#include "rdp_environment.h"
#include "network.h"
#include "horizon_sync.h"
#include "loopback_pair.h"
#include "horizon_rng.h"
#include "freerdp_session.h"
#include "rdp_diag_log.h"
#ifdef RDP_FULL_CLIENT
#include "cert_manager.h"
#endif

#define HOST_FILE "sdmc:/3ds/3ds-rdp/host.txt"
#define PROFILES_FILE "sdmc:/3ds/3ds-rdp/connections.txt"
#define SOC_SIZE (1024*1024)

static PrintConsole top;
#ifdef RDP_FULL_CLIENT
static PrintConsole cert_bottom;
#endif
static ConnectionUI ui;
static char status[100] = "Ready", detail[100] = "Edit a computer on the touch screen";

static void draw_status(int network_ready)
{
    char validation[96];
    bool valid = connection_ui_ready(&ui,validation,sizeof(validation));
    rdp_display_bind_console(&top, GFX_TOP);
    consoleSelect(&top);
    /* The iconic RDC computer/desktop belongs on the top LCD. Keep the
     * bottom 320x240 touchscreen available for actual form controls. The
     * character-art is original and does not embed Microsoft's logo. */
    printf("\x1b[2J\x1b[H");
    printf("\x1b[1;34m  +------------------+\x1b[0m\n");
    printf("\x1b[1;34m  |  []   []   []    |\x1b[0m  REMOTE DESKTOP\n");
    printf("\x1b[1;34m  +------------------+\x1b[0m  CONNECTION\n");
    printf("        |    |             3DS / O3DS\n");
    printf("        +----+\n");
    printf("---------------------------------------------\n");
    printf("Computer: %.38s\n", valid ? ui.current.host : "(not configured)");
    printf("User: %.34s\n",ui.current.username[0] ? ui.current.username : "(not set)");
    printf("Domain: %.30s  Port: %u\n",ui.current.domain[0] ? ui.current.domain : "(none)", (unsigned)ui.current.port);
    printf("\nSTATUS: %.36s\n",status);
    printf("%.43s\n",detail);
    printf("---------------------------------------------\n");
    if (!network_ready) printf("Network initialization failed.\n");
    else if (!valid) printf("Set a valid host before connecting.\n");
#ifdef RDP_FULL_CLIENT
    printf("R: Probe  L+R: Experimental RDP\n");
    printf("L+X: Manage trusted RDP certs\n");
#else
    printf("R: RDP negotiation probe only\n");
    printf("Full client not linked.\n");
#endif
    printf("B: Events  L+Y: SOC  L+SEL: RNG\n");
    printf("START: Exit\n\n");
    printf("Passwords are never saved to SD.\n");
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
    char message[100]={0};
    bool again;
    int result=-1;
    do{
        again=false;
        snprintf(status,sizeof(status),"Experimental FreeRDP...");
        snprintf(detail,sizeof(detail),"Authenticating; no SD password storage");
        draw_status(1);
        connection_ui_draw_status("CONNECTING TO REMOTE DESKTOP",
                                  "SIGN IN AND VERIFY CERTIFICATE",false);
        gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();
        rdp_diag_log_event("app-connect-attempt", 0, 0, 0);
        result=freerdp_3ds_run(&state->current,&state->options,state->cache_password_for_run,
                              state->gdi_bgrx_for_run,message,sizeof(message));
        rdp_diag_log_event("app-connect-return", (unsigned long)(result != 0), 0, 0);
        /* Always return to a deliberate choice after the FreeRDP context
         * and worker have been fully torn down. Reconnect never reuses an
         * old socket, password, OpenSSL context or certificate decision.
         * A/B mean retry/menu; no automatic retry of a bad certificate. */
        connection_ui_draw_status(result==0?"SESSION ENDED":"CONNECTION FAILED",
                                  message,true);
        gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();
        rdp_diag_log_event("app-session-clean",result==0,0,0);
        while(aptMainLoop()){
            u32 keys;
            hidScanInput();keys=hidKeysDown();
            if(keys&KEY_TOUCH){
                touchPosition touch;hidTouchRead(&touch);
                if(touch.py>=205){
                    if(touch.px<155)keys|=KEY_A;
                    else keys|=KEY_B;
                }
            }
            if(keys&KEY_A){
                again=true;
                rdp_diag_log_event("app-reconnect-request",result==0,0,0);
                break;
            }
            if(keys&(KEY_B|KEY_START))break;
            gspWaitForVBlank();
        }
    }while(again);
    /* Return to launcher without wiping an explicitly enabled RAM cache.
     * Otherwise the very next Connect is forced to prompt again.
     * The credential is never written to SD and remains usable only
     * for the same host/port/account, until the user turns caching off
     * or leaves the application. */
    snprintf(status,sizeof(status),"%s",result==0 ? "Session exited" : "RDP session error");
    snprintf(detail,sizeof(detail),"%s",message);
}

#endif

int main(int argc, char **argv)
{
    void *buffer;
    int netok = 0;
    bool ran_freerdp = false;
    (void)argc; (void)argv;
    gfxInitDefault();
    gfxSetScreenFormat(GFX_BOTTOM,GSP_RGB565_OES);
    consoleInit(GFX_TOP,&top);
#ifdef RDP_FULL_CLIENT
    consoleInit(GFX_BOTTOM,&cert_bottom);
#endif
    /* consoleInit disables double buffering. Re-enable both screens only
       after the last consoleInit, before the software UI starts rendering. */
    rdp_display_enable_double_buffering();
    buffer = memalign(0x1000,SOC_SIZE);
    if (buffer && R_SUCCEEDED(socInit(buffer,SOC_SIZE))) netok=1;
    /* The Save button is usable before the first RDP connection, so the
       home/profile directory must exist at startup, not only after NLA.
       Also provides WinPR its HOME path for all later FreeRDP allocations. */
    {
        char environment_error[96];
        if (rdp_environment_init("sdmc:/3ds/3ds-rdp",
                                 environment_error,sizeof(environment_error))) {
            snprintf(status,sizeof(status),"SD home initialization failed");
            snprintf(detail,sizeof(detail),"%s",environment_error);
        }
    }
    /* Logging is OFF unless an existing, writable rdplog.txt opts in. */
    rdp_diag_log_init();
    rdp_diag_log_event("app-boot", (unsigned long)netok, 0, 0);
    connection_ui_init(&ui,HOST_FILE,PROFILES_FILE);
    if (!netok) {
        snprintf(status,sizeof(status),"Network initialization failed");
        snprintf(detail,sizeof(detail),"SOC unavailable");
    }
    while (aptMainLoop()) {
        u32 down, held, menu_keys;
        bool call_probe, call_full;
        const bool was_caching=ui.cache_password_for_run;
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
        if (held & KEY_L) menu_keys &= ~(KEY_Y|KEY_SELECT|KEY_X);
        connection_ui_key(&ui,menu_keys,PROFILES_FILE);
#ifdef RDP_FULL_CLIENT
        /* Turning the checkbox OFF immediately destroys cached credentials.
         * Returning to launcher while ON intentionally keeps the password
         * for this process only; exit below always wipes it. */
        if(was_caching && !ui.cache_password_for_run)
            freerdp_3ds_clear_password_cache();
#endif
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
#ifdef RDP_FULL_CLIENT
        if ((down & KEY_X) && (held & KEY_L)) {
            char manager_result[100] = {0};
            const int rc = rdp_cert_manager_run(&top,&cert_bottom,
                                                manager_result,sizeof(manager_result));
            /* The keyboard / certificate applet may have presented its own
               buffers. Restore our double-buffered drawing contract. */
            rdp_display_enable_double_buffering();
            snprintf(status,sizeof(status),"%s",rc == 0 ?
                     "Trusted certificates" : "Certificate manager error");
            snprintf(detail,sizeof(detail),"%s",manager_result);
        }
#endif
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
            else {
                /* A run of FreeRDP currently leaves SOCU_Shutdown blocked on
                 * physical O3DS after all context cleanup finished. */
                ran_freerdp = true;
                full_session(&ui);
            }
#endif
        }
        draw_status(netok);
        connection_ui_draw(&ui);
        gfxFlushBuffers(); gfxSwapBuffers(); gspWaitForVBlank();
    }
    /* HOME/START can reach this code while remaining SOC sockets still
     * exist. Distinguish the final app frame from SOC/GFX shutdown hangs. */
#ifdef RDP_FULL_CLIENT
    freerdp_3ds_clear_password_cache();
#endif
    rdp_diag_log_event("app-exit", 0, 0, 0);
    if (netok && ran_freerdp) {
        /* O3DS recovery workaround: after a FreeRDP attempt, socExit()
         * reproducibly never returns from SOCU_Shutdown, freezing HOME.
         * Defer the SOC service/IPC handle and shared-memory cleanup to
         * process termination. Do not free the context buffer while its
         * kernel memory block may still reference it. This is intentionally
         * scoped to actual FreeRDP attempts; ordinary probe-only exits
         * preserve the normal socExit() path. */
        rdp_diag_log_event("app-soc-exit-deferred", 1, 0, 0);
    } else {
        rdp_diag_log_event("app-soc-exit-enter", (unsigned long)netok, 0, 0);
        if (netok) {
            const Result soc_result = socExit();
            rdp_diag_log_event("app-soc-exit-done", (unsigned long)soc_result, 0, 0);
        }
        rdp_diag_log_event("app-soc-buffer-free", 0, 0, 0);
        free(buffer);
    }
    rdp_diag_log_event("app-gfx-exit-enter", 0, 0, 0);
    gfxExit();
    rdp_diag_log_event("app-gfx-exit-done", 0, 0, 0);
    return 0;
}
