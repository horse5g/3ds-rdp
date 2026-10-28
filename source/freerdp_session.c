#include "freerdp_session.h"
#include "cert_pin.h"
#include "session_pad.h"
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
 * validated on O3DS. Never accept an untrusted or changed certificate. */
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
#include <time.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define RDP_W 400
#define RDP_H 240
#define RDP_MAX_EVENTS 64

#define PIN_FILE "sdmc:/3ds/3ds-rdp/cert-pin.txt"
#define PIN_MAX_BYTES 1024

/* Single-session homebrew app: no concurrent RDP sessions. */
static RdpCertPin active_pin;
static char cert_failure[100];
static bool desktop_dirty;

static bool load_cert_pin(RdpCertPin *pin)
{
    char text[PIN_MAX_BYTES + 1];
    size_t n;
    FILE *f = fopen(PIN_FILE, "rb");
    if (!f)
        return false;
    n = fread(text, 1, sizeof(text), f);
    const bool read_error = ferror(f) != 0;
    fclose(f);
    if (read_error || n >= sizeof(text) || memchr(text, 0, n))
        return false;
    text[n] = '\0';
    return rdp_cert_pin_parse(text, pin) == 0;
}

/* FreeRDP ExternalCertificateManagement callback. Never trust on first use.
 * Pin supplied out of band; require leaf SHA256, expected endpoint, name and
 * validity dates. Returning 2 accepts for THIS SESSION without saving a
 * FreeRDP known_hosts entry. */
static int verify_x509_pin(freerdp *instance, const BYTE *pem, size_t length,
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
    if (!pem || !length || length > INT_MAX ||
        !rdp_cert_pin_matches(&active_pin, host, port) ||
        flags != VERIFY_CERT_FLAG_NONE)
        return 0;

    bio = BIO_new_mem_buf(pem, (int)length);
    if (!bio)
        return 0;
    cert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
    if (!cert)
        goto finish;

    /* Fail closed if the 3DS real-time clock is unset or clearly invalid. */
    now = time(NULL);
    if (now < (time_t)1577836800) {
        snprintf(cert_failure, sizeof(cert_failure), "Set the 3DS date/time first");
        goto finish;
    }
    if (X509_cmp_time(X509_get0_notBefore(cert), &now) >= 0 ||
        X509_cmp_time(X509_get0_notAfter(cert), &now) <= 0)
        goto finish;

    if (inet_pton(AF_INET, host, &ipv4) == 1) {
        if (X509_check_ip_asc(cert, host, 0) != 1)
            goto finish;
    } else {
        /* DNS SAN preferred; OpenSSL can fall back to a CN if no SAN exists. */
        if (X509_check_host(cert, host, 0, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS, NULL) != 1)
            goto finish;
    }

    if (X509_digest(cert, EVP_sha256(), digest, &digest_length) != 1 ||
        digest_length != sizeof(active_pin.sha256) ||
        CRYPTO_memcmp(digest, active_pin.sha256, sizeof(active_pin.sha256)) != 0)
        goto finish;

    cert_failure[0] = '\0';
    result = 2;
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
static BOOL copy_desktop_frame(rdpContext *context)
{
    rdpGdi *gdi;
    u16 fb_width = 0, fb_height = 0;
    u8 *frame;
    int x, y, width, height;

    if (!context || !(gdi = context->gdi) || !gdi->primary_buffer ||
        gdi->width <= 0 || gdi->height <= 0)
        return FALSE;
    width = gdi->width < RDP_W ? gdi->width : RDP_W;
    height = gdi->height < RDP_H ? gdi->height : RDP_H;
    if (gdi->stride < (size_t)width * 4 ||
        gfxGetScreenFormat(GFX_TOP) != GSP_BGR8_OES)
        return FALSE;

    frame = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &fb_width, &fb_height);
    if (!frame || fb_width < RDP_H || fb_height < RDP_W)
        return FALSE;
    for (x = 0; x < width; ++x) {
        for (y = 0; y < height; ++y) {
            const u8 *src = gdi->primary_buffer + (size_t)y * gdi->stride +
                            (size_t)x * 4;
            u8 *dst = frame + ((size_t)x * RDP_H + (RDP_H - 1 - y)) * 3;
            /* XRGB32 is stored as B,G,R,X; GSP_BGR8 uses B,G,R bytes. */
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
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
    int result = -1;

    if (!cfg || !cfg->host[0] || !reason || !reason_size)
        return -1;
    snprintf(reason, reason_size, "FreeRDP startup failed");

    /* A pin must already have been installed from a trusted source.
       Do not request a password before verifying its configuration. */
    memset(&active_pin, 0, sizeof(active_pin));
    cert_failure[0] = '\0';
    if (!load_cert_pin(&active_pin) ||
        !rdp_cert_pin_matches(&active_pin, cfg->host, cfg->port)) {
        snprintf(reason, reason_size, "Missing/invalid cert-pin.txt or host mismatch");
        goto done;
    }

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
    client->VerifyX509Certificate = verify_x509_pin;
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
    snprintf(reason, reason_size, "Session ended");
    result = 0;

    while (aptMainLoop() && !freerdp_shall_disconnect_context(client->context)) {
        HANDLE handles[RDP_MAX_EVENTS];
        DWORD count, wait_status;
        u32 down, held, up;
        PadActions action;
        touchPosition touch;
        rdpInput *input = client->context->input;
        int tick;

        hidScanInput();
        down = hidKeysDown();
        held = hidKeysHeld();
        up = hidKeysUp();

        if (held & KEY_TOUCH) {
            hidTouchRead(&touch);
            session_pad_update(&pad, true, (int)touch.px, (int)touch.py, &action);
        } else {
            session_pad_update(&pad, false, -1, -1, &action);
        }
        if (action.redraw) pad_redraw = true;
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
            if (!copy_desktop_frame(client->context)) {
                snprintf(reason, reason_size, "Invalid GDI or top-screen framebuffer");
                result = -1;
                break;
            }
            session_pad_draw(&pad);
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
    OPENSSL_cleanse(&active_pin, sizeof(active_pin));
    OPENSSL_cleanse(cert_failure, sizeof(cert_failure));
    if (connected)
        freerdp_disconnect(client);
    if (client) {
        if (initialized)
            freerdp_context_free(client);
        freerdp_free(client);
    }
    return result;
}
#endif
