#include "cert_pool.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char *sample =
    "# 3DS known RDP certificates\n"
    "rdp.example.test\t3389\t0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n"
    "10.1.2.3\t3390\taaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\r\n";

static void invalid(const char *input)
{
    RdpCertPool pool;
    memset(&pool, 0xA5, sizeof(pool));
    assert(rdp_cert_pool_parse(input, &pool) != 0);
    assert(((unsigned char *)&pool)[0] == 0xA5);
}

int main(void)
{
    RdpCertPool pool = {0}, restored = {0};
    RdpCertPin pin = {0};
    char line[400];
    int n;

    assert(rdp_cert_pool_parse(sample, &pool) == 0);
    assert(pool.count == 2);
    assert(rdp_cert_pool_lookup(&pool, "rdp.example.test", 3389,
                                pool.entries[0].sha256) == 1);
    assert(rdp_cert_pool_lookup(&pool, "rdp.example.test", 3389,
                                pool.entries[1].sha256) == -1);
    assert(rdp_cert_pool_lookup(&pool, "new.example.test", 3389,
                                pool.entries[0].sha256) == 0);
    assert(rdp_cert_pool_lookup(&pool, "rdp.example.test", 3390,
                                pool.entries[0].sha256) == 0);
    pin = pool.entries[0];
    assert(rdp_cert_pool_add(&pool, &pin) == 0);
    pin.sha256[0] ^= 1;
    assert(rdp_cert_pool_add(&pool, &pin) == -1);
    assert(pool.count == 2);
    pin = pool.entries[1];
    n = rdp_cert_pool_format(&pin, line, sizeof(line));
    assert(n > 0 && (size_t)n == strlen(line));
    assert(rdp_cert_pool_parse(line, &restored) == 0 && restored.count == 1);
    assert(rdp_cert_pool_lookup(&restored, pin.host, pin.port, pin.sha256) == 1);
    assert(rdp_cert_pool_format(&pin, line, 2) == -1);

    invalid("rdp.example.test\t3389\tbad\n");
    invalid("rdp.example.test\t3389\t0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\tjunk\n");
    invalid("rdp.example.test\t3389\t0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n"
            "rdp.example.test\t3389\t0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n");
    invalid("rdp.example.test\t3389\t0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n"
            "rdp.example.test\t3389\taaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n");
    /* DNS case folding must not permit duplicate or changed-certificate pins. */
    invalid("rdp.example.test\\t3389\\t0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\\n"
            "RDP.EXAMPLE.TEST\\t3389\\t0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\\n");
    invalid("rdp.example.test\\t3389\\t0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\\n"
            "RDP.EXAMPLE.TEST\\t3389\\taaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\\n");
    invalid("rdp.example.test\t0\t0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n");
    invalid("rdp.example.test\t65536\t0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n");
    invalid("evil\nhost\t3389\t0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n");
    invalid("rdp.example.test\t3389\t0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\ntruncated");
    puts("certificate pool tests passed");
    return 0;
}
