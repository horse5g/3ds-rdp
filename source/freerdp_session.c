#include "freerdp_session.h"
#include "cert_pin.h"
#include "cert_pool.h"
#include "cert_store.h"
#include "session_pad.h"
#include "session_zoom.h"
#include "rdp_pixels.h"
#include <stdio.h>

#ifndef RDP_FULL_CLIENT
int freerdp_3ds_run(const RdpConfig *cfg, char *reason, size_t reason_size)
{
    (void)cfg;
    if (reason && reason_size)
        snprintf(reason, reason_size, "FreeRDP not linked in this build");
    return -1;
}
#else

/* Experimental full client. Build/link first; no runtime behavior has been
 * validated on O3DS. Unknown certificates require explicit approval;
 * changed certificates are always rejected until manually removed. */
#include <3ds.h>
#include <freerdp/freerdp.h>
#include <freerdp/settings.h>
#include <freerdp/gdi/gdi.h>
#include <freerdp/update.h>
#include <freerdp/input.h>
#include <winpr/synch.h>
#include <openssl/crypto.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/evp.h>
#include <arpa/inet.h>
#include <limits.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define RDP_W 400
#define RDP_H 240
#define RDP_MAX_EVENTS 64

#define PIN_FILE "sdmc:/3ds/3ds-rdp/cert-pin.txt"
#define PIN_MAX_BYTES 1024
#define POOL_FILE "sdmc:/3ds/3ds-rdp/trusted-certs.txt"
#define POOL_MAX_BYTES (RDP_CERT_POOL_MAX * RDP_CERT_POOL_LINE_MAX + 1024)

/* Single-session homebrew app: no concurrent RDP sessions. */
static RdpCertPin active_pin;
static RdpCertPool active_pool;
static bool have_manual_pin;
static char requested_host[RDP_HOST_CAP];
static uint16_t requested_port;
static PrintConsole trust_console;
static char cert_failure[100];
static bool desktop_dirty;

/* 0 = optional pin absent, 1 = loaded, -1 = malformed or I/O error */
static int load_cert_pin(RdpCertPin *pin)
{
    char text[PIN_MAX_BYTES + 1];
    size_t n;
    FILE *f = fopen(PIN_FILE, "rb");
    if (!f)
        return errno == ENOENT ? 0 : -1;
    n = fread(text, 1, sizeof(text), f);
    const bool read_error = ferror(f) != 0;
    const int closed = fclose(f);
    if (read_error || closed != 0 || n >= sizeof(text) || memchr(text, 0, n))
        return -1;
    text[n] = '\0';
    return rdp_cert_pin_parse(text, pin) == 0 ? 1 : -1;
}

static bool load_cert_pool(RdpCertPool *pool)
{
    /* Also refuse an interrupted Trusted Certificates manager rewrite.
     * An unresolved .bak or .tmp is never mistaken for an empty trust pool. */
    return rdp_cert_store_load(POOL_FILE, pool) == 0;
}

/* Read back every entry after saving; no trust if the write fails.
 * The SD filesystem does not guarantee append durability on power loss.
 * A partially written line will fail the strict parser on next startup. */
static bool save_cert_to_pool(const RdpCertPin *pin)
{
    RdpCertPool *disk = (RdpCertPool *)malloc(sizeof(RdpCertPool));
    RdpCertPool *verified = (RdpCertPool *)malloc(sizeof(RdpCertPool));
    char record[RDP_CERT_POOL_LINE_MAX];
    bool ok = false;
    FILE *f;
    int n;
    if (!disk || !verified)
        goto finish;
    if (!load_cert_pool(disk) || disk->count != active_pool.count)
        goto finish;
    for (size_t i = 0; i < disk->count; ++i) {
        const RdpCertPin *e = &active_pool.entries[i];
        if (rdp_cert_pool_lookup(disk, e->host, e->port, e->sha256) != 1)
            goto finish;
    }
    if (rdp_cert_pool_add(disk, pin) != 1)
        goto finish;
    n = rdp_cert_pool_format(pin, record, sizeof(record));
    if (n <= 0)
        goto finish;
    f = fopen(POOL_FILE, "ab");
    if (!f)
        goto finish;
    const bool written = fwrite(record, 1, (size_t)n, f) == (size_t)n &&
                         fflush(f) == 0;
    const int closed = fclose(f);
    if (!written || closed != 0 || !load_cert_pool(verified) ||
        verified->count != disk->count)
        goto finish;
    for (size_t i = 0; i < disk->count; ++i) {
        const RdpCertPin *e = &disk->entries[i];
        if (rdp_cert_pool_lookup(verified, e->host, e->port, e->sha256) != 1)
            goto finish;
    }
    active_pool = *verified;
    ok = true;
finish:
    free(disk);
    free(verified);
    return ok;
}

