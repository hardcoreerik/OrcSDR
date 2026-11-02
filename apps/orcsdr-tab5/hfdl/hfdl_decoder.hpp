#pragma once
// Receive-only HFDL frontend: 1440 Hz complex translation + bounded FIR/decimator.
// NOT a bit/frame decoder. No unverified PDU may enter the UI model.
#include <cstddef>
#include <cstdint>
#include "../ui/hfdl_dashboard_model.hpp"

namespace orcsdr::hfdl {
constexpr uint32_t kSymbolRate=1800,kCarrierOffsetHz=1440;
constexpr uint32_t kFrameSeconds=32,kSlotsPerFrame=13;
constexpr uint32_t kSlotMs=kFrameSeconds*1000/kSlotsPerFrame;
enum class Rate:uint8_t { psk2_300,psk2_600,psk4_1200,psk8_1800,count };
struct Config {
  uint32_t sample_rate=48000;
  uint32_t center_hz=0;
  Rate expected_rate=Rate::psk4_1200;
};
struct FrontendStats {
  uint64_t input_samples=0,channel_samples=0;
  float channel_power=0;
  uint32_t invalid_blocks=0;
};
using ChannelSink=void(*)(void* context,const float* i,const float* q,size_t count);
class Decoder {
 public:
  Decoder();
  void reset();
  void set_config(const Config& cfg);
  void set_model(hfdl_ui::Model* model);
  void set_channel_sink(ChannelSink sink,void* context); // synchronous worker callback; never heap
  void flush_channel(); // call after a stream/file chunk; does not pad or invent samples
  bool process(const float* i,const float* q,size_t count);
  bool process_audio(const float* audio,size_t count);
  uint32_t frames_seen() const {return 0;}
  uint32_t spdus_seen() const {return 0;}
  uint32_t messages_posted() const {return 0;}
  const FrontendStats& frontend_stats() const {return stats_;}
  bool valid_config() const {return valid_config_;}
 private:
  static constexpr int kTaps=127,kOutputRate=12000;
  void configure_filter();
  void consume(float i,float q);
  Config cfg_{};
  hfdl_ui::Model* model_=nullptr;
  FrontendStats stats_{};
  float coeff_[kTaps]{},hist_i_[kTaps]{},hist_q_[kTaps]{};
  float oscillator_i_=1,oscillator_q_=0,step_i_=1,step_q_=0;
  int history_pos_=0,decim_step_=4,decim_phase_=0;
  bool valid_config_=false;
  ChannelSink channel_sink_=nullptr;
  void* sink_context_=nullptr;
  float block_i_[64]{},block_q_[64]{};
  size_t block_count_=0;
};
} // namespace orcsdr::hfdl
