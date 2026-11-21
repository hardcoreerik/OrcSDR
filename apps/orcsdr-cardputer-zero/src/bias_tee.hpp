#pragma once
#include <atomic>
namespace orc::linux_sdr {
// One physical receiver owns service/connect/disconnect; the UI only requests.
class BiasTeeControl {
 public:
  enum State {waiting=-1,failed=-2,off=0,on=1};
  void request(bool enabled){requested_=enabled;}
  bool requested()const{return requested_;}
  State state()const{return State(state_.load());}
  void connect(){last_=-1;state_=waiting;}
  template<class Apply> void service(Apply apply){
    const int next=requested_?1:0;
    if(last_==next)return;
    const int result=apply(next);
    last_=next;state_=result==0?next:failed;
  }
  template<class Apply> void disconnect(Apply apply){apply(0);last_=-1;state_=waiting;}
 private:
  std::atomic<bool> requested_{false};std::atomic<int> state_{waiting};int last_=-1;
};
inline BiasTeeControl bias_tee;
}
