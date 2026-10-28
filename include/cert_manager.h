#ifndef RDP_CERT_MANAGER_H
#define RDP_CERT_MANAGER_H

#include <stddef.h>
#include <3ds.h>

/* Native on-device trust management; never contacts an RDP server.
 * The caller retains ownership of the top and bottom consoles.
 * 0 = returned normally, -1 = could not load trust store. */
int rdp_cert_manager_run(PrintConsole *top, PrintConsole *bottom,
                         char *message, size_t message_size);
#endif
