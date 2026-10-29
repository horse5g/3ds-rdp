#include "freerdp_session.h"
#include "cert_pin.h"
#include "cert_pool.h"
#include "cert_store.h"
#include "session_pad.h"
#include "session_zoom.h"
#include "session_cursor.h"
#include "session_keys.h"
#include "connection_ui.h"
#include "rdp_pixels.h"
#include "session_wait.h"
#include "display_buffers.h"
#include "rdp_environment.h"
#include "rdp_desktop_limits.h"
#include "rdp_startup.h"
#include "horizon_rng.h"
#include "rdp_ps_rng_diag.h"
#include "rdp_openssl_init_diag.h"
#include "rdp_freerdp_diag.h"
#include "rdp_pthread_tsd.h"
#include <stdio.h>

#ifndef RDP_FULL_CLIENT
int freerdp_3ds_run(const RdpConfig *cfg, const RdpUiOptions *options, char *reason, size_t reason_size)
{
    (void)cfg;
    (void)options;
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
#include <freerdp/error.h>
#include <freerdp/settings.h>
#include <freerdp/gdi/gdi.h>
#include <freerdp/graphics.h>
#include <freerdp/codec/color.h>
#include <freerdp/update.h>
#include <freerdp/input.h>
#include <winpr/synch.h>
#include <winpr/path.h>
#include <winpr/sysinfo.h>
#include <openssl/crypto.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <openssl/provider.h>
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
#define RDP_TCP_CONNECT_TIMEOUT_MS 12000

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
static char post_connect_failure[100];
/* Give hardware tests an actionable stage instead of reporting every
 * authentication, DNS, transport and activation error as "connect failed".
 * Never include the password or FreeRDP's credential buffers. */
static const char *connection_failure_hint(UINT error)
{
    switch (error) {
        case FREERDP_ERROR_DNS_ERROR:
        case FREERDP_ERROR_DNS_NAME_NOT_FOUND:
            return "RDP DNS lookup failed (try IPv4)";
        case FREERDP_ERROR_CONNECT_FAILED:
        case FREERDP_ERROR_CONNECT_TRANSPORT_FAILED:
            return "RDP TCP failed (check IP/port)";
        case FREERDP_ERROR_SECURITY_NEGO_CONNECT_FAILED:
            return "RDP NLA security negotiation failed";
        case FREERDP_ERROR_TLS_CONNECT_FAILED:
            return "RDP TLS handshake failed";
        case FREERDP_ERROR_CONNECT_LOGON_FAILURE:
        case FREERDP_ERROR_CONNECT_WRONG_PASSWORD:
        case FREERDP_ERROR_CONNECT_NO_OR_MISSING_CREDENTIALS:
        case FREERDP_ERROR_AUTHENTICATION_FAILED:
            return "RDP NLA username/password rejected";
        case FREERDP_ERROR_CONNECT_KDC_UNREACHABLE:
            return "Kerberos unavailable; NTLMv2 required";
        case FREERDP_ERROR_CONNECT_ACCOUNT_LOCKED_OUT:
        case FREERDP_ERROR_CONNECT_ACCOUNT_DISABLED:
        case FREERDP_ERROR_CONNECT_ACCESS_DENIED:
        case FREERDP_ERROR_CONNECT_LOGON_TYPE_NOT_GRANTED:
            return "Windows denied this RDP account";
        case FREERDP_ERROR_CONNECT_PASSWORD_EXPIRED:
        case FREERDP_ERROR_CONNECT_PASSWORD_MUST_CHANGE:
            return "Windows password expired/must change";
        case FREERDP_ERROR_CONNECT_ACTIVATION_TIMEOUT:
            return "RDP desktop activation timed out";
        case FREERDP_ERROR_POST_CONNECT_FAILED:
            return "FreeRDP PostConnect graphics failed";
        case FREERDP_ERROR_CONNECT_CANCELLED:
            return "RDP connection cancelled";
        default:
            return "RDP connection failed (see code)";
    }
}

/* RAND_bytes can return 0 without queuing an OpenSSL error when
 * global RNG state or a thread-local DRBG fails to initialize. */
static bool openssl_rng_preflight(OSSL_PROVIDER **provider_ref,
                                char *error, size_t cap)
{
    EVP_RAND *algorithm = NULL;
    EVP_RAND_CTX *primary = NULL, *public_rng = NULL;
    unsigned char check[32] = {0};
    const char *stage = "crypto init";
    unsigned long code;
    const char *description;
    RdpPsRngDiag baseline = {0};
    bool ok = false;

    if (!provider_ref || !error || !cap)
        return false;
    *provider_ref = NULL;
    /* Counts are process-wide and may include previous connection attempts.
       Compare a fresh preflight with its own baseline, not the boot totals. */
    ossl_3ds_ps_rng_snapshot(&baseline);
    ERR_clear_error();
    if (!OPENSSL_init_crypto(OPENSSL_INIT_NO_LOAD_CONFIG, NULL))
        goto fail;
    stage = "default provider";
    /* Retain the embedded default provider through the entire session. */
    *provider_ref = OSSL_PROVIDER_load(NULL, "default");
    if (!*provider_ref)
        goto fail;
    stage = "CTR-DRBG";
    algorithm = EVP_RAND_fetch(NULL, "CTR-DRBG", NULL);
    if (!algorithm)
        goto fail;
    EVP_RAND_free(algorithm);
    algorithm = NULL;
    stage = "primary DRBG";
    primary = RAND_get0_primary(NULL);
    if (!primary)
        goto fail;
    stage = "primary state";
    if (EVP_RAND_get_state(primary) != EVP_RAND_STATE_READY)
        goto fail;
    stage = "public DRBG";
    public_rng = RAND_get0_public(NULL);
    if (!public_rng)
        goto fail;
    stage = "public state";
    if (EVP_RAND_get_state(public_rng) != EVP_RAND_STATE_READY)
        goto fail;
    stage = "RAND_bytes";
    if (RAND_bytes(check, (int)sizeof(check)) != 1)
        goto fail;
    ok = true;
    error[0] = 0;
    goto done;
fail:
    code = ERR_peek_last_error();
    description = code ? ERR_reason_error_string(code) : NULL;
    /* Provider/DRBG failures sometimes pop OpenSSL's error queue. Snapshot
     * the actual Horizon backend without reading RNG seed bytes. */
    RdpPsRngDiag psdiag = {0};
    ossl_3ds_ps_rng_snapshot(&psdiag);
    const unsigned e_calls = psdiag.entropy_calls - baseline.entropy_calls;
    const unsigned e_ok = psdiag.entropy_successes - baseline.entropy_successes;
    const unsigned n_calls = psdiag.nonce_calls - baseline.nonce_calls;
    const unsigned n_ok = psdiag.nonce_successes - baseline.nonce_successes;
    if (e_calls || n_calls) {
        snprintf(error, cap,
                 "SSL %.18s %08lX PS E%u/%u N%u/%u F%u R%08X",
                 stage, code, e_ok, e_calls, n_ok, n_calls,
                 psdiag.last_failure_stage, psdiag.last_ps_result);
    } else if (strcmp(stage, "crypto init") == 0) {
        snprintf(error, cap, "OpenSSL core init S%u (0x%08lX)",
                 ossl_3ds_crypto_init_stage(), code);
    } else if (description && *description) {
        snprintf(error, cap, "SSL %.22s %.27s (0x%08lX)",
                 stage, description, code);
    } else {
        snprintf(error, cap, "SSL %.22s no PS seed calls (0x%08lX)",
                 stage, code);
    }
    ERR_clear_error();
done:
    EVP_RAND_free(algorithm);
    OPENSSL_cleanse(check, sizeof(check));
    if (!ok) {
        if (*provider_ref)
            OSSL_PROVIDER_unload(*provider_ref);
        *provider_ref = NULL;
    }
    return ok;
}

static bool desktop_dirty;
static SessionCursor system_cursor;
static int cursor_remote_x=RDP_W/2,cursor_remote_y=RDP_H/2;
static int remote_w=RDP_W,remote_h=RDP_H;

/* Connect/TLS/NLA runs on a worker. All HID, console and GFX access stays on
 * the foreground thread. The worker exchanges only a fingerprint challenge
 * and decision using libctru synchronization primitives. */
typedef struct {
    LightLock lock;
    LightEvent decision_ready;
    freerdp *client;
    bool finished;
    bool succeeded;
    bool cancel_requested;
    bool challenge_pending;
    bool challenge_answered;
    int decision;
    char challenge_host[RDP_HOST_CAP];
    uint16_t challenge_port;
    unsigned char challenge_sha256[32];
} RdpConnectGate;

static RdpConnectGate *connect_gate; /* only one session exists */

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
    RdpConnectGate *gate = connect_gate;
    int result;
    if (!gate || !host || strlen(host) >= RDP_HOST_CAP)
        return 0;
    LightLock_Lock(&gate->lock);
    if (gate->cancel_requested || gate->challenge_pending) {
        LightLock_Unlock(&gate->lock);
        return 0;
    }
    snprintf(gate->challenge_host, sizeof(gate->challenge_host), "%s", host);
    gate->challenge_port = port;
    memcpy(gate->challenge_sha256, sha256, 32);
    gate->challenge_pending = true;
    gate->challenge_answered = false;
    LightLock_Unlock(&gate->lock);

    for (;;) {
        /* Wake periodically so a session abort does not depend on an event
         * that the foreground might be unable to signal during app exit. */
        (void)LightEvent_WaitTimeout(&gate->decision_ready, 100000000LL);
        LightLock_Lock(&gate->lock);
        if (gate->cancel_requested) {
            gate->challenge_pending = false;
            LightLock_Unlock(&gate->lock);
            return 0;
        }
        if (gate->challenge_answered) {
            result = gate->decision;
            gate->challenge_answered = false;
            gate->challenge_pending = false;
            LightLock_Unlock(&gate->lock);
            return result;
        }
        LightLock_Unlock(&gate->lock);
    }
}

