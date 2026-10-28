#include <3ds.h>
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "network.h"
#include "horizon_sync.h"
#include "loopback_pair.h"
#include "horizon_rng.h"
#include "freerdp_session.h"
#include "connection_ui.h"

#define HOST_FILE "sdmc:/3ds/3ds-rdp/host.txt"
#define PROFILES_FILE "sdmc:/3ds/3ds-rdp/connections.txt"
#define SOC_SIZE (1024*1024)
#define MAX_CONFIG_BYTES 4096

static PrintConsole top,bottom;
static RdpConfig config;
static ConnectionUI ui;
static char status[100]="Ready",detail[100]="Press A to probe";
static int configured=0;

static void load_host(void) {
    char contents[MAX_CONFIG_BYTES+1],problem[96];
    size_t bytes;
    FILE *file=fopen(HOST_FILE,"rb");
    configured=0;
    memset(&config,0,sizeof(config));
    if(!file) {
        snprintf(problem,sizeof(problem),"Missing host.txt on SD card");
    } else {
        bytes=fread(contents,1,sizeof(contents),file);
        if(ferror(file)) {
            snprintf(problem,sizeof(problem),"Unable to read host.txt");
        } else if(bytes>=sizeof(contents)) {
            snprintf(problem,sizeof(problem),"host.txt too large");
        } else if(memchr(contents,0,bytes)) {
            snprintf(problem,sizeof(problem),"host.txt contains NUL byte");
        } else {
            contents[bytes]=0;
            configured=(rdp_config_parse(contents,&config,problem,sizeof(problem))==0);
        }
        fclose(file);
    }
    if(!configured) {
        snprintf(status,sizeof(status),"Configuration error");
        snprintf(detail,sizeof(detail),"%s",problem);
    } else {
        snprintf(status,sizeof(status),"Config loaded");
        snprintf(detail,sizeof(detail),"Press A to probe RDP");
    }
}

static void draw(void) {
    consoleSelect(&top);
    printf("\x1b[2J\x1b[H3DS RDP - O3DS prototype\n===========================\n\n");
    printf("Host: %.38s\nPort: %u\n\n",configured?config.host:"NOT CONFIGURED",
           (unsigned)(configured?config.port:3389));
    printf("Status: %.38s\nDetail: %.38s\n\n",status,detail);
#ifdef RDP_FULL_CLIENT
    printf("R: Experimental full FreeRDP session\n");
#else
    printf("Negotiation probe only.\nNo login or desktop yet.\n");
#endif
    consoleSelect(&bottom);
    connection_ui_draw(&ui);

}

int main(int argc,char **argv) {
    void *buffer;
    int netok=0;
    (void)argc;(void)argv;
    gfxInitDefault();
    consoleInit(GFX_TOP,&top);
    consoleInit(GFX_BOTTOM,&bottom);
    buffer=memalign(0x1000,SOC_SIZE);
    if(buffer && R_SUCCEEDED(socInit(buffer,SOC_SIZE))) netok=1;
    load_host();
    connection_ui_init(&ui,HOST_FILE,PROFILES_FILE);
    config=ui.current;
    { char validation[96]; configured=connection_ui_ready(&ui,validation,sizeof(validation)); }
    if(configured) {
        snprintf(status,sizeof(status),"Connection settings ready");
        snprintf(detail,sizeof(detail),"Press R to probe; L+R for full RDP");
    }
    if(!netok) {
        snprintf(status,sizeof(status),"Network initialization failed");
        snprintf(detail,sizeof(detail),"socInit failed");
    }
    draw();
    while(aptMainLoop()) {
        u32 keys;
        hidScanInput();
        keys=hidKeysDown();
        if(keys&KEY_START) break;
        if(keys&KEY_TOUCH) {
            touchPosition touch;
            hidTouchRead(&touch);
            connection_ui_touch(&ui,touch.px,touch.py,PROFILES_FILE);
        }
        connection_ui_key(&ui,keys,PROFILES_FILE);
        config=ui.current;
        { char validation[96]; configured=connection_ui_ready(&ui,validation,sizeof(validation)); }
        if(keys&(KEY_X|KEY_L|KEY_Y|KEY_DUP|KEY_DDOWN|KEY_TOUCH|KEY_SELECT|KEY_A)) draw();
        if(keys&KEY_B) {
            char result[96];
            bool passed = horizon_sync_selftest(result, sizeof(result));
            snprintf(status, sizeof(status), "%s", passed ? "Kernel events PASS" : "Kernel events FAIL");
            snprintf(detail, sizeof(detail), "%s", result);
            draw();
        }
        if(keys&KEY_Y) {
            char result[96];
            bool passed = netok && loopback_pair_selftest(result, sizeof(result));
            snprintf(status, sizeof(status), "%s", passed ? "SOC wakeup PASS" : "SOC wakeup FAIL");
            snprintf(detail, sizeof(detail), "%s", netok ? result : "SOC initialization failed");
            draw();
        }
        if(keys&KEY_SELECT) {
            char result[96];
            bool passed = horizon_rng_selftest(result, sizeof(result));
            snprintf(status, sizeof(status), "%s", passed ? "PS service PASS" : "PS service FAIL");
            snprintf(detail, sizeof(detail), "%s", result);
            draw();
        }
#ifdef RDP_FULL_CLIENT
        if((keys&KEY_R) && (hidKeysHeld()&KEY_L)) {
            char session_result[100] = {0};
            if (!netok || !configured) {
                snprintf(status,sizeof(status),"Not ready");
                snprintf(detail,sizeof(detail),"Check network and host.txt");
            } else {
                snprintf(status,sizeof(status),"Experimental FreeRDP...");
                snprintf(detail,sizeof(detail),"B: Cancel  X: Type  A: Click");
                draw();
                gfxFlushBuffers(); gfxSwapBuffers(); gspWaitForVBlank();
                const int rc=freerdp_3ds_run(&config,session_result,sizeof(session_result));
                snprintf(status,sizeof(status),"%s",rc == 0 ? "RDP session exited" : "RDP session error");
                snprintf(detail,sizeof(detail),"%s",session_result);
            }
            draw();
        }
#endif
        if((keys&KEY_R) && !(hidKeysHeld()&KEY_L)) {
            if(!netok || !configured) {
                snprintf(status,sizeof(status),"Not ready");
                snprintf(detail,sizeof(detail),"Check network and host.txt");
            } else {
                RdpNegResult r;
                char err[96];
                snprintf(status,sizeof(status),"Connecting...");
                draw();
                gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();
                if(network_rdp_probe(config.host,config.port,&r,err,sizeof(err))) {
                    snprintf(status,sizeof(status),"Probe failed");
                    snprintf(detail,sizeof(detail),"%s",err);
                } else if(r.kind==RDP_NEG_SELECTED) {
                    snprintf(status,sizeof(status),"RDP negotiated");
                    snprintf(detail,sizeof(detail),"%s",rdp_neg_protocol_name(r.value));
                } else if(r.kind==RDP_NEG_FAILURE) {
                    snprintf(status,sizeof(status),"RDP refused");
                    snprintf(detail,sizeof(detail),"%s",rdp_neg_failure_name(r.value));
                } else {
                    snprintf(status,sizeof(status),"No security negotiation");
                    snprintf(detail,sizeof(detail),"Legacy response");
                }
            }
            draw();
        }
        gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();
    }
    if(netok) socExit();
    free(buffer);
    gfxExit();
    return 0;
}