/* No automatic first-use trust: the user must see the complete SHA-256 and
 * expressly choose a single session or append to the trusted endpoint pool. */
static int ask_unknown_certificate(const char *host, uint16_t port,
                                   const unsigned char sha256[32])
{
    static const char hex[] = "0123456789ABCDEF";
    char fingerprint[65];
    for (size_t i = 0; i < 32; i++) {
        fingerprint[i * 2] = hex[sha256[i] >> 4];
        fingerprint[i * 2 + 1] = hex[sha256[i] & 15];
    }
    fingerprint[64] = 0;
    consoleInit(GFX_TOP, &trust_console);
    consoleSelect(&trust_console);
    printf("\x1b[2J\x1b[HUNKNOWN RDP CERTIFICATE\n");
    printf("This server is NOT yet trusted.\n\n");
    printf("Host: %.40s\nPort: %u\n\n", host, (unsigned)port);
    printf("Leaf SHA-256 (check independently):\n");
    printf("%.32s\n%s\n\n", fingerprint, fingerprint + 32);
    printf("First-use trust can allow MITM attacks!\n");
    printf("Verify this hash on the Windows host.\n\n");
    printf("B / START: Reject\n");
    printf("A: Accept for this session ONLY\n");
    printf("X: Trust and SAVE to SD card\n");
    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();
    while (aptMainLoop()) {
        hidScanInput();
        u32 keys = hidKeysDown();
        if (keys & (KEY_B | KEY_START))
            return 0;
        if (keys & KEY_A)
            return 1;
        if (keys & KEY_X)
            return 2;
        gspWaitForVBlank();
    }
    return 0;
}

/* FreeRDP ExternalCertificateManagement callback. Requires a valid name and
 * date BEFORE any pool decision, then enforces a manual pin when configured.
 * For unknown endpoints only, permit an explicit, visible trust decision.
 * Returning 2 accepts just this session in FreeRDP's certificate code. */
