#include "bias_tee.hpp"
#include <cassert>
#include <iostream>
int main(){
 orc::linux_sdr::BiasTeeControl control;int calls=0,last=-1;
 auto apply=[&](int enabled){++calls;last=enabled;return 0;};
 control.connect();control.service(apply);assert(calls==1&&last==0&&control.state()==control.off);
 control.service(apply);assert(calls==1);
 control.request(true);control.service(apply);assert(calls==2&&last==1&&control.state()==control.on);
 control.disconnect(apply);assert(last==0&&control.state()==control.waiting&&control.requested());
 control.connect();control.service(apply);assert(last==1);
 control.request(false);control.service([](int){return -1;});assert(control.state()==control.failed);
 const auto count=calls;control.service(apply);assert(calls==count);
 control.connect();control.service(apply);assert(last==0&&control.state()==control.off);
 std::cout<<"Bias Tee default off, live requests, reconnect, close-off, failure reporting PASS\n";
}