/* Certificate UI is strictly a foreground-thread responsibility. */
static void draw_cert_challenge(const char *host, uint16_t port,
                                const unsigned char sha256[32])
{
    static const char hex[] = "0123456789ABCDEF";
    char fingerprint[65];
    for (size_t i = 0; i < 32; ++i) {
        fingerprint[2*i] = hex[sha256[i] >> 4];
        fingerprint[2*i+1] = hex[sha256[i] & 15];
    }
    fingerprint[64] = 0;
    rdp_display_bind_console(&trust_console, GFX_TOP);
    consoleSelect(&trust_console);
    printf("\x1b[2J\x1b[HUNKNOWN RDP CERTIFICATE\n");
    printf("This server is NOT yet trusted.\n\n");
    printf("Host: %.40s\nPort: %u\n\n", host, (unsigned)port);
    printf("Leaf SHA-256 (verify independently):\n");
    printf("%.32s\n%s\n\n", fingerprint, fingerprint+32);
    printf("First-use trust can allow MITM!\n");
    printf("Verify this hash on the Windows host.\n\n");
    printf("B/START: Reject\n");
    printf("A: Accept for this session\n");
    printf("X: Accept and save on SD\n");
    /* gfxSwapBuffers presents BOTH LCDs. Draw the lower buffer too, so it
       cannot alternate between a current and an old/uninitialized image. */
    connection_ui_draw_status("UNKNOWN RDP CERTIFICATE",
                              "VERIFY FINGERPRINT ON TOP",false);
    gfxFlushBuffers();
    gfxSwapBuffers();
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
        !rdp_cert_host_equal(host, requested_host) || port != requested_port ||
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

/* Windows cursor shapes are transmitted as RDP pointer updates. Register
 * FreeRDP's pointer cache callbacks and decode BGRA32, preserving the server
 * hotspot and alpha channel instead of substituting a fixed 3DS arrow. */
typedef struct {
    rdpPointer pointer; /* must be first for FreeRDP's allocator */
    BYTE *pixels;
} Pointer3ds;

static BOOL pointer_new(rdpContext *context,rdpPointer *base)
{
    Pointer3ds *p=(Pointer3ds *)base;
    size_t len;
    if(!p||!context||!context->gdi)return FALSE;
    p->pixels=NULL;
    if(base->width>RDP_CURSOR_MAX||base->height>RDP_CURSOR_MAX ||
       !base->width||!base->height)return TRUE; /* safe fallback */
    len=(size_t)base->width*base->height*4;
    p->pixels=(BYTE *)malloc(len);
    if(!p->pixels)return FALSE;
    if(!freerdp_image_copy_from_pointer_data(p->pixels,PIXEL_FORMAT_BGRA32,0,0,0,
          base->width,base->height,base->xorMaskData,base->lengthXorMask,
          base->andMaskData,base->lengthAndMask,base->xorBpp,
          &context->gdi->palette)){
        free(p->pixels);p->pixels=NULL;return FALSE;
    }
    return TRUE;
}
static void pointer_free(rdpContext *context,rdpPointer *base)
{
    Pointer3ds *p=(Pointer3ds *)base;
    (void)context;
    if(p){free(p->pixels);p->pixels=NULL;}
}
static BOOL pointer_set(rdpContext *context,rdpPointer *base)
{
    Pointer3ds *p=(Pointer3ds *)base;
    (void)context;
    if(!p || !p->pixels){
        system_cursor.visible=false;desktop_dirty=true;return TRUE;
    }
    if(!session_cursor_set(&system_cursor,p->pixels,(int)base->width,
                           (int)base->height,(int)base->xPos,(int)base->yPos))
        system_cursor.visible=false;
    desktop_dirty=true;
    return TRUE;
}
static BOOL pointer_null(rdpContext *context)
{
    (void)context;
    system_cursor.visible=false;desktop_dirty=true;return TRUE;
}
static BOOL pointer_default(rdpContext *context)
{
    uint8_t arrow[16*16*4]={0};
    static const uint16_t rows[16]={
        0x0001,0x0003,0x0007,0x000F,0x001F,0x003F,0x007F,0x00FF,
        0x01FF,0x03FF,0x003F,0x0067,0x00C3,0x0181,0x0000,0x0000
    };
    (void)context;
    /* PointerSystem=DEFAULT requests the ordinary Windows arrow. Render a
       small local fallback instead of leaving an old text or resize cursor. */
    for(int y=0;y<16;++y) {
        for(int x=0;x<16;++x) {
            if(!(rows[y] & (1u<<x)))continue;
            const bool outline=(x==0 || x==15 ||
                !(rows[y]&(1u<<(x+1))) ||
                (y>0 && !(rows[y-1]&(1u<<x))) ||
                (y<15 && !(rows[y+1]&(1u<<x))));
            uint8_t *p=&arrow[((size_t)y*16+x)*4];
            p[0]=p[1]=p[2]=outline?0:255;
            p[3]=255;
        }
    }
    (void)session_cursor_set(&system_cursor,arrow,16,16,0,0);
    desktop_dirty=true;
    return TRUE;
}
static BOOL pointer_position(rdpContext *context,UINT32 x,UINT32 y)
{
    (void)context;
    cursor_remote_x=x<(UINT32)remote_w?(int)x:remote_w-1;
    cursor_remote_y=y<(UINT32)remote_h?(int)y:remote_h-1;
    desktop_dirty=true;return TRUE;
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

    view=session_zoom_view(zoom,pointer_x,pointer_y,gdi->width,gdi->height);
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
    session_cursor_overlay(frame,RDP_W,RDP_H,&system_cursor,
                           cursor_remote_x,cursor_remote_y,
                           view.left_q16,view.top_q16,
                           view.width_q16,view.height_q16);
    return TRUE;
}

static BOOL desktop_resize(rdpContext *context)
{
    UINT32 width, height;
    if (!context || !context->gdi || !context->settings)
        return FALSE;
    width = freerdp_settings_get_uint32(context->settings, FreeRDP_DesktopWidth);
    height = freerdp_settings_get_uint32(context->settings, FreeRDP_DesktopHeight);
    /* A server-initiated resize could otherwise allocate tens/hundreds of
     * megabytes and kill the O3DS process. Refuse it before GDI allocates. */
    if (!rdp_desktop_size_allowed(width, height,
                                  (unsigned)remote_w, (unsigned)remote_h))
        return FALSE;
    return gdi_resize(context->gdi, width, height);
}

static BOOL post_connect(freerdp *instance)
{
    rdpContext *ctx;
    UINT32 width, height;
    snprintf(post_connect_failure, sizeof(post_connect_failure),
             "RDP desktop setup failed");
    if (!instance || !(ctx=instance->context) ||
        !ctx->settings || !ctx->update)
        return FALSE;
    width=freerdp_settings_get_uint32(ctx->settings, FreeRDP_DesktopWidth);
    height=freerdp_settings_get_uint32(ctx->settings, FreeRDP_DesktopHeight);
    /* This is BEFORE gdi_init(), when the initial GDI framebuffer is
       allocated. A server's negotiated size must not exhaust O3DS memory. */
    if (!rdp_desktop_size_allowed(width,height,
                                  (unsigned)remote_w,(unsigned)remote_h)) {
        snprintf(post_connect_failure,sizeof(post_connect_failure),
                 "Server requested unsupported desktop size");
        return FALSE;
    }
    if (!gdi_init(instance, PIXEL_FORMAT_XRGB32)) {
        snprintf(post_connect_failure,sizeof(post_connect_failure),
                 "RDP graphics allocation failed (O3DS memory)");
        return FALSE;
    }
    post_connect_failure[0]=0;
    ctx->update->BeginPaint = begin_paint;
    ctx->update->EndPaint = end_paint;
    ctx->update->DesktopResize = desktop_resize;
    if(ctx->graphics){
        rdpPointer proto={0};
        proto.size=sizeof(Pointer3ds);
        proto.New=pointer_new;proto.Free=pointer_free;
        proto.Set=pointer_set;proto.SetNull=pointer_null;
        proto.SetDefault=pointer_default;proto.SetPosition=pointer_position;
        graphics_register_pointer(ctx->graphics,&proto);
    }
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
    rdp_display_restore_after_keyboard();
    OPENSSL_cleanse(&keyboard, sizeof(keyboard));
    return button == SWKBD_BUTTON_CONFIRM;
}

/* Only while the main thread is inside Nintendo's modal software keyboard,
 * one helper may pump FreeRDP network events. No HID or GPU calls occur on
 * this worker. The main thread never sends remote input until after join. */
typedef struct {
    rdpContext *context;
    LightLock lock;
    bool stop_requested;
    bool failed;
    DWORD failure_code;
} RdpKeyboardPump;

static void rdp_keyboard_pump_worker(void *arg)
{
    RdpKeyboardPump *pump = (RdpKeyboardPump *)arg;
    while (true) {
        HANDLE handles[RDP_MAX_EVENTS + 2];
        DWORD count, wait_status;
        LightLock_Lock(&pump->lock);
        const bool stop = pump->stop_requested;
        LightLock_Unlock(&pump->lock);
        if (stop)
            return;
        if (freerdp_shall_disconnect_context(pump->context))
            break;
        count = freerdp_get_event_handles(pump->context, handles, RDP_MAX_EVENTS);
        if (!count || count > RDP_MAX_EVENTS) {
            pump->failure_code = count;
            break;
        }
        wait_status = WaitForMultipleObjects(count, handles, FALSE, 20);
        if (!rdp_wait_result_valid((uint32_t)wait_status, (uint32_t)count)) {
            pump->failure_code = wait_status;
            break;
        }
        if (!freerdp_check_event_handles(pump->context)) {
            pump->failure_code = freerdp_get_last_error(pump->context);
            break;
        }
    }
    LightLock_Lock(&pump->lock);
    pump->failed = true;
    LightLock_Unlock(&pump->lock);
}

/* libctru threadCreate does not invoke POSIX pthread TSD destructors. */
static void rdp_keyboard_pump_worker_with_cleanup(void *arg)
{
    rdp_keyboard_pump_worker(arg);
    OPENSSL_thread_stop();
    rdp_3ds_tsd_cleanup_current_thread();
}

/* The software keyboard still owns main-thread HID/GFX; a network-only
 * worker can process RDP updates without concurrent input. This is best
 * effort if the 3DS OS suspends all user threads during an applet. */
static bool send_text(freerdp *client, char *reason, size_t reason_size)
{
    char text[128] = {0};
    SwkbdState keyboard;
    SwkbdButton button;
    RdpKeyboardPump pump = {0};
    Thread worker;
    size_t i;

    if (!client || !client->context || !client->context->input)
        return false;
    swkbdInit(&keyboard, SWKBD_TYPE_NORMAL, 2, sizeof(text) - 1);
    swkbdSetHintText(&keyboard, "Remote text (ASCII only)");
    pump.context = client->context;
    LightLock_Init(&pump.lock);
    worker = threadCreate(rdp_keyboard_pump_worker_with_cleanup, &pump,
                          64 * 1024, 0x30, -2, false);
    if (!worker) {
        snprintf(reason, reason_size, "Cannot start keyboard network worker");
        OPENSSL_cleanse(&keyboard, sizeof(keyboard));
        return false;
    }

    button = swkbdInputText(&keyboard, text, sizeof(text));
    rdp_display_restore_after_keyboard();
    LightLock_Lock(&pump.lock);
    pump.stop_requested = true;
    LightLock_Unlock(&pump.lock);
    (void)threadJoin(worker, U64_MAX);
    threadFree(worker);

    if (pump.failed) {
        snprintf(reason, reason_size, "RDP disconnected during keyboard (0x%08lx)",
                 (unsigned long)pump.failure_code);
        OPENSSL_cleanse(&keyboard, sizeof(keyboard));
        OPENSSL_cleanse(text, sizeof(text));
        return false;
    }
    if (button == SWKBD_BUTTON_CONFIRM) {
        for (i = 0; text[i]; ++i) {
            /* UTF-8 multi-byte input is not supported by this simple path. */
            const unsigned char ch = (unsigned char)text[i];
            if (ch < 0x20 || ch > 0x7e)
                continue;
            (void)freerdp_input_send_unicode_keyboard_event(
                client->context->input, 0, ch);
            (void)freerdp_input_send_unicode_keyboard_event(
                client->context->input, KBD_FLAGS_RELEASE, ch);
        }
    }
    OPENSSL_cleanse(&keyboard, sizeof(keyboard));
    OPENSSL_cleanse(text, sizeof(text));
    return true;
}

/* Worker performs only blocking FreeRDP connect and OpenSSL operations.
 * It never invokes HID, console drawing, or 3DS software keyboard APIs. */
static void rdp_connect_worker(void *arg)
{
    RdpConnectGate *gate = (RdpConnectGate *)arg;
    LightLock_Lock(&gate->lock);
    const bool cancelled_before_start = gate->cancel_requested;
    LightLock_Unlock(&gate->lock);
    const bool ok = !cancelled_before_start &&
                    freerdp_connect(gate->client) == TRUE;
    /* Destroy worker-local OpenSSL and pthread TSD before publishing done. */
    OPENSSL_thread_stop();
    rdp_3ds_tsd_cleanup_current_thread();
    LightLock_Lock(&gate->lock);
    gate->succeeded = ok;
    gate->finished = true;
    LightLock_Unlock(&gate->lock);
}

static void connect_screen(const char *host, bool cancelling)
{
    rdp_display_bind_console(&trust_console, GFX_TOP);
    consoleSelect(&trust_console);
    printf("\x1b[2J\x1b[H3DS Remote Desktop\n\n");
    printf("%.38s\n\n", host);
    if (cancelling)
        printf("Cancelling RDP connection...\n");
    else
        printf("Connecting (TLS/NLA)...\n\nB/START: Cancel\n");
    connection_ui_draw_status(cancelling?"CANCELLING CONNECTION":
                               "CONNECTING (TLS/NLA)",
                              cancelling?"STOPPING SESSION":
                                         "B / START TO CANCEL",false);
    gfxFlushBuffers();
    gfxSwapBuffers();
}

/* On success the joined worker hands its connected FreeRDP context to the
 * foreground's existing single-threaded event pump. No concurrent GDI or
 * mouse-input access occurs after the connect thread has been joined.
 * A cancellation that is stuck in a platform socket cannot safely be freed
 * under a running worker: report progress and keep waiting for abort to exit. */
static bool connect_with_ui(freerdp *client, const char *host,
                            char *reason, size_t reason_size)
{
    RdpConnectGate gate = {0};
    Thread worker;
    bool cancelling = false, challenge_visible = false;
    bool started = false, connected = false;
    LightLock_Init(&gate.lock);
    LightEvent_Init(&gate.decision_ready, RESET_ONESHOT);
    gate.client = client;
    connect_gate = &gate;
    /* Reuse the already configured top console without calling consoleInit:
       consoleInit would disable double buffering AND unexpectedly swap both
       screens, displaying an incomplete/stale bottom touch UI frame. */
    {
        PrintConsole *previous = consoleSelect(&trust_console);
        if (previous && previous->consoleInitialised) {
            trust_console = *previous;
        } else {
            consoleInit(GFX_TOP, &trust_console);
            rdp_display_enable_double_buffering();
        }
    }
    connect_screen(host, false);

    worker = threadCreate(rdp_connect_worker, &gate, 96 * 1024, 0x30, -2, false);
    if (!worker) {
        snprintf(reason, reason_size, "Could not start ARM11 RDP connection worker");
        connect_gate = NULL;
        return false;
    }
    started = true;
    while (started) {
        bool done, pending;
        char pending_host[RDP_HOST_CAP];
        uint16_t pending_port = 0;
        unsigned char pending_sha[32];
        bool active = aptMainLoop();
        u32 down = 0;

        LightLock_Lock(&gate.lock);
        done = gate.finished;
        pending = gate.challenge_pending && !gate.challenge_answered;
        if (pending) {
            memcpy(pending_host, gate.challenge_host, sizeof(pending_host));
            pending_port = gate.challenge_port;
            memcpy(pending_sha, gate.challenge_sha256, sizeof(pending_sha));
        }
        LightLock_Unlock(&gate.lock);
        if (done)
            break;
        if (active) {
            hidScanInput();
            down = hidKeysDown();
        }
        if ((!active || (down & (KEY_B | KEY_START))) && !pending &&
            !cancelling) {
            cancelling = true;
            LightLock_Lock(&gate.lock);
            gate.cancel_requested = true;
            LightLock_Unlock(&gate.lock);
            LightEvent_Signal(&gate.decision_ready);
            (void)freerdp_abort_connect_context(client->context);
            if (active)
                connect_screen(host, true);
        } else if (pending && !cancelling) {
            if (!challenge_visible && active) {
                draw_cert_challenge(pending_host, pending_port, pending_sha);
                challenge_visible = true;
            }
            if (!active || (down & (KEY_B | KEY_START))) {
                /* Reject only the new certificate; B/START during its
                 * prompt does not automatically close the app. */
                LightLock_Lock(&gate.lock);
                gate.decision = 0;
                gate.challenge_answered = true;
                LightLock_Unlock(&gate.lock);
                LightEvent_Signal(&gate.decision_ready);
                challenge_visible = false;
            } else if (down & (KEY_A | KEY_X)) {
                LightLock_Lock(&gate.lock);
                gate.decision = (down & KEY_X) ? 2 : 1;
                gate.challenge_answered = true;
                LightLock_Unlock(&gate.lock);
                LightEvent_Signal(&gate.decision_ready);
                challenge_visible = false;
            }
        }
        if (active)
            gspWaitForVBlank();
        else
            svcSleepThread(16000000LL);
    }
    /* Only join after worker has published completion, then it is safe to
     * destroy the gate and use the FreeRDP context on this main thread. */
    (void)threadJoin(worker, U64_MAX);
    threadFree(worker);
    LightLock_Lock(&gate.lock);
    const bool established = gate.succeeded;
    connected = established && !gate.cancel_requested;
    LightLock_Unlock(&gate.lock);
    connect_gate = NULL;
    /* Cancellation can race with the final NLA response. If the worker
     * connected just as B was pressed, explicitly close that live session:
     * the caller's unsuccessful-connect cleanup won't disconnect it. */
    if (established && !connected)
        (void)freerdp_disconnect(client);
    if (!connected && cancelling)
        snprintf(reason, reason_size, "RDP connection cancelled");
    return connected;
}

int freerdp_3ds_run(const RdpConfig *cfg, const RdpUiOptions *options, char *reason, size_t reason_size)
{
    freerdp *client = NULL;
    rdpSettings *settings;
    OSSL_PROVIDER *rng_provider = NULL;
    char password[128] = {0};
    bool initialized = false;
    bool connected = false;
    bool left_down = false;
    bool right_down = false;
    bool pad_redraw = true;
    SessionPad pad;
    SessionZoom zoom;
    RdpUiOptions opts=options?*options:rdp_options_default();
    int result = -1;

    if (!reason || !reason_size)
        return -1;
    /* A probe-only host profile is not sufficient for NLA. Catch missing
       credentials and malformed fields before any FreeRDP/WinPR allocation
       or password applet, instead of a vague CredSSP authentication error. */
    if (rdp_full_session_preflight(cfg, &opts, reason, reason_size))
        return -1;
    snprintf(reason, reason_size, "FreeRDP startup failed");

    /* Load both trust sources before prompting for credentials. Missing files
       are allowed (new server); malformed or unreadable files fail closed. */
    memset(&active_pin, 0, sizeof(active_pin));
    memset(&active_pool, 0, sizeof(active_pool));
    cert_failure[0] = '\0';
    post_connect_failure[0] = '\0';
    memset(requested_host, 0, sizeof(requested_host));
    memcpy(requested_host, cfg->host, strlen(cfg->host) + 1);
    requested_port = cfg->port;
    const int pin_state = load_cert_pin(&active_pin);
    if (pin_state < 0 || !load_cert_pool(&active_pool)) {
        snprintf(reason, reason_size, "Invalid or unreadable RDP certificate trust file");
        goto done;
    }
    have_manual_pin = (pin_state > 0);

    /* WinPR has no POSIX user account on Horizon. FreeRDP 3.32 requires
     * HOME to allocate the client settings during freerdp_context_new().
     * Export a private, existing SD directory before any FreeRDP client
     * object is instantiated. */
    if (rdp_environment_init("sdmc:/3ds/3ds-rdp", reason, reason_size) != 0)
        goto done;
    {
        char *winpr_home = GetKnownPath(KNOWN_PATH_HOME);
        char *winpr_config = GetKnownPath(KNOWN_PATH_XDG_CONFIG_HOME);
        const bool ok = winpr_home && winpr_config;
        free(winpr_home);
        free(winpr_config);
        if (!ok) {
            snprintf(reason, reason_size,
                     "WinPR cannot resolve HOME/XDG paths on SD");
            goto done;
        }
    }

    /* FreeRDP settings_new() calls GetComputerNameExA before a client
       context exists. 3DS has no guaranteed gethostname(), so verify our
       narrowly scoped WinPR client-label fallback is active first. */
    {
        CHAR client_name[MAX_COMPUTERNAME_LENGTH+1]={0};
        DWORD client_name_size=(DWORD)sizeof(client_name);
        if (!GetComputerNameExA(ComputerNameNetBIOS,
                                client_name,&client_name_size) ||
            !client_name[0]) {
            snprintf(reason,reason_size,
                     "WinPR cannot get 3DS client hostname");
            goto done;
        }
    }

    /* Separate a Horizon PS IPC failure from an OpenSSL provider/DRBG
       failure. Both are fatal: do not seed TLS with a weak substitute. */
    {
        char ps_status[96] = {0};
        if (!horizon_rng_selftest(ps_status, sizeof(ps_status))) {
            snprintf(reason, reason_size, "PS RNG: %.85s", ps_status);
            goto done;
        }
    }
    if (!openssl_rng_preflight(&rng_provider, reason, reason_size))
        goto done;

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
        /* freerdp_context_new_ex() destroys its partial context on
         * failure. Internal O3DS stage markers survive that cleanup. */
        const unsigned int cstage = freerdp_3ds_context_init_stage();
        const unsigned int rstage =
            cstage == 4 ? freerdp_3ds_rdp_init_stage() : 0u;
        snprintf(reason, reason_size, "FreeRDP ctx C%u R%u", cstage, rstage);
        goto done;
    }
    initialized = true;
    settings = client->context ? client->context->settings : NULL;
    if (!settings) {
        snprintf(reason, reason_size,
                 "FreeRDP context has no client settings");
        goto done;
    }
    remote_w=opts.display_mode?640:RDP_W;
    remote_h=opts.display_mode?360:RDP_H;
    /* Do WinPR/client-context allocation before opening the modal password
       keyboard. On an O3DS allocation failure, users can diagnose the
       failure immediately instead of repeatedly entering a secret. */
    if (!prompt_password(password, sizeof(password))) {
        snprintf(reason, reason_size, "Password input cancelled");
        goto done;
    }
    if (!freerdp_settings_set_string(settings, FreeRDP_ServerHostname, cfg->host) ||
        !freerdp_settings_set_uint32(settings, FreeRDP_ServerPort, cfg->port) ||
        !freerdp_settings_set_uint32(settings, FreeRDP_DesktopWidth, (UINT32)remote_w) ||
        !freerdp_settings_set_uint32(settings, FreeRDP_DesktopHeight, (UINT32)remote_h) ||
        !freerdp_settings_set_uint32(settings, FreeRDP_ColorDepth, (UINT32)opts.color_depth) ||
        /* Only NLA/CredSSP (including hybrid extended). No legacy RDP
           Security or TLS-without-NLA credential fallback. */
        /* Horizon SOC is IPv4-only and does not support SO_KEEPALIVE;
           FreeRDP otherwise defaults TCP keepalive to enabled. */
        !freerdp_settings_set_uint32(settings, FreeRDP_ForceIPvX, 4) ||
        !freerdp_settings_set_uint32(settings, FreeRDP_TcpConnectTimeout,
                                     (UINT32)opts.timeout_seconds*1000u) ||
        !freerdp_settings_set_bool(settings, FreeRDP_AutoReconnectionEnabled, FALSE) ||
        !freerdp_settings_set_bool(settings, FreeRDP_DisableWallpaper,
                                   opts.low_bandwidth!=0) ||
        !freerdp_settings_set_bool(settings, FreeRDP_TcpKeepAlive, FALSE) ||
        /* FreeRDP defaults multitransport/clipboard on, but our O3DS build
           intentionally has no RDP virtual channels or UDP transport. Stay
           on the tested IPv4 TCP path and do not advertise a dead channel. */
        !freerdp_settings_set_bool(settings, FreeRDP_SupportMultitransport, FALSE) ||
        !freerdp_settings_set_bool(settings, FreeRDP_RedirectClipboard, FALSE) ||
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
    session_cursor_reset(&system_cursor);
    cursor_remote_x=RDP_W/2;cursor_remote_y=RDP_H/2;
    if (!connect_with_ui(client, cfg->host, reason, reason_size)) {
        /* A user cancellation is distinct from an authentication failure.
         * Do not overwrite its reason with a generic FreeRDP error. */
        if (strcmp(reason, "RDP connection cancelled") == 0)
            goto done;
        if (cert_failure[0])
            snprintf(reason, reason_size, "%.90s", cert_failure);
        else if (post_connect_failure[0])
            snprintf(reason, reason_size, "%.90s", post_connect_failure);
        else {
            const UINT code=freerdp_get_last_error(client->context);
            snprintf(reason,reason_size,"%.65s (0x%08lx)",
                     connection_failure_hint(code),(unsigned long)code);
        }
        goto done;
    }
    connected = true;
    session_pad_init(&pad);
    pad.remote_w=remote_w;pad.remote_h=remote_h;
    pad.pointer_x=remote_w/2;pad.pointer_y=remote_h/2;
    pad.sensitivity=opts.touch_speed;pad.scroll_amount=opts.scroll_step;
    pad.reverse_scroll=opts.reverse_scroll!=0;
    cursor_remote_x=pad.pointer_x;cursor_remote_y=pad.pointer_y;
    session_zoom_init(&zoom);
    snprintf(reason, reason_size, "Session ended");
    result = 0;

    while (aptMainLoop() && !freerdp_shall_disconnect_context(client->context)) {
        /* FreeRDP 3.32.2 may append two channel events after the initial
           rdp_get_event_handles count; reserve that overrun margin. */
        HANDLE handles[RDP_MAX_EVENTS + 2];
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
        /* A is reserved for drag. D-pad nudges the remote pointer and thus
           pans the magnified viewport. Y always returns to fit view. */
        if(down & KEY_Y){zoom.target=1.0f;pad_redraw=true;}
        if(down & KEY_R){
            float native_zoom=(float)remote_w/(float)RDP_W;
            float vertical=(float)remote_h/(float)RDP_H;
            if(vertical>native_zoom)native_zoom=vertical;
            if(native_zoom<1.0f)native_zoom=1.0f;
            zoom.target=zoom.target>1.01f?1.0f:native_zoom;
            pad_redraw=true;
        }
        if(input && (held & (KEY_DUP|KEY_DDOWN|KEY_DLEFT|KEY_DRIGHT))){
            int nx=pad.pointer_x+(((held & KEY_DRIGHT)!=0)?8:0)
                                -(((held & KEY_DLEFT)!=0)?8:0);
            int ny=pad.pointer_y+(((held & KEY_DDOWN)!=0)?8:0)
                                -(((held & KEY_DUP)!=0)?8:0);
            if(nx<0)nx=0;if(ny<0)ny=0;
            if(nx>=remote_w)nx=remote_w-1;
            if(ny>=remote_h)ny=remote_h-1;
            if(nx!=pad.pointer_x || ny!=pad.pointer_y){
                pad.pointer_x=nx;pad.pointer_y=ny;
                action.pointer_moved=true;pad_redraw=true;
            }
        }
        if (action.pointer_moved && zoom.visible > 1.001f)
            pad_redraw=true; /* keep viewport tracking the remote cursor */
        if (action.disconnect || (down & KEY_START)) {
            snprintf(reason, reason_size, "Disconnected by user");
            break;
        }

        /* Software keyboard is transient, not a permanent overlay. It never
         * stores entered text and the pad layout returns after dismissal. */
        if (down & KEY_X || action.text_input) {
            /* The software keyboard takes over HID until it closes. Release
               held remote buttons first or a drag can remain stuck while
               HID key-up events are consumed by the keyboard applet. */
            if (input && left_down) {
                (void)freerdp_input_send_mouse_event(input, PTR_FLAGS_BUTTON1,
                        (UINT16)pad.pointer_x, (UINT16)pad.pointer_y);
                left_down = false;
            }
            if (input && right_down) {
                (void)freerdp_input_send_mouse_event(input, PTR_FLAGS_BUTTON2,
                        (UINT16)pad.pointer_x, (UINT16)pad.pointer_y);
                right_down = false;
            }
            if (input && !send_text(client, reason, reason_size)) {
                result = -1;
                break;
            }
            pad_redraw = true;
        }

        if (input && action.key) {
            SessionKeyEvent events[10];
            const size_t n=session_key_sequence(action.key,action.modifiers,events,10);
            for(size_t k=0;k<n;++k) {
                const UINT16 flags=(UINT16)((events[k].extended?KBD_FLAGS_EXTENDED:0) |
                    (events[k].down?0:KBD_FLAGS_RELEASE));
                (void)freerdp_input_send_keyboard_event(input,flags,events[k].scan);
            }
        }
        if (input) {
            if (action.pointer_moved) {
                cursor_remote_x=pad.pointer_x;cursor_remote_y=pad.pointer_y;
                desktop_dirty=true;
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
            snprintf(reason, reason_size, "FreeRDP event count invalid (%lu)",
                     (unsigned long)count);
            result = -1;
            break;
        }
        wait_status = WaitForMultipleObjects(count, handles, FALSE, 20);
        /* WAIT_TIMEOUT is normal with no server activity. Abandoned or
           out-of-range waits indicate a damaged WinPR event lifecycle. */
        if (!rdp_wait_result_valid((uint32_t)wait_status, (uint32_t)count)) {
            snprintf(reason, reason_size, "WinPR wait failed (0x%08lx)",
                     (unsigned long)wait_status);
            result = -1;
            break;
        }
        if (!freerdp_check_event_handles(client->context)) {
            snprintf(reason, reason_size, "RDP event pump error (0x%08lx)",
                     (unsigned long)freerdp_get_last_error(client->context));
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
    /* Do not report an unrequested transport abort as a successful session.
       The abort event is also used by FreeRDP for fatal connection errors. */
    if (connected && freerdp_shall_disconnect_context(client->context)) {
        const UINT err = freerdp_get_last_error(client->context);
        if (err != FREERDP_ERROR_SUCCESS) {
            snprintf(reason, reason_size, "RDP connection ended (0x%08lx)",
                     (unsigned long)err);
            result = -1;
        } else {
            snprintf(reason, reason_size, "Remote desktop disconnected");
        }
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
    if (rng_provider)
        OSSL_PROVIDER_unload(rng_provider);
    OPENSSL_cleanse(&active_pin, sizeof(active_pin));
    OPENSSL_cleanse(&active_pool, sizeof(active_pool));
    OPENSSL_cleanse(requested_host, sizeof(requested_host));
    have_manual_pin = false;
    requested_port = 0;
    OPENSSL_cleanse(cert_failure, sizeof(cert_failure));
    OPENSSL_cleanse(post_connect_failure, sizeof(post_connect_failure));
    return result;
}
#endif
