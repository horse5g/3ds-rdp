#include "cert_pool.h"
#include <stdio.h>
#include <string.h>

static int valid_pin(const RdpCertPin *pin)
{
    char stanza[400];
    RdpCertPin parsed;
    char hash[65];
    static const char hex[] = "0123456789abcdef";
    if (!pin || !pin->host[0] || !pin->port ||
        !memchr(pin->host, 0, sizeof(pin->host)))
        return 0;
    for (size_t i = 0; i < 32; i++) {
        hash[2 * i] = hex[pin->sha256[i] >> 4];
        hash[2 * i + 1] = hex[pin->sha256[i] & 15];
    }
    hash[64] = 0;
    const int n = snprintf(stanza, sizeof(stanza),
                           "host=%s\nport=%u\nsha256=%s\n",
                           pin->host, (unsigned)pin->port, hash);
    return n > 0 && (size_t)n < sizeof(stanza) &&
           rdp_cert_pin_parse(stanza, &parsed) == 0;
}

int rdp_cert_pool_lookup(const RdpCertPool *pool, const char *host,
                         uint16_t port, const unsigned char sha256[32])
{
    if (!pool || !host || !sha256 || pool->count > RDP_CERT_POOL_MAX)
        return -1;
    for (size_t i = 0; i < pool->count; i++) {
        const RdpCertPin *p = &pool->entries[i];
        if (rdp_cert_pin_matches(p, host, port)) {
            unsigned difference = 0;
            for (size_t k = 0; k < 32; ++k)
                difference |= (unsigned)(p->sha256[k] ^ sha256[k]);
            return difference ? -1 : 1;
        }
    }
    return 0;
}

int rdp_cert_pool_add(RdpCertPool *pool, const RdpCertPin *pin)
{
    int previous;
    if (!pool || !valid_pin(pin) || pool->count > RDP_CERT_POOL_MAX)
        return -1;
    previous = rdp_cert_pool_lookup(pool, pin->host, pin->port, pin->sha256);
    if (previous)
        return previous > 0 ? 0 : -1;
    if (pool->count >= RDP_CERT_POOL_MAX)
        return -1;
    pool->entries[pool->count++] = *pin;
    return 1;
}

int rdp_cert_pool_format(const RdpCertPin *pin, char *buffer, size_t size)
{
    static const char hex[] = "0123456789abcdef";
    char hash[65];
    if (!buffer || !size || !valid_pin(pin))
        return -1;
    for (size_t i = 0; i < 32; ++i) {
        hash[2 * i] = hex[pin->sha256[i] >> 4];
        hash[2 * i + 1] = hex[pin->sha256[i] & 15];
    }
    hash[64] = 0;
    const int n = snprintf(buffer, size, "%s\t%u\t%s\n",
                           pin->host, (unsigned)pin->port, hash);
    return n >= 0 && (size_t)n < size ? n : -1;
}

int rdp_cert_pool_parse(const char *text, RdpCertPool *out)
{
    RdpCertPool tmp = {0};
    if (!text || !out)
        return -1;
    for (const char *p = text; *p;) {
        const char *end = strchr(p, '\n');
        size_t size = end ? (size_t)(end - p) : strlen(p);
        char line[RDP_CERT_POOL_LINE_MAX], stanza[400];
        char *first, *second;
        RdpCertPin pin;
        if (size && p[size - 1] == '\r')
            size--;
        if (size >= sizeof(line))
            return -1;
        memcpy(line, p, size);
        line[size] = 0;
        p = end ? end + 1 : p + strlen(p);
        if (!size || line[0] == '#')
            continue;
        first = strchr(line, '\t');
        if (!first)
            return -1;
        *first++ = 0;
        second = strchr(first, '\t');
        if (!second)
            return -1;
        *second++ = 0;
        if (strchr(second, '\t'))
            return -1;
        int n = snprintf(stanza, sizeof(stanza),
                         "host=%s\nport=%s\nsha256=%s\n",
                         line, first, second);
        if (n < 0 || (size_t)n >= sizeof(stanza) ||
            rdp_cert_pin_parse(stanza, &pin) != 0 ||
            rdp_cert_pool_add(&tmp, &pin) != 1)
            return -1;
    }
    *out = tmp;
    return 0;
}
