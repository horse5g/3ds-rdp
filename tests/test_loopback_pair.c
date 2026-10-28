#include "loopback_pair.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    LoopbackPair pair;
    char error[128];
    loopback_pair_init(&pair);
    assert(pair.read_fd == -1 && pair.write_fd == -1);
    assert(loopback_pair_selftest(error, sizeof(error)) && "Loopback pair failed");
    assert(loopback_pair_open(&pair, error, sizeof(error)));
    assert(pair.read_fd >= 0 && pair.write_fd >= 0);
    loopback_pair_close(&pair);
    assert(pair.read_fd == -1 && pair.write_fd == -1);
    printf("%s; lifecycle PASS\n", error);
    return 0;
}
