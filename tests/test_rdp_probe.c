#include "rdp_probe.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    uint8_t out[19],mutated[19];
    const uint8_t request[19]={3,0,0,19,14,0xe0,0,0,0,0,0,1,0,8,0,3,0,0,0};
    const uint8_t ok[19]={3,0,0,19,14,0xd0,0,0,0,0,0,2,0,8,0,2,0,0,0};
    const uint8_t failure[19]={3,0,0,19,14,0xd0,0,0,0,0,0,3,0,8,0,5,0,0,0};
    const uint8_t legacy[11]={3,0,0,11,6,0xd0,0,0,0,0,0};
    RdpNegResult r;
    assert(rdp_neg_build_request(out,18)==0);
    assert(rdp_neg_build_request(out,19)==19&&memcmp(out,request,19)==0);
    assert(rdp_neg_parse_confirm(ok,19,&r)==RDP_NEG_SELECTED&&r.value==2);
    assert(rdp_neg_parse_confirm(failure,19,&r)==RDP_NEG_FAILURE&&r.value==5);
    assert(rdp_neg_parse_confirm(legacy,11,&r)==RDP_NEG_NO_DATA);
    memcpy(mutated,ok,19);mutated[12]=3;
    assert(rdp_neg_parse_confirm(mutated,19,&r)==RDP_NEG_SELECTED);
    memcpy(mutated,ok,19);mutated[3]=20;
    assert(rdp_neg_parse_confirm(mutated,19,&r)==RDP_NEG_INVALID);
    memcpy(mutated,ok,19);mutated[5]=0xe0;
    assert(rdp_neg_parse_confirm(mutated,19,&r)==RDP_NEG_INVALID);
    memcpy(mutated,ok,19);mutated[13]=7;
    assert(rdp_neg_parse_confirm(mutated,19,&r)==RDP_NEG_INVALID);
    assert(rdp_neg_parse_confirm(ok,10,&r)==RDP_NEG_INVALID);
    puts("RDP negotiation tests passed");
    return 0;
}
