#include "map_downloads.hpp"
#include <cstdio>
namespace d=orcsdr::maps::downloads;
int main() {
 int failures=0;
 auto check=[&](bool ok,const char* name){if(!ok){++failures;std::printf("FAIL %s\n",name);}};
 d::Snapshot s;s.phase=d::Phase::failed;s.pending=true;
 check(d::can_retry(s),"pending failed recovery remains retryable offline and unconfigured");
 check(!d::can_check(s)&&!d::can_install(s,0),"pending recovery blocks new catalog and install");
 s.busy=true;check(!d::can_retry(s),"active operation cannot queue retry");
 s={};s.configured=true;s.connected=true;s.catalog_ready=true;s.sd_available=true;s.count=2;
 check(d::can_check(s)&&d::can_install(s,1)&&!d::can_install(s,2),"ready catalog bounds checked");
 s.connected=false;check(!d::can_check(s)&&d::can_install(s,0),"staged archive can finish without network");
 s.sd_available=false;check(!d::can_install(s,0),"missing SD disables installation");
 s.busy=true;check(!d::can_check(s)&&!d::can_install(s,0),"active job excludes other commands");
 s={};s.phase=d::Phase::complete;check(!d::can_retry(s),"success does not offer retry");
 s={};s.phase=d::Phase::failed;s.pending=true;check(d::can_reset(s),"stuck saved install can be reset");
 s.busy=true;check(!d::can_reset(s),"reset not offered during an active job");
 s={};s.phase=d::Phase::failed;check(!d::can_reset(s),"nothing to reset without a saved install");
 std::printf("RESULT: download actions %s\n",failures?"FAIL":"PASS");return failures?1:0;
}
