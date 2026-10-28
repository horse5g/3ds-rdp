#ifndef RDP_CERT_STORE_H
#define RDP_CERT_STORE_H

#include "cert_pool.h"

/* Portable file-backed trust pool. All operations fail closed on malformed
 * stores or interrupted transactions. The caller owns each pool object.
 * Missing store means empty ONLY when no recovery sidecar is present. */
int rdp_cert_store_load(const char *path, RdpCertPool *out);

/* Remove one EXACT expected entry from a validated store. Changes are
 * written to .tmp, checked, then the original is moved to .bak before
 * replacement. A stale .bak always blocks future trust until recovered.
 * Returns 0 for verified success and -1 for no change or any failure.
 * Never replaces a changed entry with a newly observed certificate. */
int rdp_cert_store_remove(const char *path, size_t index,
                          const RdpCertPin *expected);
#endif
