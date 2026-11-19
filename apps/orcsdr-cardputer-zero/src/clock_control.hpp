#pragma once
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <condition_variable>

namespace orc::zero {
struct ClockStatus {bool ntp=false,synced=false;std::string server,message;};
inline bool valid_utc_digits(const std::string& s){
  if(s.size()!=14||s<"20240101000000"||s>"20991231235959")return false;
  for(char ch:s)if(ch<'0'||ch>'9')return false;
  return true; // Calendar validation is enforced by the privileged helper.
}
class ClockControl {
 public:
  ~ClockControl(){stop();}
  void start(){stop();running_=true;worker_=std::thread([this]{run();});}
  void stop(){running_=false;wake_.notify_all();if(worker_.joinable())worker_.join();}
  ClockStatus snapshot(){std::lock_guard<std::mutex> l(mutex_);return status_;}
  void sync(){request("--sync");}
  bool set(const std::string& utc){if(!valid_utc_digits(utc))return false;request("--set "+utc);return true;}
 private:
  static std::string command(const std::string& args,bool privileged){
    const std::string cmd="timeout -k 1 12 "+std::string(privileged?"sudo -n ":"")+"/usr/local/libexec/orcsdr-clock "+args+" 2>&1";
    FILE* pipe=popen(cmd.c_str(),"r");if(!pipe)return "CLOCK SERVICE UNAVAILABLE";
    std::string text;char b[192];while(fgets(b,sizeof(b),pipe))if(text.size()<2048)text+=b;
    const int result=pclose(pipe);
    if(result&&text.empty())return "CLOCK COMMAND FAILED";
    return text;
  }
  void request(std::string action){std::lock_guard<std::mutex> l(mutex_);pending_=std::move(action);status_.message="WORKING...";wake_.notify_all();}
  void run(){
    while(running_){
      std::string action;
      {std::lock_guard<std::mutex> l(mutex_);action.swap(pending_);}
      if(!action.empty()){
        auto result=command(action,true);
        std::lock_guard<std::mutex> l(mutex_);status_.message=result.substr(0,result.find('\n'));
      }
      const auto info=command("--status",false);
      {
        std::lock_guard<std::mutex> l(mutex_);
        status_.ntp=info.find("NTP=yes")!=std::string::npos;
        status_.synced=info.find("NTPSynchronized=yes")!=std::string::npos;
        const auto at=info.find("ServerName=");
        status_.server=at==std::string::npos?"":info.substr(at+11,info.find('\n',at)-(at+11));
        if(info.find("NTP=")==std::string::npos)status_.message="CLOCK STATUS UNAVAILABLE";
      }
      std::unique_lock<std::mutex> l(mutex_);
      wake_.wait_for(l,std::chrono::seconds(3),[&]{return !running_||!pending_.empty();});
    }
  }
  std::atomic<bool> running_{false};std::thread worker_;std::mutex mutex_;std::condition_variable wake_;
  ClockStatus status_;std::string pending_;
};
}
