#ifndef RDP_FREERDP_SESSION_H
#define RDP_FREERDP_SESSION_H
#include <stddef.h>
#include <stdbool.h>
#include "config.h"
#include "profile_store.h"

/* Interactive experimental FreeRDP session, only built in RDP_FULL_CLIENT mode.
 * Return 0 for clean termination, -1 for unsupported or failed startup.
 * Password caching is automatic within the current process only.
 * Credentials are wiped on authentication failure/app exit, never saved
 * on SD or sent to a different account. Certificate verification is fresh. */
int freerdp_3ds_run(const RdpConfig *cfg, const RdpUiOptions *options, bool bgrx_gdi, char *reason, size_t reason_size);
/* Explicitly wipe all RAM-cached credentials on app exit. */
void freerdp_3ds_clear_password_cache(void);
#endif
