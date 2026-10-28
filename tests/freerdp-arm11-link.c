/*
 * Combined ARM11 linker probe: FreeRDP core + WinPR + OpenSSL + libctru.
 * An ELF/.3dsx build proves resolution of symbols only, not an RDP session.
 * In particular, it does not establish TLS verification or hardware support.
 */
#include <3ds.h>
#include <freerdp/freerdp.h>
#include <freerdp/settings.h>
#include <openssl/ssl.h>
#include <openssl/rand.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    freerdp *client = NULL;
    SSL_CTX *tls = NULL;
    int rc = 1;

    gfxInitDefault();
    consoleInit(GFX_TOP, NULL);
    puts("FreeRDP / OpenSSL ARM11 linker probe");

    tls = SSL_CTX_new(TLS_client_method());
    if (!tls) {
        puts("SSL_CTX_new failed");
        goto cleanup;
    }

    client = freerdp_new();
    if (!client || !freerdp_context_new(client)) {
        puts("FreeRDP initialization failed");
        goto cleanup;
    }

    /* Exercise the generated settings APIs and pull core symbols into
       the final link; never persist or send credentials from this probe. */
    if (!freerdp_settings_set_string(client->context->settings,
                                     FreeRDP_ServerHostname, "127.0.0.1") ||
        !freerdp_settings_set_uint32(client->context->settings,
                                     FreeRDP_ServerPort, 3389)) {
        puts("FreeRDP settings failed");
        goto cleanup;
    }

    /* Deliberately unreachable in the normal homebrew launcher invocation.
       Keep the connect symbol in the link so its transitive dependencies
       (TLS, NLA and WinPR transport) are checked without touching network. */
    if (argc == 2 && argv && argv[1] &&
        strcmp(argv[1], "--link-probe-connect") == 0) {
        (void)freerdp_connect(client);
        freerdp_disconnect(client);
    }

    puts("Combined linker probe initialized.");
    puts("This is NOT a validated remote desktop.");
    rc = 0;
cleanup:
    if (client) {
        if (client->context)
            freerdp_context_free(client);
        freerdp_free(client);
    }
    SSL_CTX_free(tls);
    gfxExit();
    return rc;
}
