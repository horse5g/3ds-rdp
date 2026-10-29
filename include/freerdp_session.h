#ifndef RDP_FREERDP_SESSION_H
#define RDP_FREERDP_SESSION_H
#include <stddef.h>
#include <stdbool.h>
#include "config.h"
#include "profile_store.h"

/* Interactive experimental FreeRDP session, only built in RDP_FULL_CLIENT mode.
 * Return 0 for clean termination, -1 for unsupported or failed startup.
 * Password caching is explicit opt-in for this launch only; never persists
 * plaintext credentials or relaxes certificate verification. */
int freerdp_3ds_run(const RdpConfig *cfg, const RdpUiOptions *options, bool cache_password, char *reason, size_t reason_size);
/* Explicitly wipe all RAM-cached credentials on return to launcher or exit. */
void freerdp_3ds_clear_password_cache(void);
#endif
