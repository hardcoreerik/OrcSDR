#pragma once
#include "control_model.hpp"
#include "iq_queue.hpp"
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace orc::zero {
struct FmTelemetry {
  Connection connection=Connection::disconnected;
  std::uint32_t requested_hz=96100000,applied_hz=0,applied_rate_sps=0;
  std::uint64_t generation=0,known_queue_dropped_samples=0,audio_underruns=0,audio_frames=0;
  bool audio_ready=false,has_spectrum=false,gain_auto=true;
  int gain_tenth_db=0;
  std::vector<int> supported_gains;
  std::string receiver_name="No receiver",last_error,audio_error;
  std::array<std::uint8_t,256> spectrum{};
  std::array<std::array<std::uint8_t,256>,36> waterfall{};
  int waterfall_rows=0;
};
// One physical SDR owner, one IQ producer, one DSP/audio consumer. GUI sees
// snapshots only. This is a local adapter, not a separate Radio Service ABI.
class FmSession {
 public:
  FmSession();
  ~FmSession();
  FmSession(const FmSession&)=delete;
  FmSession& operator=(const FmSession&)=delete;
  void start(std::uint32_t device,std::uint32_t initial_hz,
             std::string audio_device="default",bool null_audio=false);
  void stop();
  void restart(std::uint32_t hz);
  bool request_tune(std::uint32_t hz);
  bool request_gain(bool automatic,int tenth_db);
  void set_volume(unsigned percent);
  void set_mute(bool muted);
  FmTelemetry snapshot() const;
  bool running()const noexcept{return running_.load();}
 private:
  void capture_worker(std::uint32_t device);
  void dsp_worker();
  void set_error(const std::string& error);
  mutable std::mutex state_mutex_,wake_mutex_;
  std::condition_variable wake_;
  orc::linux_sdr::BoundedIqQueue queue_;
  std::thread capture_thread_,dsp_thread_;
  std::atomic<bool> running_{false};
  std::atomic<std::uint32_t> requested_hz_{96100000};
  std::atomic<int> volume_{30},gain_tenth_db_{0};
  std::atomic<bool> muted_{false},gain_auto_{true};
  std::atomic<std::uint64_t> gain_request_seq_{0},stream_generation_{0};
  FmTelemetry state_;
  std::string audio_device_="default";
  bool null_audio_=false;
  std::uint32_t device_index_=0;
};
}