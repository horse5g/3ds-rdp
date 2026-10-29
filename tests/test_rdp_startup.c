#include "rdp_startup.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    RdpConfig cfg={0};
    char reason[96]={0};
    RdpUiOptions options=rdp_options_default();
    strcpy(cfg.host,"rdp.example.com");
    cfg.port=3389;
    /* Negotiation allows a host-only profile, but not a full NLA login. */
    assert(rdp_profile_validate(&cfg,reason,sizeof(reason))==0);
    assert(rdp_full_session_preflight(&cfg,&options,reason,sizeof(reason))!=0);
    assert(strstr(reason,"username")!=NULL);
    strcpy(cfg.username,"alice");
    assert(rdp_full_session_preflight(&cfg,&options,reason,sizeof(reason))==0);
    assert(reason[0]==0);
    strcpy(cfg.domain,"EXAMPLE");
    assert(rdp_full_session_preflight(&cfg,&options,reason,sizeof(reason))==0);
    cfg.port=0;
    assert(rdp_full_session_preflight(&cfg,&options,reason,sizeof(reason))!=0);
    cfg.port=3389;
    options.color_depth=0;
    assert(rdp_full_session_preflight(&cfg,&options,reason,sizeof(reason))!=0);
    assert(strstr(reason,"options")!=NULL);
    assert(rdp_full_session_preflight(NULL,&options,reason,sizeof(reason))!=0);
    assert(rdp_full_session_preflight(&cfg,&options,NULL,0)!=0);
    puts("FreeRDP NLA full-session startup preflight tests passed");
    return 0;
}