static int verify_x509_cert(freerdp *instance, const BYTE *pem, size_t length,
                            const char *host, UINT16 port, DWORD flags)
{
    BIO *bio = NULL;
    X509 *cert = NULL;
    unsigned char digest[EVP_MAX_MD_SIZE] = {0};
    unsigned int digest_length = 0;
    struct in_addr ipv4;
    time_t now;
    int result = 0;
    (void)instance;

    snprintf(cert_failure, sizeof(cert_failure), "RDP certificate rejected");
    if (!pem || !length || length > INT_MAX || !host ||
        strcmp(host, requested_host) != 0 || port != requested_port ||
        flags != VERIFY_CERT_FLAG_NONE)
        return 0;

    bio = BIO_new_mem_buf(pem, (int)length);
    if (!bio)
        return 0;
    cert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
    if (!cert)
        goto finish;

    now = time(NULL);
    if (now < (time_t)1577836800) {
        snprintf(cert_failure, sizeof(cert_failure), "Set the 3DS date/time first");
        goto finish;
    }
    if (X509_cmp_time(X509_get0_notBefore(cert), &now) >= 0 ||
        X509_cmp_time(X509_get0_notAfter(cert), &now) <= 0) {
        snprintf(cert_failure, sizeof(cert_failure), "RDP certificate expired/not yet valid");
        goto finish;
    }
    if (inet_pton(AF_INET, host, &ipv4) == 1) {
        if (X509_check_ip_asc(cert, host, 0) != 1) {
            snprintf(cert_failure, sizeof(cert_failure), "RDP certificate IP name mismatch");
            goto finish;
        }
    } else if (X509_check_host(cert, host, 0,
                               X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS, NULL) != 1) {
        snprintf(cert_failure, sizeof(cert_failure), "RDP certificate DNS name mismatch");
        goto finish;
    }

    if (X509_digest(cert, EVP_sha256(), digest, &digest_length) != 1 ||
        digest_length != 32)
        goto finish;

    if (have_manual_pin && rdp_cert_pin_matches(&active_pin, host, port)) {
        if (CRYPTO_memcmp(digest, active_pin.sha256, 32) != 0) {
            snprintf(cert_failure, sizeof(cert_failure), "Manual certificate pin changed; blocked");
            goto finish;
        }
        result = 2;
    } else {
        const int known = rdp_cert_pool_lookup(&active_pool, host, port, digest);
        if (known < 0) {
            snprintf(cert_failure, sizeof(cert_failure), "Saved certificate changed; blocked");
            goto finish;
        }
        if (known > 0) {
            result = 2;
        } else {
            const int approval = ask_unknown_certificate(host, port, digest);
            if (approval == 0) {
                snprintf(cert_failure, sizeof(cert_failure), "Unknown RDP certificate rejected");
                goto finish;
            }
            if (approval == 2) {
                RdpCertPin pin = {0};
                memcpy(pin.host, host, strlen(host) + 1);
                pin.port = port;
                memcpy(pin.sha256, digest, 32);
                if (!save_cert_to_pool(&pin)) {
                    snprintf(cert_failure, sizeof(cert_failure), "Could not save certificate trust pool");
                    goto finish;
                }
            }
            result = 2;
        }
    }
    cert_failure[0] = '\0';
finish:
    OPENSSL_cleanse(digest, sizeof(digest));
    X509_free(cert);
    BIO_free(bio);
    return result;
}

static DWORD deny_unknown_certificate(freerdp *instance, const char *host, UINT16 port,
                                      const char *common_name, const char *subject,
                                      const char *issuer, const char *fingerprint, DWORD flags)
{
    (void)instance; (void)host; (void)port; (void)common_name;
    (void)subject; (void)issuer; (void)fingerprint; (void)flags;
    /* A trust store / verified pin has not been configured yet. */
    return 0;
}

static BOOL begin_paint(rdpContext *context)
{
    if (context && context->gdi && context->gdi->primary &&
        context->gdi->primary->hdc && context->gdi->primary->hdc->hwnd &&
        context->gdi->primary->hdc->hwnd->invalid)
        context->gdi->primary->hdc->hwnd->invalid->null = TRUE;
    return TRUE;
}

static BOOL end_paint(rdpContext *context)
{
    if (!context || !context->gdi)
        return FALSE;
    desktop_dirty = true;
    return TRUE;
}

/* Full GDI surface copy only when a paint has occurred. The 3DS uses two
 * framebuffers; swapping on idle iterations otherwise exposes stale frames. */
