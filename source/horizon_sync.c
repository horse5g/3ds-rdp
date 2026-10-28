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

/* svcWaitSynchronization* returns 0x09401BFE on timeout, which has an
 * informational (nonnegative) result level. R_SUCCEEDED(timeout) is TRUE!
 * Check timeout before considering an event signaled. */
static HorizonWaitResult classify_wait_result(Result rc) {
    if (R_DESCRIPTION(rc) == RD_TIMEOUT)
        return HORIZON_WAIT_TIMEOUT;
    if (rc == 0)
        return HORIZON_WAIT_SIGNALED;
    return HORIZON_WAIT_ERROR;
}

HorizonWaitResult horizon_event_wait(HorizonEvent *event, uint32_t timeout_ms) {
    Result rc;
    s64 nanoseconds;
    if (!valid_event(event)) return HORIZON_WAIT_ERROR;
    nanoseconds = (timeout_ms == UINT32_MAX) ? -1 : (s64)timeout_ms * 1000000LL;
    rc = svcWaitSynchronization(event->handle, nanoseconds);
    return classify_wait_result(rc);
}

HorizonWaitResult horizon_events_wait_any(HorizonEvent *const *events, size_t count,
                                          uint32_t timeout_ms, size_t *signaled_index) {
    Handle handles[16];
    s32 selected = -1;
    s64 nanoseconds;
    Result rc;
    size_t i;
    if (!events || !signaled_index || !count || count > 16) return HORIZON_WAIT_ERROR;
    for (i = 0; i < count; ++i) {
        if (!valid_event(events[i])) return HORIZON_WAIT_ERROR;
        handles[i] = events[i]->handle;
    }
    *signaled_index = SIZE_MAX;
    nanoseconds = (timeout_ms == UINT32_MAX) ? -1 : (s64)timeout_ms * 1000000LL;
    rc = svcWaitSynchronizationN(&selected, handles, (s32)count, false, nanoseconds);
    const HorizonWaitResult wait = classify_wait_result(rc);
    if (wait == HORIZON_WAIT_SIGNALED) {
        if (selected < 0 || (size_t)selected >= count) return HORIZON_WAIT_ERROR;
        *signaled_index = (size_t)selected;
    }
    return wait;
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
    {
        const HorizonWaitResult auto_wait = horizon_event_wait(&auto_event, 0);
        const HorizonWaitResult manual_wait = horizon_event_wait(&manual_event, 0);
        if (auto_wait != HORIZON_WAIT_TIMEOUT || manual_wait != HORIZON_WAIT_TIMEOUT) {
            if (message && capacity)
                snprintf(message, capacity, "Initial waits: auto=%d sticky=%d",
                         (int)auto_wait, (int)manual_wait);
            goto cleanup;
        }
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

    {
        HorizonEvent *watch[2] = { &auto_event, &manual_event };
        size_t selected = SIZE_MAX;
        if (horizon_events_wait_any(watch, 2, 0, &selected) != HORIZON_WAIT_TIMEOUT ||
            !horizon_event_signal(&manual_event) ||
            horizon_events_wait_any(watch, 2, 0, &selected) != HORIZON_WAIT_SIGNALED ||
            selected != 1 ||
            !horizon_event_reset(&manual_event) ||
            !horizon_event_signal(&auto_event) ||
            horizon_events_wait_any(watch, 2, 0, &selected) != HORIZON_WAIT_SIGNALED ||
            selected != 0 ||
            horizon_events_wait_any(watch, 2, 0, &selected) != HORIZON_WAIT_TIMEOUT) {
            set_message(message, capacity, "Multi-event wait failed");
            goto cleanup;
        }
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
    set_message(message, capacity, "Single/multi + threaded wait PASS");

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
