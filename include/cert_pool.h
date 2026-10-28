#ifndef RDP_CERT_POOL_H
#define RDP_CERT_POOL_H

#include <stddef.h>
#include "cert_pin.h"

/* Small append-only trust store. One trust decision per host:port. */
#define RDP_CERT_POOL_MAX 32
#define RDP_CERT_POOL_LINE_MAX 400
typedef struct {
    RdpCertPin entries[RDP_CERT_POOL_MAX];
    size_t count;
} RdpCertPool;

/* All-or-nothing parser; reject corruption, duplicate endpoints, malformed
 * hashes, oversize records and excess entries rather than trusting partially. */
int rdp_cert_pool_parse(const char *text, RdpCertPool *out);
/* 1=trusted leaf, 0=new endpoint, -1=changed leaf */
int rdp_cert_pool_lookup(const RdpCertPool *pool, const char *host,
                         uint16_t port, const unsigned char sha256[32]);
/* 1=added, 0=already present, -1=changed/invalid/full */
int rdp_cert_pool_add(RdpCertPool *pool, const RdpCertPin *pin);
/* Returns number of characters (excluding NUL), or -1 if buffer insufficient.
 * One line: host<TAB>decimal-port<TAB>64-hex-sha256<LF>. */
int rdp_cert_pool_format(const RdpCertPin *pin, char *buffer, size_t size);
#endif
