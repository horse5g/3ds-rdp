/*
 * Build/link-only probe for native OpenSSL dependencies on Horizon OS.
 * This is NOT executed by CI and does not attempt a network connection.
 * It does not demonstrate secure random seeding or certificate validation.
 */
#include <3ds.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509.h>

int main(void)
{
    gfxInitDefault();

    /* Pull the actual SSL, provider and X.509 code into the link,
       rather than just confirming that empty archives exist. */
    SSL_CTX *context = SSL_CTX_new(TLS_client_method());
    if (context) {
        X509_STORE *store = SSL_CTX_get_cert_store(context);
        if (store)
            (void)X509_STORE_get0_objects(store);
        SSL_CTX_free(context);
    } else {
        (void)ERR_get_error();
    }

    gfxExit();
    return 0;
}