static BOOL copy_desktop_frame(rdpContext *context, const SessionZoom *zoom,
                               int pointer_x, int pointer_y)
{
    rdpGdi *gdi;
    u16 fb_width=0, fb_height=0;
    uint16_t *frame;
    SessionZoomView view;
    int x,y;
    int sample_y[RDP_H];
    int32_t step_x,step_y;

    if (!context || !(gdi=context->gdi) || !gdi->primary_buffer ||
        gdi->width < RDP_W || gdi->height < RDP_H ||
        gdi->stride < (size_t)gdi->width * 4 ||
        gfxGetScreenFormat(GFX_TOP) != GSP_RGB565_OES)
        return FALSE;

    /* consoleInit(GFX_TOP, ...) sets RGB565 and single buffering.
       Match that real format rather than rejecting every first paint. */
    frame=(uint16_t *)gfxGetFramebuffer(GFX_TOP,GFX_LEFT,&fb_width,&fb_height);
    if (!frame || fb_width < RDP_H || fb_height < RDP_W) return FALSE;

    view=session_zoom_view(zoom,pointer_x,pointer_y,RDP_W,RDP_H);
    step_x=view.width_q16/RDP_W;
    step_y=view.height_q16/RDP_H;
    /* Cache vertical source row lookup once per frame; on an O3DS this
       avoids 400x repeating the expensive crop and bound arithmetic. */
    for (y=0;y<RDP_H;++y) {
        int sy=(int)((view.top_q16+(int64_t)y*step_y+32768)>>16);
        if (sy < 0) sy=0;
        if (sy >= gdi->height) sy=gdi->height-1;
        sample_y[y]=sy;
    }
    for (x=0;x<RDP_W;++x) {
        int sx=(int)((view.left_q16+(int64_t)x*step_x+32768)>>16);
        if (sx < 0) sx=0;
        if (sx >= gdi->width) sx=gdi->width-1;
        for (y=0;y<RDP_H;++y) {
            const u8 *src=gdi->primary_buffer+
                     (size_t)sample_y[y]*gdi->stride+(size_t)sx*4;
            frame[(size_t)x*RDP_H+(RDP_H-1-y)]=rdp_bgrx_to_rgb565(src);
        }
    }
    return TRUE;
}

static BOOL desktop_resize(rdpContext *context)
{
    return context && context->gdi &&
           gdi_resize(context->gdi,
                      freerdp_settings_get_uint32(context->settings, FreeRDP_DesktopWidth),
                      freerdp_settings_get_uint32(context->settings, FreeRDP_DesktopHeight));
}

static BOOL post_connect(freerdp *instance)
{
    rdpContext *ctx;
    if (!instance || !gdi_init(instance, PIXEL_FORMAT_XRGB32))
        return FALSE;
    ctx = instance->context;
    if (!ctx || !ctx->update)
        return FALSE;
    ctx->update->BeginPaint = begin_paint;
    ctx->update->EndPaint = end_paint;
    ctx->update->DesktopResize = desktop_resize;
    return TRUE;
}

static void post_disconnect(freerdp *instance)
{
    if (instance && instance->context && instance->context->gdi)
        gdi_free(instance);
}

static bool prompt_password(char *password, size_t size)
{
    SwkbdState keyboard;
    SwkbdButton button;
    swkbdInit(&keyboard, SWKBD_TYPE_NORMAL, 2, (int)size - 1);
    swkbdSetHintText(&keyboard, "RDP password (not saved)");
    swkbdSetPasswordMode(&keyboard, SWKBD_PASSWORD_HIDE);
    button = swkbdInputText(&keyboard, password, size);
    OPENSSL_cleanse(&keyboard, sizeof(keyboard));
    return button == SWKBD_BUTTON_CONFIRM;
}

static void send_text(rdpInput *input)
{
    char text[128] = {0};
    SwkbdState keyboard;
    size_t i;
    swkbdInit(&keyboard, SWKBD_TYPE_NORMAL, 2, sizeof(text) - 1);
    swkbdSetHintText(&keyboard, "Remote text (ASCII only)");
    if (swkbdInputText(&keyboard, text, sizeof(text)) == SWKBD_BUTTON_CONFIRM) {
        for (i = 0; text[i]; ++i) {
            /* UTF-8 multi-byte input is not supported by this simple path. */
            const unsigned char ch = (unsigned char)text[i];
            if (ch < 0x20 || ch > 0x7e)
                continue;
            (void)freerdp_input_send_unicode_keyboard_event(input, 0, ch);
            (void)freerdp_input_send_unicode_keyboard_event(input, KBD_FLAGS_RELEASE, ch);
        }
    }
    OPENSSL_cleanse(text, sizeof(text));
}

