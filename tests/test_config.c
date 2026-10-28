#include "config.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void good(const char *s,const char *host,unsigned port,const char *user,const char *domain) {
  RdpConfig cfg; char err[128];
  int rc=rdp_config_parse(s,&cfg,err,sizeof(err));
  if(rc) printf("FAILED good input %s => %s\n",s,err);
  assert(!rc);
  assert(!strcmp(cfg.host,host)); assert(cfg.port==port);
  assert(!strcmp(cfg.username,user)); assert(!strcmp(cfg.domain,domain));
}
static void bad(const char *s) {
  RdpConfig cfg; char err[128];
  assert(rdp_config_parse(s,&cfg,err,sizeof(err))<0);
  assert(err[0]);
}
int main(void) {
  good("192.168.1.100:3389\n","192.168.1.100",3389,"","");
  good("rdp01.corp.example.com\r\n","rdp01.corp.example.com",3389,"","");
  good("# Comment\nhost = dc01.example.com\nport=3390\nusername=CORP\\alice\n", "dc01.example.com",3390,"alice","CORP");
  good("host=win11\ndomain=corp.example.com\nusername=alice\n", "win11",3389,"alice","corp.example.com");
  good("host=win11\nusername=alice@corp.example.com\n", "win11",3389,"alice@corp.example.com","");
  good("HOST=rdp.local\nUSERNAME=CORP\\Alice\nDOMAIN=corp\n", "rdp.local",3389,"Alice","CORP");
  bad(""); bad("host=foo\nport=0\n"); bad("foo:99999");
  bad("host=foo\nhost=bar\n"); bad("foo\nusername=alice\n");
  bad("host=foo\npassword=secret\n"); bad("host=foo/bar\n");
  bad("host=foo\nusername=AAA\\bob\ndomain=BBB\n");
  bad("host=foo\nusername=alice@corp.local\ndomain=CORP\n");
  bad("host=foo\nusername=CORP\\\n");
  bad("host=foo\nusername=alice@\n");
  bad("host=foo\nusername=x\\y\\z\n");
  puts("Domain and host config tests passed");
  return 0;
}
