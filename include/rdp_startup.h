#ifndef RDP_STARTUP_H
#define RDP_STARTUP_H
#include "profile_store.h"
#include <stddef.h>

/* Strict full-client preflight. The negotiation probe intentionally permits
 * a blank username, but CredSSP/NLA login needs explicit credentials.
 * No secrets are accepted or persisted here. */
int rdp_full_session_preflight(const RdpConfig *cfg,
                               const RdpUiOptions *options,
                               char *error, size_t error_size);
#endif
