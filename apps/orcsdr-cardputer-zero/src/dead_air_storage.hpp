#pragma once
#include "dead_air_file_sink.hpp"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>
namespace orc::zero {
class DeadAirStorage {
 public:
  ~DeadAirStorage(){stop();}
  void start(const std::filesystem::path& directory){
    stop();failed_=false;saved_=0;running_=true;
    worker_=std::thread([this,directory]{
      try{
        std::filesystem::create_directories(directory);
        const auto stamp=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        const auto path=directory/("session-"+std::to_string(stamp)+"-"+std::to_string(getpid())+".jsonl");
        DeadAirFileSink sink(path.c_str());orcsdr::dead_air::EventJournal journal;
        if(!sink.ready()||!journal.start(sink,uint32_t(stamp))){failed_=true;return;}
        std::clog<<"Dead Air journal "<<path<<std::endl;
        while(running_&&!journal.failed()){
          journal.drain(queue_,sink);saved_=journal.saved();
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if(!journal.finish(queue_,sink))failed_=true;
        saved_=journal.saved();
      }catch(const std::exception& e){failed_=true;std::cerr<<"Dead Air journal: "<<e.what()<<std::endl;}
    });
  }
  void stop(){running_=false;if(worker_.joinable())worker_.join();}
  orcsdr::dead_air::EventQueue& queue(){return queue_;}
  uint32_t saved()const{return saved_;}
  uint32_t dropped()const{return queue_.dropped();}
  bool failed()const{return failed_;}
 private:
  orcsdr::dead_air::EventQueue queue_;
  std::atomic<bool> running_{false},failed_{true};
  std::atomic<uint32_t> saved_{0};
  std::thread worker_;
};
}
