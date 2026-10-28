#include "network.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#define MAX_TPKT 512
static int waitfd(int fd,int write_ready) {
    fd_set fds; struct timeval tv; int rc;
    tv.tv_sec=3; tv.tv_usec=0; FD_ZERO(&fds); FD_SET(fd,&fds);
    do { rc=select(fd+1,write_ready?NULL:&fds,write_ready?&fds:NULL,NULL,&tv); } while(rc<0&&errno==EINTR);
    return rc;
}
static int transfer(int fd,uint8_t *data,size_t n,int writing) {
    size_t offset=0;
    while(offset<n) {
        int count;
        if(waitfd(fd,writing)<=0) return -1;
        count=writing?(int)send(fd,(const char *)data+offset,n-offset,0):(int)recv(fd,(char *)data+offset,n-offset,0);
        if(count<0&&(errno==EINTR||errno==EAGAIN)) continue;
        if(count<=0) return -1;
        offset+=(size_t)count;
    }
    return 0;
}
int network_rdp_probe(const char *ip,uint16_t port,RdpNegResult *result,char *error,size_t error_size) {
    struct sockaddr_in addr; uint8_t request[19],reply[MAX_TPKT];
    int fd=-1,flags,rc,so_error=0; socklen_t so_len=sizeof(so_error);
    size_t n; const char *why="Unknown error";
    if(!ip||!port||!result||!error||!error_size) return -1;
    result->kind=RDP_NEG_INVALID; result->value=0;
    memset(&addr,0,sizeof(addr)); addr.sin_family=AF_INET; addr.sin_port=htons(port);
    if(inet_pton(AF_INET,ip,&addr.sin_addr)!=1) { why="Invalid IPv4";goto fail; }
    fd=socket(AF_INET,SOCK_STREAM,0);
    if(fd<0) {why="socket failed";goto fail;}
    flags=fcntl(fd,F_GETFL,0);
    if(flags<0||fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0) {why="nonblocking setup failed";goto fail;}
    rc=connect(fd,(struct sockaddr *)&addr,sizeof(addr));
    if(rc<0&&errno!=EINPROGRESS&&errno!=EWOULDBLOCK) {why="connect failed";goto fail;}
    if(rc<0&&(waitfd(fd,1)<=0||getsockopt(fd,SOL_SOCKET,SO_ERROR,&so_error,&so_len)<0||so_error)) {why="TCP timed out/refused";goto fail;}
    n=rdp_neg_build_request(request,sizeof(request));
    if(transfer(fd,request,n,1)) {why="send failed";goto fail;}
    if(transfer(fd,reply,4,0)) {why="No response";goto fail;}
    n=((size_t)reply[2]<<8)|reply[3];
    if(reply[0]!=3||reply[1]!=0||n<11||n>sizeof(reply)) {why="Invalid TPKT";goto fail;}
    if(transfer(fd,reply+4,n-4,0)) {why="Truncated response";goto fail;}
    if(rdp_neg_parse_confirm(reply,n,result)==RDP_NEG_INVALID) {why="Invalid X.224 confirmation";goto fail;}
    close(fd); error[0]=0; return 0;
fail:
    snprintf(error,error_size,"%s",why); if(fd>=0) close(fd); return -1;
}
