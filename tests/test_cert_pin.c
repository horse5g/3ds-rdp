#include "cert_pin.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char valid[] =
    "# Manually verified SHA256 of DER leaf certificate\n"
    "host=rdp.example.test\n"
    "port=3389\n"
    "sha256=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n";

static void rejects(const char *s)
{
    RdpCertPin pin;
    memset(&pin, 0xA5, sizeof(pin));
    assert(rdp_cert_pin_parse(s, &pin) != 0);
    assert(((unsigned char *)&pin)[0] == 0xA5);
}

int main(void)
{
    RdpCertPin pin = {0};
    assert(rdp_cert_pin_parse(valid, &pin) == 0);
    assert(strcmp(pin.host, "rdp.example.test") == 0);
    assert(pin.port == 3389);
    assert(pin.sha256[0] == 0x01 && pin.sha256[31] == 0xef);
    assert(rdp_cert_pin_matches(&pin, "rdp.example.test", 3389));
    assert(!rdp_cert_pin_matches(&pin, "RDp.example.test", 3389));
    assert(!rdp_cert_pin_matches(&pin, "rdp.example.test", 3390));
    assert(!rdp_cert_pin_matches(&pin, "rdp.evil.test", 3389));
    rejects("host=rdp.example.test\nport=3389\n");
    rejects("host=rdp.example.test\nport=3389\nsha256=xyz\n");
    rejects("host=rdp.example.test\nport=3389\nsha256=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdeg\n");
    rejects("host=rdp.example.test\nport=3389\nsha256=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\nport=3389\n");
    rejects("host=rdp.example.test\nport=0\nsha256=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n");
    rejects("host=rdp.example.test\nport=65536\nsha256=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n");
    rejects("host=rdp.example.test\nport=+3389\nsha256=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n");
    rejects("host=rdp.example.test\nport=3389\nsha256=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\nignore_verify=1\n");
    rejects("host=*.example.test\nport=3389\nsha256=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n");
    rejects("host=rdp.example.test:3389\nport=3389\nsha256=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n");
    puts("cert pin parser tests passed");
    return 0;
}
