#include "horizon_sync.h"
#include <stdio.h>
#include <string.h>

static bool valid_event(const HorizonEvent *event) {
    return event && event->valid;
}

bool horizon_event_create(HorizonEvent *event, bool manual_reset, bool initial_state) {
    Result rc;
    if (!event) return false;
    memset(event, 0, sizeof(*event));
    event->manual_reset = manual_reset;
    rc = svcCreateEvent(&event->handle, manual_reset ? RESET_STICKY : RESET_ONESHOT);
    if (R_FAILED(rc)) return false;
    event->valid = true;
    if (initial_state && !horizon_event_signal(event)) {
        horizon_event_destroy(event);
        return false;
    }
    return true;
}

bool horizon_event_signal(HorizonEvent *event) {
    return valid_event(event) && R_SUCCEEDED(svcSignalEvent(event->handle));
}

bool horizon_event_reset(HorizonEvent *event) {
    return valid_event(event) && R_SUCCEEDED(svcClearEvent(event->handle));
}

HorizonWaitResult horizon_event_wait(HorizonEvent *event, uint32_t timeout_ms) {
    Result rc;
    s64 nanoseconds;
    if (!valid_event(event)) return HORIZON_WAIT_ERROR;
    nanoseconds = (timeout_ms == UINT32_MAX) ? -1 : (s64)timeout_ms * 1000000LL;
    rc = svcWaitSynchronization(event->handle, nanoseconds);
    if (R_SUCCEEDED(rc)) return HORIZON_WAIT_SIGNALED;
    if (R_DESCRIPTION(rc) == RD_TIMEOUT) return HORIZON_WAIT_TIMEOUT;
    return HORIZON_WAIT_ERROR;
}

void horizon_event_destroy(HorizonEvent *event) {
    if (!event) return;
    if (event->valid) svcCloseHandle(event->handle);
    event->handle = 0;
    event->valid = false;
    event->manual_reset = false;
}

static void set_message(char *buffer, size_t capacity, const char *text) {
    if (buffer && capacity) snprintf(buffer, capacity, "%s", text);
}

static void delayed_signal(void *ctx) {
    HorizonEvent *event = (HorizonEvent *)ctx;
    svcSleepThread(10 * 1000000LL);
    (void)horizon_event_signal(event);
}

bool horizon_sync_selftest(char *message, size_t capacity) {
    HorizonEvent auto_event = {0}, manual_event = {0};
    Thread worker = NULL;
    bool ok = false;
    set_message(message, capacity, "Initializing kernel events");

    if (!horizon_event_create(&auto_event, false, false) ||
        !horizon_event_create(&manual_event, true, false)) {
        set_message(message, capacity, "svcCreateEvent failed");
        goto cleanup;
    }
    if (horizon_event_wait(&auto_event, 0) != HORIZON_WAIT_TIMEOUT ||
        horizon_event_wait(&manual_event, 0) != HORIZON_WAIT_TIMEOUT) {
        set_message(message, capacity, "Unsignaled event failed to time out");
        goto cleanup;
    }
    if (!horizon_event_signal(&auto_event) ||
        horizon_event_wait(&auto_event, 0) != HORIZON_WAIT_SIGNALED ||
        horizon_event_wait(&auto_event, 0) != HORIZON_WAIT_TIMEOUT) {
        set_message(message, capacity, "Auto-reset event failed");
        goto cleanup;
    }
    if (!horizon_event_signal(&manual_event) ||
        horizon_event_wait(&manual_event, 0) != HORIZON_WAIT_SIGNALED ||
        horizon_event_wait(&manual_event, 0) != HORIZON_WAIT_SIGNALED ||
        !horizon_event_reset(&manual_event) ||
        horizon_event_wait(&manual_event, 0) != HORIZON_WAIT_TIMEOUT) {
        set_message(message, capacity, "Manual-reset event failed");
        goto cleanup;
    }

    /* Cross-thread handoff: the kernel must unblock a waiter, not just
       report an event that this same thread already signaled. */
    worker = threadCreate(delayed_signal, &auto_event, 16 * 1024, 0x30, -1, false);
    if (!worker) {
        set_message(message, capacity, "Could not create test thread");
        goto cleanup;
    }
    if (horizon_event_wait(&auto_event, 500) != HORIZON_WAIT_SIGNALED) {
        set_message(message, capacity, "Cross-thread signal timed out");
        goto cleanup;
    }
    ok = true;
    set_message(message, capacity, "Auto/manual + threaded wait PASS");

cleanup:
    /* A worker may still reference auto_event; always join before closing. */
    if (worker) {
        Result rc = threadJoin(worker, UINT64_MAX);
        if (R_FAILED(rc)) {
            ok = false;
            set_message(message, capacity, "Could not join test thread");
        }
        threadFree(worker);
    }
    horizon_event_destroy(&manual_event);
    horizon_event_destroy(&auto_event);
    return ok;
}
