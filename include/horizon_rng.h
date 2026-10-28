#ifndef HORIZON_RNG_H
#define HORIZON_RNG_H
#include <stdbool.h>
#include <stddef.h>

/* Hardware service-availability test only. Does not establish OpenSSL DRBG
 * integration, long-term entropy quality, or secure session readiness. */
bool horizon_rng_selftest(char *message, size_t capacity);

#endif
