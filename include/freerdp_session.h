#ifndef RDP_FREERDP_SESSION_H
#define RDP_FREERDP_SESSION_H
#include <stddef.h>
#include "config.h"
#include "profile_store.h"

/* Interactive experimental FreeRDP session, only built in RDP_FULL_CLIENT mode.
 * Return 0 for clean termination, -1 for unsupported or failed startup.
 * Never persists passwords or relaxes certificate verification. */
int freerdp_3ds_run(const RdpConfig *cfg, const RdpUiOptions *options, char *reason, size_t reason_size);
#endif
