#include "../src/control/secure_runtime.hpp"
#include <cassert>
#include <iostream>
int main(){
  const uint8_t mac[6]={0,0,0,0,0,2};
  for(int stage=1;stage<=7;++stage){
    orc::secure::Runtime runtime;
    for(int retry=0;retry<100;++retry){
      fault_step=stage;operation=0;
      assert(!runtime.begin(2,mac,false,[](const uint8_t*,const uint8_t*){return true;}));
      assert(live_queues==0 && live_stores==0);
      assert(runtime.status().state==orc::secure::State::failed);
      assert(!runtime.action(orc::secure::Action::forget));
    }
  }
  std::cout<<"PASS: 700 initialization failures release all queues/NVS handles\n";
}
