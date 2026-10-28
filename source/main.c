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

#define HOST_FILE "sdmc:/3ds/3ds-rdp/host.txt"
#define SOC_SIZE (1024*1024)
#define MAX_CONFIG_BYTES 4096

static PrintConsole top,bottom;
static RdpConfig config;
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
    printf("Negotiation probe only.\nNo login or desktop yet.\n");
    consoleSelect(&bottom);
    printf("\x1b[2J\x1b[H A: Connect/probe\n B: Kernel event test\n Y: SOC loopback test\n X: Reload host.txt\n START: Exit\n\n");
    printf("User: %.30s\n",config.username[0]?config.username:"(not set)");
    printf("AD domain: %.27s\n",config.domain[0]?config.domain:"(not set)");
    printf("\nAuthentication not enabled.\n");
    printf("No passwords saved or sent.\n");
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
        if(keys&KEY_X) {load_host();draw();}
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
        if(keys&KEY_A) {
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
