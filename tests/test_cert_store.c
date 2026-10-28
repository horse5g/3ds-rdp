#include "cert_store.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define PATH "test-cert-store-fixture.txt"
#define TMP PATH ".tmp"
#define BAK PATH ".bak"

static void clear_files(void)
{
    remove(PATH);
    remove(TMP);
    remove(BAK);
}

static void write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    assert(f);
    assert(fwrite(text, 1, strlen(text), f) == strlen(text));
    assert(fclose(f) == 0);
}

int main(void)
{
    RdpCertPool pool = {0};
    RdpCertPin chosen = {0};
    const char *original =
        "rdp.example.test\t3389\t0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n"
        "10.1.2.3\t3390\taaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n";
    clear_files();

    /* Absent store is harmlessly empty, but interrupted rewrite is NOT. */
    assert(rdp_cert_store_load(PATH, &pool) == 0 && pool.count == 0);
    write_text(TMP, "incomplete");
    assert(rdp_cert_store_load(PATH, &pool) != 0);
    remove(TMP);
    write_text(BAK, original);
    assert(rdp_cert_store_load(PATH, &pool) != 0);
    remove(BAK);

    write_text(PATH, original);
    assert(rdp_cert_store_load(PATH, &pool) == 0 && pool.count == 2);
    chosen = pool.entries[1];
    assert(rdp_cert_store_remove(PATH, 99, &chosen) != 0);
    chosen.sha256[0] ^= 1;
    assert(rdp_cert_store_remove(PATH, 1, &chosen) != 0);
    chosen.sha256[0] ^= 1;
    assert(rdp_cert_store_remove(PATH, 0, &chosen) != 0);
    assert(rdp_cert_store_remove(PATH, 1, &chosen) == 0);
    assert(rdp_cert_store_load(PATH, &pool) == 0 && pool.count == 1);
    assert(rdp_cert_pool_lookup(&pool, "10.1.2.3", 3390, chosen.sha256) == 0);
    assert(rdp_cert_pool_lookup(&pool, "rdp.example.test", 3389,
                                pool.entries[0].sha256) == 1);
    assert(rdp_cert_store_remove(PATH, 0, &pool.entries[0]) == 0);
    assert(rdp_cert_store_load(PATH, &pool) == 0 && pool.count == 0);
    assert(rdp_cert_store_remove(PATH, 0, &chosen) != 0);

    write_text(PATH, "malformed\n");
    assert(rdp_cert_store_load(PATH, &pool) != 0);
    assert(rdp_cert_store_remove(PATH, 0, &chosen) != 0);

    clear_files();
    puts("certificate store removal tests passed");
    return 0;
}
