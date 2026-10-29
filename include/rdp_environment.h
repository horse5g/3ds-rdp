#ifndef RDP_ENVIRONMENT_H
#define RDP_ENVIRONMENT_H

#include <stddef.h>

/* Supply the Unix HOME/XDG paths expected by WinPR on platforms that
 * do not have a user account / shell, including Nintendo 3DS homebrew.
 *
 * The directories must exist before FreeRDP creates its client context.
 * Caller chooses the application-owned root, e.g. sdmc:/3ds/3ds-rdp.
 * Never falls back to a different or global home without reporting failure.
 */
int rdp_environment_init(const char *home, char *error, size_t cap);

#endif
