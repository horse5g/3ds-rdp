/*
 * Opt-in SD diagnostics for 3ds-rdp.
 * A pre-existing rdplog.txt is both the enable flag and the output.
 * Absent marker => no file creation, no writes, no startup logging.
 * Every event is flushed before returning so crashes retain breadcrumbs.
 */
#include "rdp_diag_log.h"
#include <3ds/synchronization.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#define RDP_LOG_PATH "sdmc:/3ds/3ds-rdp/rdplog.txt"
#define RDP_LOG_MAX_BYTES (128L * 1024L)
#define RDP_LOG_EVENT_MAX 50u

static LightLock log_lock;
static bool log_enabled;
static unsigned long log_sequence;

static void emit_locked(const char *event, unsigned long a,
                        unsigned long b, unsigned long c)
{
    FILE *f;
    long size;
    if (!log_enabled || !event)
        return;
    /* rb+ cannot create the file if it was deleted after startup. */
    f = fopen(RDP_LOG_PATH, "rb+");
    if (!f) {
        log_enabled = false;
        return;
    }
    if (fseek(f, 0L, SEEK_END) != 0 || (size = ftell(f)) < 0 ||
        size >= RDP_LOG_MAX_BYTES) {
        fclose(f);
        return;
    }
    /* Do not allow unbounded/freeform strings from remote endpoints. */
    fprintf(f, "#%lu %.50s %08lX %08lX %08lX\n",
            ++log_sequence, event, a, b, c);
    fflush(f);
    fclose(f);
}

void rdp_diag_log_init(void)
{
    FILE *f;
    LightLock_Lock(&log_lock);
    log_enabled = false;
    log_sequence = 0;
    /* No O_CREAT: this exact file must already exist and be writable. */
    f = fopen(RDP_LOG_PATH, "rb+");
    if (f) {
        fclose(f);
        log_enabled = true;
        emit_locked("session-start/v1", 0, 0, 0);
    }
    LightLock_Unlock(&log_lock);
}

int rdp_diag_log_enabled(void)
{
    int result;
    LightLock_Lock(&log_lock);
    result = log_enabled ? 1 : 0;
    LightLock_Unlock(&log_lock);
    return result;
}

void rdp_diag_log_event(const char *event, unsigned long a,
                        unsigned long b, unsigned long c)
{
    LightLock_Lock(&log_lock);
    emit_locked(event, a, b, c);
    LightLock_Unlock(&log_lock);
}
