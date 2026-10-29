#ifndef LOOPBACK_PAIR_H
#define LOOPBACK_PAIR_H

#include <stdbool.h>
#include <stddef.h>

/* Temporary transport experiment for WinPR's fd-based wait model.
   Pairs are TCP loopback sockets, NOT Unix pipes or kernel events.
   Call socInit() before using this on Nintendo 3DS. */
typedef struct {
    int read_fd;
    int write_fd;
} LoopbackPair;

void loopback_pair_init(LoopbackPair *pair);
bool loopback_pair_open(LoopbackPair *pair, char *error, size_t error_len);
void loopback_pair_close(LoopbackPair *pair);

/* This tests the socket transport and select readiness, not WinPR wait
   behavior. Returns false with a concise diagnostic on failure. */
bool loopback_pair_selftest(char *error, size_t error_len);


/* Stage recorded on loopback-pair creation failure. Numeric only. */
enum {
    LOOPBACK_FAIL_NONE = 0,
    LOOPBACK_FAIL_LISTEN_SOCKET = 1,
    LOOPBACK_FAIL_BIND = 2,
    LOOPBACK_FAIL_LISTEN = 3,
    LOOPBACK_FAIL_GETSOCKNAME = 4,
    LOOPBACK_FAIL_CLIENT_SOCKET = 5,
    LOOPBACK_FAIL_CLIENT_NONBLOCK = 6,
    LOOPBACK_FAIL_CONNECT = 7,
    LOOPBACK_FAIL_CONNECT_TIMEOUT = 8,
    LOOPBACK_FAIL_CONNECT_SO_ERROR = 9,
    LOOPBACK_FAIL_ACCEPT_TIMEOUT = 10,
    LOOPBACK_FAIL_ACCEPT = 11,
    LOOPBACK_FAIL_READER_NONBLOCK = 12
};
typedef struct {
    unsigned long opened;
    unsigned long failed;
    unsigned int last_stage;
    int last_errno;
} LoopbackPairStats;
void loopback_pair_get_stats(LoopbackPairStats *stats);
/* Expensive and opt-in: temporary pairs to estimate available SOC sockets.
 * Not an authoritative socket count; observe only during diagnostics.
 * Probe counters remain included in cumulative stats (no unsafe restore). */
unsigned int loopback_pair_probe_capacity(unsigned int max_pairs,
                                          unsigned int *fail_stage,
                                          int *fail_errno);

#endif
