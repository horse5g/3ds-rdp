#include "rdp_startup.h"
#include <stdio.h>

static int fail(char *error,size_t cap,const char *text)
{
    if(error && cap) snprintf(error,cap,"%s",text);
    return -1;
}
int rdp_full_session_preflight(const RdpConfig *cfg,
                               const RdpUiOptions *options,
                               char *error,size_t error_size)
{
    if(!error || !error_size) return -1;
    error[0]=0;
    if(!cfg || !options)
        return fail(error,error_size,"Invalid RDP configuration");
    if(rdp_profile_validate(cfg,error,error_size))
        return -1;
    if(!rdp_options_valid(options))
        return fail(error,error_size,"Invalid RDP display/input options");
    if(!cfg->username[0])
        return fail(error,error_size,"Set username for NLA sign-in");
    return 0;
}