int freerdp_3ds_run(const RdpConfig *cfg, char *reason, size_t reason_size)
{
    freerdp *client = NULL;
    rdpSettings *settings;
    char password[128] = {0};
    bool initialized = false;
    bool connected = false;
    bool left_down = false;
    bool right_down = false;
    bool pad_redraw = true;
    SessionPad pad;
    SessionZoom zoom;
    int result = -1;

    if (!cfg || !cfg->host[0] || !reason || !reason_size)
        return -1;
    snprintf(reason, reason_size, "FreeRDP startup failed");

    /* Load both trust sources before prompting for credentials. Missing files
       are allowed (new server); malformed or unreadable files fail closed. */
    memset(&active_pin, 0, sizeof(active_pin));
    memset(&active_pool, 0, sizeof(active_pool));
    cert_failure[0] = '\0';
    memset(requested_host, 0, sizeof(requested_host));
    memcpy(requested_host, cfg->host, strlen(cfg->host) + 1);
    requested_port = cfg->port;
    const int pin_state = load_cert_pin(&active_pin);
    if (pin_state < 0 || !load_cert_pool(&active_pool)) {
        snprintf(reason, reason_size, "Invalid or unreadable RDP certificate trust file");
        goto done;
    }
    have_manual_pin = (pin_state > 0);

    /* Password is prompted for at runtime; never accepted from host.txt. */
    if (!prompt_password(password, sizeof(password))) {
        snprintf(reason, reason_size, "Password input cancelled");
        goto done;
    }

    client = freerdp_new();
    if (!client) {
        snprintf(reason, reason_size, "freerdp_new failed");
        goto done;
    }
    client->VerifyCertificateEx = deny_unknown_certificate;
    client->VerifyX509Certificate = verify_x509_cert;
    client->PostConnect = post_connect;
    client->PostDisconnect = post_disconnect;
    if (!freerdp_context_new(client)) {
        snprintf(reason, reason_size, "FreeRDP context allocation failed");
        goto done;
    }
    initialized = true;
    settings = client->context->settings;
    if (!freerdp_settings_set_string(settings, FreeRDP_ServerHostname, cfg->host) ||
        !freerdp_settings_set_uint32(settings, FreeRDP_ServerPort, cfg->port) ||
        !freerdp_settings_set_uint32(settings, FreeRDP_DesktopWidth, RDP_W) ||
        !freerdp_settings_set_uint32(settings, FreeRDP_DesktopHeight, RDP_H) ||
        !freerdp_settings_set_uint32(settings, FreeRDP_ColorDepth, 32) ||
        /* Only NLA/CredSSP (including hybrid extended). No legacy RDP
           Security or TLS-without-NLA credential fallback. */
        /* Horizon SOC is IPv4-only and does not support SO_KEEPALIVE;
           FreeRDP otherwise defaults TCP keepalive to enabled. */
        !freerdp_settings_set_uint32(settings, FreeRDP_ForceIPvX, 4) ||
        !freerdp_settings_set_bool(settings, FreeRDP_TcpKeepAlive, FALSE) ||
        !freerdp_settings_set_bool(settings, FreeRDP_ExternalCertificateManagement, TRUE) ||
        !freerdp_settings_set_bool(settings, FreeRDP_IgnoreCertificate, FALSE) ||
        !freerdp_settings_set_bool(settings, FreeRDP_AutoAcceptCertificate, FALSE) ||
        !freerdp_settings_set_uint32(settings, FreeRDP_AuthenticationLevel, 2) ||
        !freerdp_settings_set_bool(settings, FreeRDP_NlaSecurity, TRUE) ||
        !freerdp_settings_set_bool(settings, FreeRDP_ExtSecurity, TRUE) ||
        !freerdp_settings_set_bool(settings, FreeRDP_RdpSecurity, FALSE) ||
        !freerdp_settings_set_bool(settings, FreeRDP_TlsSecurity, FALSE) ||
        !freerdp_settings_set_string(settings, FreeRDP_Username, cfg->username) ||
        !freerdp_settings_set_string(settings, FreeRDP_Domain, cfg->domain) ||
        !freerdp_settings_set_string(settings, FreeRDP_Password, password)) {
        snprintf(reason, reason_size, "FreeRDP settings rejected");
        goto done;
    }
    OPENSSL_cleanse(password, sizeof(password));

    /* Security policy is not weakened. Without an explicitly trusted
       certificate, FreeRDP is expected to fail closed. */
    desktop_dirty = false;
    if (!freerdp_connect(client)) {
        if (cert_failure[0])
            snprintf(reason, reason_size, "%.90s", cert_failure);
        else
        snprintf(reason, reason_size, "RDP connect failed (0x%08lx)",
                 (unsigned long)freerdp_get_last_error(client->context));
        goto done;
    }
    connected = true;
    session_pad_init(&pad);
    session_zoom_init(&zoom);
    snprintf(reason, reason_size, "Session ended");
    result = 0;

    while (aptMainLoop() && !freerdp_shall_disconnect_context(client->context)) {
        HANDLE handles[RDP_MAX_EVENTS];
        DWORD count, wait_status;
        u32 down, held, up;
        PadActions action;
        touchPosition touch;
        circlePosition circle;
        rdpInput *input = client->context->input;
        int tick;

        hidScanInput();
        down = hidKeysDown();
        held = hidKeysHeld();
        up = hidKeysUp();
        hidCircleRead(&circle);
        /* Hold L and rotate the Circle Pad. Clockwise increases magnification,
           counterclockwise reduces it. No mouse wheel or RDP size changes. */
        if (session_zoom_update(&zoom,(held & KEY_L)!=0,
                                circle.dx,circle.dy))
            pad_redraw=true;

        if (held & KEY_TOUCH) {
            hidTouchRead(&touch);
            session_pad_update(&pad, true, (int)touch.px, (int)touch.py, &action);
        } else {
            session_pad_update(&pad, false, -1, -1, &action);
        }
        if (action.redraw) pad_redraw = true;
        if (action.pointer_moved && zoom.visible > 1.001f)
            pad_redraw=true; /* keep viewport tracking the remote cursor */
        if (action.disconnect || (down & KEY_START)) {
            snprintf(reason, reason_size, "Disconnected by user");
            break;
        }

        /* Software keyboard is transient, not a permanent overlay. It never
         * stores entered text and the pad layout returns after dismissal. */
        if (down & KEY_X || action.keyboard) {
            if (input) send_text(input);
            pad_redraw = true;
        }

        if (input) {
            if (action.pointer_moved) {
                (void)freerdp_input_send_mouse_event(input, PTR_FLAGS_MOVE,
                         (UINT16)pad.pointer_x, (UINT16)pad.pointer_y);
            }

            /* Physical A can be held for dragging over the touchpad.
             * Physical B is right-click (START or toolbar disconnects). */
            if (down & KEY_A) {
                (void)freerdp_input_send_mouse_event(input,
                        PTR_FLAGS_BUTTON1 | PTR_FLAGS_DOWN,
                        (UINT16)pad.pointer_x, (UINT16)pad.pointer_y);
                left_down = true;
            }
            if ((up & KEY_A) && left_down) {
                (void)freerdp_input_send_mouse_event(input, PTR_FLAGS_BUTTON1,
                        (UINT16)pad.pointer_x, (UINT16)pad.pointer_y);
                left_down = false;
            }
            if (down & KEY_B) {
                (void)freerdp_input_send_mouse_event(input,
                        PTR_FLAGS_BUTTON2 | PTR_FLAGS_DOWN,
                        (UINT16)pad.pointer_x, (UINT16)pad.pointer_y);
                right_down = true;
            }
            if ((up & KEY_B) && right_down) {
                (void)freerdp_input_send_mouse_event(input, PTR_FLAGS_BUTTON2,
                        (UINT16)pad.pointer_x, (UINT16)pad.pointer_y);
                right_down = false;
            }

            if (action.left_click && !left_down) {
                (void)freerdp_input_send_mouse_event(input,
                        PTR_FLAGS_BUTTON1 | PTR_FLAGS_DOWN,
                        (UINT16)pad.pointer_x, (UINT16)pad.pointer_y);
                (void)freerdp_input_send_mouse_event(input, PTR_FLAGS_BUTTON1,
                        (UINT16)pad.pointer_x, (UINT16)pad.pointer_y);
            }
            if (action.right_click && !right_down) {
                (void)freerdp_input_send_mouse_event(input,
                        PTR_FLAGS_BUTTON2 | PTR_FLAGS_DOWN,
                        (UINT16)pad.pointer_x, (UINT16)pad.pointer_y);
                (void)freerdp_input_send_mouse_event(input, PTR_FLAGS_BUTTON2,
                        (UINT16)pad.pointer_x, (UINT16)pad.pointer_y);
            }

            /* RDP wheel rotation uses 9-bit signed notation: 120 up,
             * 0x100 | (0x100-120) down. Never combine MOVE with WHEEL. */
            for (tick = 0; tick < abs(action.wheel_notches); ++tick) {
                const UINT16 flags = (UINT16)(PTR_FLAGS_WHEEL |
                    (action.wheel_notches > 0 ? 120 :
                     (PTR_FLAGS_WHEEL_NEGATIVE | (0x100 - 120))));
                (void)freerdp_input_send_mouse_event(input, flags,
                        (UINT16)pad.pointer_x, (UINT16)pad.pointer_y);
            }
        }

        count = freerdp_get_event_handles(client->context, handles, RDP_MAX_EVENTS);
        if (!count || count > RDP_MAX_EVENTS) {
            snprintf(reason, reason_size, "FreeRDP event handles unavailable");
            result = -1;
            break;
        }
        wait_status = WaitForMultipleObjects(count, handles, FALSE, 20);
        if (wait_status == WAIT_FAILED || !freerdp_check_event_handles(client->context)) {
            snprintf(reason, reason_size, "FreeRDP event pump failed");
            result = -1;
            break;
        }
        if (desktop_dirty || pad_redraw) {
            /* gfxSwapBuffers swaps BOTH screens. Always redraw the current
             * GDI desktop into the new top backbuffer when the bottom pad
             * changes, otherwise the previous top console can reappear. */
            if (!copy_desktop_frame(client->context,&zoom,pad.pointer_x,pad.pointer_y)) {
                snprintf(reason, reason_size, "Invalid GDI or top-screen framebuffer");
                result = -1;
                break;
            }
            session_pad_draw(&pad,session_zoom_percent(&zoom));
            gfxFlushBuffers();
            gfxSwapBuffers();
            desktop_dirty = false;
            pad_redraw = false;
        }
        gspWaitForVBlank();
    }
done:
    if (connected && client && client->context && client->context->input) {
        rdpInput *input = client->context->input;
        if (left_down)
            (void)freerdp_input_send_mouse_event(input, PTR_FLAGS_BUTTON1,
                      (UINT16)pad.pointer_x, (UINT16)pad.pointer_y);
        if (right_down)
            (void)freerdp_input_send_mouse_event(input, PTR_FLAGS_BUTTON2,
                      (UINT16)pad.pointer_x, (UINT16)pad.pointer_y);
    }
    OPENSSL_cleanse(password, sizeof(password));
    if (connected)
        freerdp_disconnect(client);
    if (client) {
        if (initialized) {
            /* Scrub the FreeRDP-owned password string before freeing settings.
             * NLA/SSPI can have additional short-lived copies we do not own. */
            if (client->context && client->context->settings &&
                client->context->settings->Password) {
                char *owned = client->context->settings->Password;
                OPENSSL_cleanse(owned, strlen(owned));
            }
            freerdp_context_free(client);
        }
        freerdp_free(client);
    }
    OPENSSL_cleanse(&active_pin, sizeof(active_pin));
    OPENSSL_cleanse(&active_pool, sizeof(active_pool));
    OPENSSL_cleanse(requested_host, sizeof(requested_host));
    have_manual_pin = false;
    requested_port = 0;
    OPENSSL_cleanse(cert_failure, sizeof(cert_failure));
    return result;
}
#endif
