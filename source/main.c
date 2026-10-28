#include <3ds.h>
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "network.h"
#define HOST_FILE "sdmc:/3ds/3ds-rdp/host.txt"
#define SOC_SIZE (1024*1024)
static PrintConsole top,bottom;
static char host[64],status[100]="Ready",detail[100]="Press A to probe";
static uint16_t port=3389;
static int configured=0;
static void load_host(void) {
    FILE *f=fopen(HOST_FILE,"r"); char line[128],*colon,*p,*end; unsigned long v;
    configured=0;host[0]=0;port=3389;
    if(!f) return;
    while(fgets(line,sizeof(line),f)) {
        p=line;while(*p==' '||*p=='\t')p++;
        if(!*p||*p=='#'||*p=='\n')continue;
        end=p+strlen(p);
        while(end>p&&(end[-1]=='\n'||end[-1]=='\r'||end[-1]==' '))*--end=0;
        colon=strrchr(p,':');
        if(colon) { char *stop;*colon++=0;v=strtoul(colon,&stop,10);
            if(!*colon||*stop||v<1||v>65535)break;port=(uint16_t)v; }
        if(strlen(p)==0||strlen(p)>=sizeof(host))break;
        strcpy(host,p);configured=1;break;
    }
    fclose(f);
}
static void draw(void) {
    consoleSelect(&top);
    printf("\x1b[2J\x1b[H3DS RDP - O3DS prototype\n===========================\n\n");
    printf("Host: %s\nPort: %u\n\n",configured?host:"NOT CONFIGURED",(unsigned)port);
    printf("Status: %s\nDetail: %s\n\n",status,detail);
    printf("Negotiation probe only!\nNo login or desktop yet.\n");
    consoleSelect(&bottom);
    printf("\x1b[2J\x1b[H A: Connect/probe\n X: Reload host.txt\n START: Exit\n\n");
    printf("SD: /3ds/3ds-rdp/host.txt\n192.168.1.100:3389\n\nNo passwords saved.\n");
}
int main(int argc,char **argv) {
    void *buffer; int netok=0;
    (void)argc;(void)argv;
    gfxInitDefault();consoleInit(GFX_TOP,&top);consoleInit(GFX_BOTTOM,&bottom);
    buffer=memalign(0x1000,SOC_SIZE);
    if(buffer&&R_SUCCEEDED(socInit(buffer,SOC_SIZE)))netok=1;
    else snprintf(status,sizeof(status),"socInit failed");
    load_host();draw();
    while(aptMainLoop()) {
        u32 keys;hidScanInput();keys=hidKeysDown();
        if(keys&KEY_START)break;
        if(keys&KEY_X){load_host();snprintf(status,sizeof(status),"Config reloaded");draw();}
        if(keys&KEY_A) {
            if(!netok||!configured) {
                snprintf(status,sizeof(status),"Not ready");
                snprintf(detail,sizeof(detail),"Check network and host.txt");
            } else {
                RdpNegResult r;char err[96];
                snprintf(status,sizeof(status),"Connecting...");draw();
                gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();
                if(network_rdp_probe(host,port,&r,err,sizeof(err))) {
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
    if(netok)socExit();free(buffer);gfxExit();return 0;
}
