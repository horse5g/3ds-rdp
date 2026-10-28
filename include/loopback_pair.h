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

#endif
