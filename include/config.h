#ifndef RDP_CONFIG_H
#define RDP_CONFIG_H

#include <stddef.h>
#include <stdint.h>

#define RDP_HOST_CAP 256
#define RDP_USER_CAP 128
#define RDP_DOMAIN_CAP 128

/* No passwords or secret material are ever stored by this structure. */
typedef struct {
    char host[RDP_HOST_CAP];
    uint16_t port;
    char username[RDP_USER_CAP];
    char domain[RDP_DOMAIN_CAP];
} RdpConfig;

/* Accepts legacy "host[:port]" or key=value configuration text. */
int rdp_config_parse(const char *text, RdpConfig *out, char *error, size_t error_size);

#endif
