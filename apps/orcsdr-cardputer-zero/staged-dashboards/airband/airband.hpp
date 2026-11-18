#pragma once
#include "../../src/iq_queue.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace orc::zero::airband {
constexpr uint32_t kMinHz=118000000, kMaxHz=136975000, kGuardHz=121500000;
constexpr uint32_t kRatePreferred=2560000, kRateFallback=2400000;
constexpr uint32_t kAudioRate=48000, kStepHz=25000;
constexpr size_t kMaxIqBytes=32768;
constexpr size_t kSpectrum=256, kWaterfall=32;

enum class Connection : uint8_t { off, connecting, ready, error };
enum class Action : uint8_t { tune_up, tune_down, guard, frequency_entry, entry_digit,
  entry_point, entry_backspace, entry_commit, entry_cancel, volume_up,volume_down,
  mute, squelch_up, squelch_down, gain_auto_toggle, gain_up, gain_down,stop };
struct Request { uint32_t rf_center_hz=0, sample_rate_sps=kRatePreferred; bool retune=false; };
struct Settings {int volume=35,squelch_db=6,gain_tenth_db=0;
  bool user_muted=false,gain_auto=true;};
struct Snapshot {
  uint32_t selected_hz=kGuardHz,requested_center_hz=0,applied_center_hz=0,input_rate=0;
  uint64_t generation=0,known_queue_drops=0, discontinuities=0, rejected_blocks=0;
  int volume=35,squelch_db=6,gain_tenth_db=0,step_hz=kStepHz;
  bool user_muted=false,squelch_open=false,carrier_detected=false,audio_ready=false,gain_auto=true,entry_active=false,demo=false;
  bool has_spectrum=false,tune_pending=true;
  Connection connection=Connection::off;
  float channel_level_dbfs=-120.0f,relative_db=0.0f;
  uint8_t waterfall_rows=0;
  std::array<uint8_t,256> spectrum{};
  std::array<std::array<uint8_t,256>,kWaterfall> waterfall{};
  std::array<char,16> entry{};
  std::array<char,64> error{};
};
using PcmCallback=void (*)(void* ctx,const int16_t* pcm,size_t frames);
class Controller {
public:
  Controller();
  // UI/controller thread only, except consume() may be called on ONE DSP worker.
  // Host synchronizes actions, readbacks, and snapshot access against consume().
  bool activate(uint32_t selected=kGuardHz);
  Request requested_radio() const;
  Settings settings() const;
  bool restore_settings(const Settings& settings);
  bool action(Action a,char digit=0);
  bool tune_direct(uint32_t hz);
  void radio_applied(uint32_t center_hz,uint32_t sample_rate_sps,uint64_t generation);
  void radio_failed(const char* message);
  void audio_available(bool ready,const char* failure=nullptr);
  void disconnected(const char* reason);
  void set_known_queue_drops(uint64_t count);
  void set_gain_readback(bool automatic,int tenth_db);
  void set_demo(bool demo);
  // Decoder accepts only the current applied center/rate/generation, returns false on invalid/stale block.
  bool consume(const uint8_t* cu8,size_t bytes,const orc::linux_sdr::IqMetadata& meta,
               PcmCallback cb,void* ctx);
  void stop();
  Snapshot snapshot() const {return s_;}
private:
  void reset_dsp();
  void reset_squelch();
  void fft_update(const uint8_t* cu8,size_t bytes);
  void pcm_emit(int16_t value,PcmCallback cb,void* ctx);
  void process_80k(float i,float q,PcmCallback cb,void* ctx);
  Snapshot s_{};
  struct Cic {uint64_t a=0,b=0,c=0,d1=0,d2=0,d3=0;};
  Cic cic_i_{},cic_q_{};
  uint32_t decimate_=0, decim_phase_=0;
  float nco_re_=1,nco_im_=0,nco_step_re_=1,nco_step_im_=0;
  uint32_t nco_count_=0;
  std::array<float,128> ring_i_{},ring_q_{};
  unsigned ring_pos_=0;
  uint64_t samples_80k_=0,next_output_tick_=0;
  std::array<std::array<float,97>,3> polyphase_{};
  float dc_=0, hp_x_=0,hp_y_=0,lp_a_=0,lp_b_=0,level_=0,level_pow_=0,noise_floor_db_=-120;
  bool noise_ready_=false;
  uint64_t squelch_samples_=0,squelch_hang_=0;
  std::array<int16_t,256> pcm_{};
  size_t pcm_size_=0;
  uint32_t applied_selected_hz_=0;
  uint64_t next_sample_=0;
  bool have_continuity_=false;
  uint32_t fft_delay_=0;
};

bool valid_frequency(uint32_t hz);
bool parse_frequency_mhz(const std::string& text,uint32_t& hz);
uint32_t step_frequency(uint32_t hz,int direction);
int32_t display_offset_hz(uint32_t selected_hz,uint32_t center_hz);
}

namespace orc::zero::airband {
// Keyboard adapter is deliberately opt-in. Space/P/Tab/Q remain coordinator-owned.
bool keyboard_action(Controller& controller,char key);
class Renderer {
public:
  static constexpr int width=320,height=170;
  struct Rect {int x=0,y=0,w=0,h=0;};
  Renderer();
  // Caller passes an already validated 256-entry RGB565 palette from the shared palette owner.
  // nullptr means intensity-only grayscale and is NOT an additional palette selection.
  void render(const Snapshot& state,const uint16_t* palette=nullptr);
  bool save_ppm(const std::string& path)const;
  const std::array<uint16_t,width*height>& framebuffer()const{return pixels_;}
  const std::array<Rect,6>& dirty_rects()const{return dirty_;}
  unsigned dirty_count()const{return dirty_count_;}
  unsigned full_clears()const{return full_clears_;}
  void invalidate_palette(){palette_invalid_=true;}
private:
  void rect(int x,int y,int w,int h,uint16_t color);
  void point(int x,int y,uint16_t color);
  void line(int x0,int y0,int x1,int y1,uint16_t color);
  void text(int x,int y,const std::string& str,uint16_t color,int size=1,int chars=50);
  void init_static();
  std::array<uint16_t,width*height> pixels_{};
  std::array<Rect,6> dirty_{};
  unsigned dirty_count_=0,full_clears_=0;
  bool initialized_=false,have_last_=false,palette_invalid_=true;
  const uint16_t* last_palette_=nullptr;
  Snapshot last_{};
};
}
