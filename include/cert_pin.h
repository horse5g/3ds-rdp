#ifndef RDP_CERT_PIN_H
#define RDP_CERT_PIN_H
#include <stddef.h>
#include <stdint.h>

/* One manually verified certificate fingerprint, bound to a single endpoint.
 * SHA-256 of the DER-encoded leaf X.509 certificate, not of the PEM text.
 * This is NOT TOFU; the trusted fingerprint must be provisioned out-of-band.
 */
typedef struct {
    char host[256];
    uint16_t port;
    unsigned char sha256[32];
} RdpCertPin;

/* Strict input, exactly one host=, port=, sha256=; blank/comment lines allowed.
 * Reject extra keys, duplicate keys, oversized lines, non-hex hashes.
 */
int rdp_cert_pin_parse(const char *text, RdpCertPin *out);
int rdp_cert_pin_matches(const RdpCertPin *pin, const char *host, uint16_t port);
#endif
