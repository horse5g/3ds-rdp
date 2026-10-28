#include "freerdp_session.h"
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
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define RDP_W 400
#define RDP_H 240
#define RDP_MAX_EVENTS 64

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
    rdpGdi *gdi;
    u16 fb_width = 0, fb_height = 0;
    u8 *frame;
    int x, y, width, height;

    if (!context || !(gdi = context->gdi) || !gdi->primary_buffer ||
        gdi->stride < 4 || gdi->width <= 0 || gdi->height <= 0)
        return TRUE;

    frame = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &fb_width, &fb_height);
    if (!frame)
        return FALSE;
    (void)fb_width; (void)fb_height;

    width = gdi->width < RDP_W ? gdi->width : RDP_W;
    height = gdi->height < RDP_H ? gdi->height : RDP_H;
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
    bool mouse_down = false;
    u16 last_x = RDP_W / 2, last_y = RDP_H / 2;
    int result = -1;

    if (!cfg || !cfg->host[0] || !reason || !reason_size)
        return -1;
    snprintf(reason, reason_size, "FreeRDP startup failed");

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
    if (!freerdp_connect(client)) {
        snprintf(reason, reason_size, "RDP connect failed (0x%08lx)",
                 (unsigned long)freerdp_get_last_error(client->context));
        goto done;
    }
    connected = true;
    snprintf(reason, reason_size, "Session ended");
    result = 0;

    while (aptMainLoop() && !freerdp_shall_disconnect_context(client->context)) {
        HANDLE handles[RDP_MAX_EVENTS];
        DWORD count, wait_status;
        u32 down, held, up;
        touchPosition touch;
        rdpInput *input = client->context->input;
        hidScanInput();
        down = hidKeysDown();
        up = hidKeysUp();
        held = hidKeysHeld();
        if (down & (KEY_B | KEY_START))
            break;
        if ((down & KEY_X) && input)
            send_text(input);
        if ((held & KEY_TOUCH) && input) {
            hidTouchRead(&touch);
            last_x = (u16)((unsigned)touch.px * RDP_W / 320);
            last_y = (u16)touch.py;
            if (last_x >= RDP_W) last_x = RDP_W - 1;
            if (last_y >= RDP_H) last_y = RDP_H - 1;
            (void)freerdp_input_send_mouse_event(input, PTR_FLAGS_MOVE, last_x, last_y);
        }
        if ((down & KEY_A) && input) {
            (void)freerdp_input_send_mouse_event(input, PTR_FLAGS_BUTTON1 | PTR_FLAGS_DOWN,
                                                 last_x, last_y);
            mouse_down = true;
        }
        if ((up & KEY_A) && mouse_down && input) {
            (void)freerdp_input_send_mouse_event(input, PTR_FLAGS_BUTTON1, last_x, last_y);
            mouse_down = false;
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
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
done:
    OPENSSL_cleanse(password, sizeof(password));
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
