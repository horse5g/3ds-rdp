#include "rdp_probe.h"
static const uint8_t REQUEST[] = {3,0,0,19,14,0xe0,0,0,0,0,0,1,0,8,0,3,0,0,0};
static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0]<<8)|p[1]); }
static uint16_t le16(const uint8_t *p) { return (uint16_t)((p[1]<<8)|p[0]); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
size_t rdp_neg_build_request(uint8_t *out,size_t cap) {
    size_t i;
    if(!out||cap<sizeof(REQUEST)) return 0;
    for(i=0;i<sizeof(REQUEST);++i) out[i]=REQUEST[i];
    return sizeof(REQUEST);
}
RdpNegKind rdp_neg_parse_confirm(const uint8_t *p,size_t n,RdpNegResult *r) {
    uint8_t type; uint32_t value;
    if(!r) return RDP_NEG_INVALID;
    r->kind=RDP_NEG_INVALID; r->value=0;
    if(!p||n<11||p[0]!=3||p[1]!=0||be16(p+2)!=n||p[4]!=n-5||p[5]!=0xd0||p[10]!=0) return RDP_NEG_INVALID;
    if(n==11) { r->kind=RDP_NEG_NO_DATA; return r->kind; }
    if(n!=19||le16(p+13)!=8) return RDP_NEG_INVALID;
    type=p[11]; value=le32(p+15);
    if(type==2&&(value==0||value==1||value==2||value==8)) r->kind=RDP_NEG_SELECTED;
    else if(type==3&&value>=1&&value<=6) r->kind=RDP_NEG_FAILURE;
    else return RDP_NEG_INVALID;
    r->value=value; return r->kind;
}
const char *rdp_neg_protocol_name(uint32_t v) {
    switch(v) { case 0:return "Legacy RDP (unsupported)";case 1:return "TLS";case 2:return "CredSSP / NLA";case 8:return "CredSSP / NLA extended";default:return "Unknown"; }
}
const char *rdp_neg_failure_name(uint32_t v) {
    switch(v) { case 1:return "TLS required";case 2:return "TLS not allowed";case 3:return "TLS certificate unavailable";case 4:return "Inconsistent flags";case 5:return "NLA required";case 6:return "TLS with user auth required";default:return "Unknown failure"; }
}
