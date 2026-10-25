#pragma once
// The non-display parts of M5Unified that the Tab5 UI touches, as explicit stand-ins for the
// Waveshare ESP32-P4 board. Anything the board does not have answers "not available"
// (battery -1, RTC disabled, ...) so the UI shows its normal "unavailable" states.
//
// Speaker is a hook: until a board audio backend is installed, isRunning() is false and nothing
// plays. The Waveshare board has an ES8311 codec (I2C 0x18) feeding the speaker connector;
// wiring it in means implementing AudioBackend.
#include <cstddef>
#include <cstdint>
#include <ctime>
#include "orcsdr_gfx/scaled_gfx.hpp"

namespace orcsdr::gfx {

struct AudioBackend {
  virtual ~AudioBackend() = default;
  virtual bool begin(uint32_t sample_rate, bool stereo) = 0;
  virtual void end() = 0;
  virtual bool running() const = 0;
  virtual void set_volume(uint8_t volume) = 0;
  // Queues 16-bit PCM; returns false if it could not be accepted.
  virtual bool play(const int16_t* samples, size_t count, uint32_t sample_rate, bool stereo) = 0;
  virtual void stop() = 0;
};

struct SpeakerConfig {
  uint32_t sample_rate = 48000;
  bool stereo = false;
  uint8_t magnification = 16;
  size_t dma_buf_len = 256;
  size_t dma_buf_count = 8;
  uint8_t task_priority = 2;
};

class Speaker {
 public:
  static void set_backend(AudioBackend* backend) { backend_ = backend; }
  SpeakerConfig config() const { return config_; }
  void config(const SpeakerConfig& c) { config_ = c; }
  bool begin() { return backend_ && backend_->begin(config_.sample_rate, config_.stereo); }
  void end() { if (backend_) backend_->end(); }
  bool isEnabled() const { return backend_ != nullptr; }
  bool isRunning() const { return backend_ && backend_->running(); }
  size_t getPlayingChannels() const { return isRunning() ? 1 : 0; }
  void stop() { if (backend_) backend_->stop(); }
  void stop(uint8_t) { stop(); }
  void setVolume(uint8_t v) { if (backend_) backend_->set_volume(v); }
  void setChannelVolume(uint8_t, uint8_t v) { setVolume(v); }
  bool playRaw(const int16_t* data, size_t length, uint32_t sample_rate, bool stereo = false, uint32_t repeat = 1,
               int channel = -1, bool stop_current = false) {
    (void)repeat; (void)channel; (void)stop_current;
    return backend_ && backend_->play(data, length, sample_rate, stereo);
  }

 private:
  SpeakerConfig config_{};
  static inline AudioBackend* backend_ = nullptr;
};

struct MicConfig {
  uint32_t sample_rate = 16000;
  bool stereo = false;
  size_t dma_buf_len = 256;
  size_t dma_buf_count = 4;
};
class Mic {
 public:
  MicConfig config() const { return config_; }
  void config(const MicConfig& c) { config_ = c; }
  bool begin() { return false; }
  void end() {}
  bool isRecording() const { return false; }
  bool record(int16_t*, size_t, uint32_t, bool = false) { return false; }
 private:
  MicConfig config_{};
};

class Power {
 public:
  int32_t getBatteryLevel() const { return -1; }       // no battery on this board
  int32_t getBatteryVoltage() const { return 0; }
  int32_t getBatteryCurrent() const { return 0; }
  int32_t getVBUSVoltage() const { return 0; }
  bool isCharging() const { return false; }
  int getType() const { return 0; }
  void setExtOutput(bool) {}
};

struct rtc_datetime_t {
  struct { int16_t year = 2000; int8_t month = 1, date = 1, weekDay = 0; } date;
  struct { int8_t hours = 0, minutes = 0, seconds = 0; } time;
};
class Rtc {
 public:
  bool isEnabled() const { return false; }              // the Hosyond bench has no battery-backed RTC wired
  bool getDateTime(rtc_datetime_t*) const { return false; }
  void setDateTime(const rtc_datetime_t&) {}
  void setSystemTimeFromRtc(struct timeval* = nullptr) {}
};

class I2CPort {
 public:
  uint8_t readRegister8(uint8_t, uint8_t, uint32_t = 100000) { return 0; }
  bool readRegister(uint8_t, uint8_t, uint8_t* data, size_t length, uint32_t = 100000) {
    for (size_t i = 0; i < length; ++i) data[i] = 0;
    return false;
  }
  bool bitOn(uint8_t, uint8_t, uint8_t, uint32_t = 100000) { return false; }
  bool bitOff(uint8_t, uint8_t, uint8_t, uint32_t = 100000) { return false; }
};

struct TouchDetail { int x = 0, y = 0; bool isPressed() const { return false; } };
class Touch {
 public:
  TouchDetail getDetail(int = 0) const { return {}; }
};

// The global `M5` object the Tab5 sources use.
class M5Class {
 public:
  orcsdr::gfx::ScaledDisplay Display;
  orcsdr::gfx::Speaker Speaker;  // qualified: a member may not reuse its own type name unqualified
  orcsdr::gfx::Mic Mic;
  orcsdr::gfx::Power Power;
  orcsdr::gfx::Rtc Rtc;
  orcsdr::gfx::I2CPort In_I2C, Ex_I2C;
  orcsdr::gfx::Touch Touch;
  void update() {}
};

}  // namespace orcsdr::gfx
