#include "cert_manager.h"
#ifdef RDP_FULL_CLIENT
#include "cert_store.h"
#include "connection_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRUST_FILE "sdmc:/3ds/3ds-rdp/trusted-certs.txt"
#define PAGE_ROWS 7

static void show_fingerprint(const unsigned char sha[32])
{
    static const char hex[] = "0123456789ABCDEF";
    for (int i = 0; i < 32; ++i) {
        putchar(hex[sha[i] >> 4]);
        putchar(hex[sha[i] & 15]);
        if (i == 15)
            putchar('\n');
    }
    putchar('\n');
}

static void draw_manager(PrintConsole *top, PrintConsole *bottom,
                         const RdpCertPool *pool, size_t selected,
                         unsigned screen, const char *notice)
{
    consoleSelect(top);
    printf("\x1b[2J\x1b[HTrusted RDP Certificates\n");
    printf("========================\n\n");
    if (screen == 0) {
        printf("Saved endpoints: %u / %u\n\n",
               (unsigned)pool->count, (unsigned)RDP_CERT_POOL_MAX);
        if (!pool->count)
            puts("(No saved certificates)");
        else {
            const size_t start = (selected / PAGE_ROWS) * PAGE_ROWS;
            for (size_t i = start; i < pool->count && i < start + PAGE_ROWS; ++i) {
                const RdpCertPin *p = &pool->entries[i];
                printf("%c %2u %.31s\n", i == selected ? '>' : ' ',
                       (unsigned)(i + 1), p->host);
                printf("     port %u\n", (unsigned)p->port);
            }
        }
    } else {
        const RdpCertPin *p = &pool->entries[selected];
        printf("%s\nPort: %u\n\n", p->host, (unsigned)p->port);
        puts("Trusted leaf SHA-256:");
        show_fingerprint(p->sha256);
        if (screen == 2) {
            puts("\nREMOVE THIS TRUST ENTRY?");
            puts("Next connection will be untrusted.");
            puts("Verify any replacement independently.");
        }
    }

    (void)bottom;
    {
        char info[80],title[64];
        if(screen==0){
            snprintf(title,sizeof(title),"SAVED CERTIFICATES");
            snprintf(info,sizeof(info),"%u TRUSTED ENDPOINTS",
                     (unsigned)pool->count);
        }else {
            const RdpCertPin *p=&pool->entries[selected];
            snprintf(title,sizeof(title),"CERTIFICATE DETAILS");
            snprintf(info,sizeof(info),"%.40s PORT %u",p->host,(unsigned)p->port);
        }
        connection_ui_draw_cert_manager(title,info,
             (screen==0 ? "UP/DOWN: SELECT CERT" : notice),
             screen==2);
    }
}

int rdp_cert_manager_run(PrintConsole *top, PrintConsole *bottom,
                         char *message, size_t message_size)
{
    RdpCertPool *pool = NULL;
    size_t selected = 0;
    unsigned screen = 0;
    char notice[64] = {0};
    int result = -1;

    if (!top || !bottom || !message || !message_size)
        return -1;
    snprintf(message, message_size, "Trusted certs unavailable");
    pool = (RdpCertPool *)malloc(sizeof(*pool));
    if (!pool)
        goto finish;
    if (rdp_cert_store_load(TRUST_FILE, pool) != 0) {
        snprintf(message, message_size,
                 "Trust file invalid; inspect SD .bak/.tmp");
        goto finish;
    }

    result = 0;
    draw_manager(top, bottom, pool, selected, screen, notice);
    gfxFlushBuffers();
    gfxSwapBuffers();

    while (aptMainLoop()) {
        hidScanInput();
        u32 keys = hidKeysDown();
        if(keys & KEY_TOUCH){
            touchPosition touch;
            hidTouchRead(&touch);
            if(touch.py>=205){
                if(touch.px<108) keys|=KEY_A;
                else if(touch.px<213)keys|=KEY_X;
                else keys|=KEY_B;
            }
        }
        bool redraw = false;

        if (keys & KEY_START)
            break;
        if (keys & KEY_B) {
            if (screen == 0)
                break;
            screen = (screen == 2) ? 1 : 0;
            notice[0] = 0;
            redraw = true;
        } else if (screen == 0) {
            if ((keys & KEY_DUP) && selected > 0) {
                selected--;
                redraw = true;
            }
            if ((keys & KEY_DDOWN) && selected + 1 < pool->count) {
                selected++;
                redraw = true;
            }
            if ((keys & KEY_A) && pool->count) {
                screen = 1;
                notice[0] = 0;
                redraw = true;
            }
            if (keys & KEY_Y) {
                if (rdp_cert_store_load(TRUST_FILE, pool) != 0) {
                    snprintf(message, message_size,
                             "Trust file invalid; inspect SD .bak/.tmp");
                    result = -1;
                    break;
                }
                if (selected >= pool->count)
                    selected = pool->count ? pool->count - 1 : 0;
                snprintf(notice, sizeof(notice), "Trust pool reloaded");
                redraw = true;
            }
            if ((keys & KEY_X) && pool->count) {
                screen=2;redraw=true;
            }
        } else if (screen == 1) {
            if (keys & KEY_X) {
                screen = 2;
                redraw = true;
            }
        } else if (screen == 2 && (keys & KEY_A)) {
            /* Revalidate selected entry from disk at write time; a changed
             * or corrupt file is never silently replaced by the UI. */
            const RdpCertPin old = pool->entries[selected];
            if (rdp_cert_store_remove(TRUST_FILE, selected, &old) != 0 ||
                rdp_cert_store_load(TRUST_FILE, pool) != 0) {
                snprintf(message, message_size,
                         "Trust removal failed; inspect SD");
                result = -1;
                break;
            }
            if (selected >= pool->count)
                selected = pool->count ? pool->count - 1 : 0;
            screen = 0;
            snprintf(notice, sizeof(notice), "Entry removed. Verify any new cert.");
            redraw = true;
        }

        if (redraw) {
            draw_manager(top, bottom, pool, selected, screen, notice);
            gfxFlushBuffers();
            gfxSwapBuffers();
        }
        gspWaitForVBlank();
    }
    if (result == 0)
        snprintf(message, message_size, "Certificate manager exited");

finish:
    free(pool);
    return result;
}

#endif /* RDP_FULL_CLIENT */
