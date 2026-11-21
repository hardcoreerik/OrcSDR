#pragma once
#include "dead_air_journal.hpp"
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>

namespace orc::zero {
// Storage-worker-only sink. Each session gets an exclusive file.
class DeadAirFileSink final : public orcsdr::dead_air::JournalSink {
 public:
  explicit DeadAirFileSink(const char* path)
    : file_(::open(path,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600)) {}
  DeadAirFileSink(const DeadAirFileSink&)=delete;
  DeadAirFileSink& operator=(const DeadAirFileSink&)=delete;
  ~DeadAirFileSink() override {if(file_>=0)::close(file_);}
  bool ready() const {return file_>=0&&!failed_;}
  bool append(const char* line,size_t length) override {
    if(!ready())return false;
    const auto start=::lseek(file_,0,SEEK_CUR);
    if(start<0){failed_=true;return false;}
    size_t at=0;
    while(at<length){
      const auto wrote=::write(file_,line+at,length-at);
      if(wrote<0&&errno==EINTR)continue;
      if(wrote<=0){
        if(::ftruncate(file_,start)<0)std::perror("Dead Air journal rollback");
        failed_=true;
        return false;
      }
      at+=size_t(wrote);
    }
    return true;
  }
  bool sync() override {
    if(!ready())return false;
    int rc;
    do {rc=::fsync(file_);} while(rc<0&&errno==EINTR);
    if(rc<0)failed_=true;
    return !failed_;
  }
 private:
  int file_=-1;
  bool failed_=false;
};
}
