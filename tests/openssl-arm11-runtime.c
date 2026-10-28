/*
 * Standalone hardware smoke test for ARM11 OpenSSL 3.5.
 * NOT an RDP client and NOT a certificate/TLS security validation.
 * Does not use network services or store/display random bytes.
 */
#include <3ds.h>
#include <openssl/ssl.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <openssl/crypto.h>
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
    bool random_ok = false;
    bool ssl_ok = false;

    gfxInitDefault();
    consoleInit(GFX_TOP, NULL);
    printf("OpenSSL ARM11 hardware smoke test\n");
    printf("===============================\n\n");

    /* Calls OpenSSL's default DRBG. The 3DS build must route its
       seeding to PS_GenerateRandomBytes, and fail on PS service errors. */
    if (RAND_bytes(first, sizeof(first)) != 1 ||
        RAND_bytes(second, sizeof(second)) != 1) {
        show_error("RAND_bytes");
    } else if (!memcmp(first, zeros, sizeof(first)) ||
               !memcmp(second, zeros, sizeof(second)) ||
               !memcmp(first, second, sizeof(first))) {
        puts("RAND bytes repeated or all zero: FAIL");
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

    puts("");
    printf("Summary: %s\n", random_ok && ssl_ok ? "PASS" : "FAIL");
    puts("No network, trust-store, or NLA test.");
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
