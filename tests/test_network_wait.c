#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
/* Inject select() to reproduce an interrupted Horizon socket wait.
 * Include implementation so its static waitfd() can be exercised. */
#define select mock_select
#include "../source/network.c"
#undef select
#include <assert.h>
#include <stdio.h>

static int calls;
int mock_select(int nfds, fd_set *readfds, fd_set *writefds,
                fd_set *exceptfds, struct timeval *timeout)
{
    fd_set *set=readfds?readfds:writefds;
    (void)exceptfds;
    assert(nfds==4);
    assert(set!=NULL);
    assert(FD_ISSET(3,set));
    assert(timeout!=NULL);
    assert(timeout->tv_sec==3 && timeout->tv_usec==0);
    ++calls;
    if(calls==1){
        FD_ZERO(set);
        timeout->tv_sec=0;
        errno=EINTR;
        return -1;
    }
    return 1;
}
int main(void)
{
    calls=0;
    assert(waitfd(3,0)==1);
    assert(calls==2);
    calls=0;
    assert(waitfd(3,1)==1);
    assert(calls==2);
    puts("SOC select EINTR fd_set and timeout retry tests passed");
    return 0;
}
