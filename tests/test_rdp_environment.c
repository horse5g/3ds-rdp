#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "rdp_environment.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void exists_dir(const char *path)
{
    struct stat st;
    assert(stat(path,&st)==0);
    assert(S_ISDIR(st.st_mode));
}
int main(void)
{
    char home[]="test-rdp-env-XXXXXX";
    char path[384], error[96];
    static const char *const vars[]={
        "XDG_CONFIG_HOME","XDG_CACHE_HOME","XDG_DATA_HOME",
        "TMPDIR","XDG_RUNTIME_DIR"};
    static const char *const suffix[]={
        "/.config","/.cache","/.local/share","/tmp","/tmp"};
    size_t i;
    assert(mkdtemp(home)!=NULL);
    assert(rdp_environment_init(home,error,sizeof(error))==0);
    assert(error[0]==0);
    assert(getenv("HOME") && strcmp(getenv("HOME"),home)==0);
    for(i=0;i<sizeof(vars)/sizeof(vars[0]);++i){
        assert(snprintf(path,sizeof(path),"%s%s",home,suffix[i])>0);
        assert(getenv(vars[i]) && strcmp(getenv(vars[i]),path)==0);
        exists_dir(path);
    }
    /* Existing home should be idempotent; no unknown global HOME used. */
    assert(rdp_environment_init(home,error,sizeof(error))==0);
    assert(rdp_environment_init("",error,sizeof(error))!=0);
    assert(error[0]!=0);
    snprintf(path,sizeof(path),"%s/tmp",home);assert(rmdir(path)==0);
    snprintf(path,sizeof(path),"%s/.local/share",home);assert(rmdir(path)==0);
    snprintf(path,sizeof(path),"%s/.local",home);assert(rmdir(path)==0);
    snprintf(path,sizeof(path),"%s/.config",home);assert(rmdir(path)==0);
    snprintf(path,sizeof(path),"%s/.cache",home);assert(rmdir(path)==0);
    assert(rmdir(home)==0);
    puts("WinPR home / XDG bootstrap test passed");
    return 0;
}
