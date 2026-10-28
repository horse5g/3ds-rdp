#ifndef RDP_PROBE_H
#define RDP_PROBE_H
#include <stddef.h>
#include <stdint.h>
typedef enum { RDP_NEG_INVALID, RDP_NEG_SELECTED, RDP_NEG_FAILURE, RDP_NEG_NO_DATA } RdpNegKind;
typedef struct { RdpNegKind kind; uint32_t value; } RdpNegResult;
size_t rdp_neg_build_request(uint8_t *out, size_t capacity);
RdpNegKind rdp_neg_parse_confirm(const uint8_t *packet, size_t length, RdpNegResult *result);
const char *rdp_neg_protocol_name(uint32_t protocol);
const char *rdp_neg_failure_name(uint32_t code);
#endif
