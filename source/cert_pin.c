#include "cert_pin.h"
#include <string.h>

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int rdp_cert_pin_parse(const char *text, RdpCertPin *out)
{
    RdpCertPin tmp = {0};
    unsigned seen = 0;
    if (!text || !out)
        return -1;

    for (const char *p = text; *p;) {
        const char *end = strchr(p, '\n');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        char line[300];
        const char *value;
        if (n && p[n - 1] == '\r')
            n--;
        if (n >= sizeof(line))
            return -1;
        memcpy(line, p, n);
        line[n] = '\0';
        p = end ? end + 1 : p + strlen(p);
        if (n == 0 || line[0] == '#')
            continue;
        if (strncmp(line, "host=", 5) == 0) {
            value = line + 5;
            if ((seen & 1) || !*value || strlen(value) >= sizeof(tmp.host))
                return -1;
            /* No whitespace, Unicode, wildcards or URL-like host syntax. */
            for (const char *s = value; *s; ++s) {
                const char c = *s;
                if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                      (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_'))
                    return -1;
            }
            memcpy(tmp.host, value, strlen(value) + 1);
            seen |= 1;
        } else if (strncmp(line, "port=", 5) == 0) {
            unsigned long port = 0;
            value = line + 5;
            if ((seen & 2) || !*value)
                return -1;
            for (const char *s = value; *s; ++s) {
                if (*s < '0' || *s > '9')
                    return -1;
                port = port * 10 + (unsigned)(*s - '0');
                if (port > 65535)
                    return -1;
            }
            if (port == 0)
                return -1;
            tmp.port = (uint16_t)port;
            seen |= 2;
        } else if (strncmp(line, "sha256=", 7) == 0) {
            value = line + 7;
            if ((seen & 4) || strlen(value) != 64)
                return -1;
            for (size_t i = 0; i < 32; ++i) {
                const int hi = hex_value(value[i * 2]);
                const int lo = hex_value(value[i * 2 + 1]);
                if (hi < 0 || lo < 0)
                    return -1;
                tmp.sha256[i] = (unsigned char)((hi << 4) | lo);
            }
            seen |= 4;
        } else {
            return -1;
        }
    }
    if (seen != 7)
        return -1;
    *out = tmp;
    return 0;
}

int rdp_cert_host_equal(const char *a, const char *b)
{
    if (!a || !b) return 0;
    while (*a && *b) {
        unsigned char ca=(unsigned char)*a++;
        unsigned char cb=(unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z') ca=(unsigned char)(ca-'A'+'a');
        if (cb >= 'A' && cb <= 'Z') cb=(unsigned char)(cb-'A'+'a');
        if (ca != cb) return 0;
    }
    return *a == 0 && *b == 0;
}
int rdp_cert_pin_matches(const RdpCertPin *pin, const char *host, uint16_t port)
{
    return pin && host && pin->port == port && rdp_cert_host_equal(pin->host,host);
}
