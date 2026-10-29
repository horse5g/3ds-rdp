/*
 * Standalone hardware smoke test for ARM11 OpenSSL 3.5.
 * NOT an RDP client and NOT a certificate/TLS security validation.
 * Does not use network services or store/display random bytes.
 */
#include <3ds.h>
#include <openssl/ssl.h>
#include <openssl/rand.h>
#include <openssl/provider.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/crypto.h>
#include "rdp_ps_rng_diag.h"
#include <stdio.h>
#include <string.h>

static void show_error(const char *what)
{
    unsigned long error = ERR_get_error();
    printf("%s failed (OpenSSL 0x%08lX)\n", what, error);
}

int main(void)
{
    unsigned char first[32] = {0}, second[32] = {0}, zeros[32] = {0};
    SSL_CTX *ctx = NULL;
    OSSL_PROVIDER *default_provider = NULL;
    EVP_RAND_CTX *primary = NULL, *public_rng = NULL;
    EVP_RAND *seed_source = NULL;
    EVP_RAND_CTX *seed_test = NULL;
    bool seed_ok = false;
    bool random_ok = false;
    bool ssl_ok = false;

    gfxInitDefault();
    consoleInit(GFX_TOP, NULL);
    printf("OpenSSL ARM11 hardware smoke test\n");
    printf("===============================\n\n");

    /* Isolate PS-backed SEED-SRC from CTR-DRBG and its thread-local RNGs.
       This is a diagnostic, not a substitute for the standard RAND_bytes. */
    (void)OPENSSL_init_crypto(OPENSSL_INIT_NO_LOAD_CONFIG, NULL);
    default_provider = OSSL_PROVIDER_load(NULL, "default");
    if (default_provider) {
        ERR_clear_error();
        seed_source = EVP_RAND_fetch(NULL, "SEED-SRC", NULL);
        if (!seed_source)
            show_error("SEED-SRC fetch");
        else if ((seed_test = EVP_RAND_CTX_new(seed_source, NULL)) == NULL)
            show_error("SEED-SRC context");
        else if (!EVP_RAND_instantiate(seed_test, 0, 0, NULL, 0, NULL))
            show_error("SEED-SRC instantiate");
        else if (!EVP_RAND_generate(seed_test, first, sizeof(first),
                                    256, 0, NULL, 0))
            show_error("SEED-SRC draw");
        else {
            seed_ok = true;
            puts("PS-backed SEED-SRC draw PASS");
        }
        EVP_RAND_CTX_free(seed_test);
        EVP_RAND_free(seed_source);
        OPENSSL_cleanse(first, sizeof(first));
        ERR_clear_error();
    }

    /* Display which OpenSSL 3.5 provider/DRBG layer fails on O3DS,
       without printing or storing the generated random bytes. */
    if (!default_provider) {
        show_error("default provider");
    } else if ((primary = RAND_get0_primary(NULL)) == NULL) {
        show_error("primary DRBG");
    } else if (EVP_RAND_get_state(primary) != EVP_RAND_STATE_READY) {
        show_error("primary state");
    } else if ((public_rng = RAND_get0_public(NULL)) == NULL) {
        show_error("public DRBG");
    } else if (EVP_RAND_get_state(public_rng) != EVP_RAND_STATE_READY) {
        show_error("public state");
    } else if (RAND_bytes(first, sizeof(first)) != 1 ||
               RAND_bytes(second, sizeof(second)) != 1) {
        show_error("RAND_bytes");
    } else if (!memcmp(first, zeros, sizeof(first)) ||
               !memcmp(second, zeros, sizeof(second)) ||
               !memcmp(first, second, sizeof(first))) {
        puts("RAND bytes repeated/zero: FAIL");
    } else {
        random_ok = true;
        puts("RAND_bytes/PS entropy smoke PASS");
    }

    OPENSSL_cleanse(first, sizeof(first));
    OPENSSL_cleanse(second, sizeof(second));

    ctx = SSL_CTX_new(TLS_client_method());
    if (ctx) {
        ssl_ok = true;
        puts("SSL_CTX creation PASS (no handshake)");
        SSL_CTX_free(ctx);
    } else {
        show_error("SSL_CTX_new");
    }

    /* Independent instrumentation from our patched Horizon PS backend.
       Counts/stage codes only: neither entropy nor any bytes are exposed. */
    RdpPsRngDiag diag = {0};
    ossl_3ds_ps_rng_snapshot(&diag);
    printf("PS entropy OK/calls: %u/%u\n",
           diag.entropy_successes, diag.entropy_calls);
    printf("PS nonce OK/calls:   %u/%u\n",
           diag.nonce_successes, diag.nonce_calls);
    printf("PS last fail stage: %u\n", diag.last_failure_stage);
    printf("PS last Result: %08X\n", diag.last_ps_result);
    printf("PS entropy bytes: %u nonce cap: %u\n",
           diag.last_entropy_request, diag.last_nonce_capacity);
    printf("SEED-SRC: %s\n", seed_ok ? "PASS" : "FAIL / UNAVAILABLE");
    puts("");
    printf("Summary: %s\n", random_ok && ssl_ok ? "PASS" : "FAIL");
    puts("No network, trust-store, or NLA test.");
    if (default_provider) OSSL_PROVIDER_unload(default_provider);
    puts("START: Exit");

    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START)
            break;
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
    gfxExit();
    return random_ok && ssl_ok ? 0 : 1;
}
