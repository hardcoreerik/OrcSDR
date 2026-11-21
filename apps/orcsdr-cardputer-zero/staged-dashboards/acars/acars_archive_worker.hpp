#pragma once
#include "acars_archive.hpp"
#include "acars_dashboard.hpp"
#include <array>
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <thread>

namespace orc::zero::acars_dashboard {
class ArchiveWorker {
 public:
  ~ArchiveWorker(){stop();}
  bool start(const std::filesystem::path& directory);
  void stop();
  bool enqueue(const Record& record);
  size_t recent(size_t offset,Record* out,size_t capacity,size_t* total)const;
  uint32_t dropped()const{return dropped_.load();}
  bool failed()const{return failed_.load();}
 private:
  void run(std::filesystem::path directory);
  static constexpr size_t kQueue=32;
  std::array<Record,kQueue> queue_{};
  size_t head_=0,tail_=0,held_=0;
  mutable std::mutex mutex_;
  std::condition_variable wake_;
  std::atomic<bool> running_{false},failed_{false};
  std::atomic<uint32_t> dropped_{0};
  std::thread worker_;
  orcsdr::acars_archive::Store store_;
};
}
