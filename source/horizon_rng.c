#include "horizon_rng.h"
#include <3ds.h>
#include <stdio.h>
#include <string.h>

static void clear_secret(void *ptr, size_t size)
{
    volatile unsigned char *p = (volatile unsigned char *)ptr;
    while (size--)
        *p++ = 0;
}

bool horizon_rng_selftest(char *message, size_t capacity)
{
    u8 first[32] = { 0 }, second[32] = { 0 }, zero[32] = { 0 };
    Result rc = psInit();
    bool ok = false;

    if (rc != 0) {
        if (message && capacity)
            snprintf(message, capacity, "psInit: 0x%08lX", (unsigned long)rc);
        goto finish;
    }

    rc = PS_GenerateRandomBytes(first, sizeof(first));
    if (rc != 0) {
        if (message && capacity)
            snprintf(message, capacity, "PS draw 1: 0x%08lX", (unsigned long)rc);
        goto cleanup_service;
    }

    rc = PS_GenerateRandomBytes(second, sizeof(second));
    if (rc != 0) {
        if (message && capacity)
            snprintf(message, capacity, "PS draw 2: 0x%08lX", (unsigned long)rc);
        goto cleanup_service;
    }

    if (!memcmp(first, zero, sizeof(first)) ||
        !memcmp(second, zero, sizeof(second)) ||
        !memcmp(first, second, sizeof(first))) {
        if (message && capacity)
            snprintf(message, capacity, "Repeated/zero output; investigate PS");
        goto cleanup_service;
    }

    ok = true;
    if (message && capacity)
        snprintf(message, capacity, "PS service + 2 draws PASS");

cleanup_service:
    psExit();
finish:
    clear_secret(first, sizeof(first));
    clear_secret(second, sizeof(second));
    return ok;
}
