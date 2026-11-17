#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace orc::zero {
// ALSA is an output adapter, never the radio/decoder owner. A null sink is
// explicitly diagnostic and must never be labelled audible on the UI.
class AudioSink {
 public:
  AudioSink()=default;
  ~AudioSink();
  AudioSink(const AudioSink&)=delete;
  AudioSink& operator=(const AudioSink&)=delete;
  bool open(const std::string& device,bool null_sink=false);
  void close() noexcept;
  void discard() noexcept;
  bool write_mono(const int16_t* samples,size_t frames,int volume_percent,bool mute);
  bool audible() const noexcept { return pcm_!=nullptr && !null_; }
  bool opened() const noexcept { return pcm_!=nullptr || null_; }
  const std::string& last_error() const noexcept { return last_error_; }
  uint64_t underruns() const noexcept { return underruns_; }
  uint64_t short_writes() const noexcept { return short_writes_; }
  uint64_t frames_written() const noexcept { return frames_written_; }
  const std::vector<int16_t>& diagnostic_output() const noexcept { return scratch_; }
 private:
  void* pcm_=nullptr;
  bool null_=false;
  unsigned channels_=0;
  float gain_=0.0f;
  std::string last_error_;
  uint64_t underruns_=0,short_writes_=0,frames_written_=0;
  std::vector<int16_t> scratch_; // allocated on open, never on audio writes
};
}