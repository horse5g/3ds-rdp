#ifndef HORIZON_SYNC_H
#define HORIZON_SYNC_H

/* O3DS/Horizon kernel event primitive; intentionally NOT a POSIX file descriptor. */
#include <3ds.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    HORIZON_WAIT_ERROR = -1,
    HORIZON_WAIT_SIGNALED = 0,
    HORIZON_WAIT_TIMEOUT = 1
} HorizonWaitResult;

typedef struct {
    Handle handle;
    bool valid;
    bool manual_reset;
} HorizonEvent;

/* Returns false on kernel failure. manual_reset uses RESET_STICKY. */
bool horizon_event_create(HorizonEvent *event, bool manual_reset, bool initial_state);
bool horizon_event_signal(HorizonEvent *event);
bool horizon_event_reset(HorizonEvent *event);
/* timeout_ms == UINT32_MAX waits indefinitely; zero polls without blocking. */
HorizonWaitResult horizon_event_wait(HorizonEvent *event, uint32_t timeout_ms);
void horizon_event_destroy(HorizonEvent *event);

/* Hardware smoke test, including cross-thread signal/wait. Returns false on failure.
   This tests the kernel event backend, not yet WinPR/FreeRDP integration. */
bool horizon_sync_selftest(char *message, size_t capacity);

#endif
