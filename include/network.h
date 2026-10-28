#ifndef NETWORK_H
#define NETWORK_H
#include <stddef.h>
#include <stdint.h>
#include "rdp_probe.h"
int network_rdp_probe(const char *ipv4,uint16_t port,RdpNegResult *result,char *error,size_t error_size);
#endif
