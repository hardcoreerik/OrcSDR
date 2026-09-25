#pragma once

#include <cstddef>
#include <cstdint>

namespace orcsdr::airband {

constexpr uint32_t kMinFrequencyHz = 118000000u;
constexpr uint32_t kMaxFrequencyHz = 136975000u;
constexpr uint32_t kGuardFrequencyHz = 121500000u;
constexpr size_t kBankCapacity = 32;
constexpr size_t kActivityCapacity = 32;

enum class Spacing : uint8_t { khz25, khz833 };
enum class ScanSource : uint8_t { airport_bank, full_band };
enum class ScanState : uint8_t { off, scanning, settling, receiving, hang, held };

struct BankEntry {
  uint32_t frequency_hz = 0;
  float distance_nm = 0.0f;
  char label[40]{};
};

struct Activity {
  uint32_t frequency_hz = 0;
  uint32_t start_ms = 0;
  uint32_t duration_ms = 0;
  float peak_dbfs = -120.0f;
  char label[40]{};
};

struct Settings {
  Spacing spacing = Spacing::khz25;
  ScanSource source = ScanSource::airport_bank;
  int16_t squelch_dbfs = -75;
  uint16_t settle_ms = 55;
  uint16_t hang_ms = 1500;
  bool priority_guard = true;
  uint8_t priority_every = 20;
};

uint32_t spacing_hz(Spacing spacing);
const char* spacing_name(Spacing spacing);
const char* source_name(ScanSource source);
const char* state_name(ScanState state);
bool in_band(uint32_t frequency_hz);
uint32_t snap_frequency(uint32_t frequency_hz, Spacing spacing);
uint32_t step_frequency(uint32_t frequency_hz, int direction, Spacing spacing);

class Scanner {
 public:
  void reset();
  void set_bank(const BankEntry* entries, size_t count);
  void start(uint32_t now_ms, uint32_t current_frequency_hz);
  void stop();
  void hold(uint32_t now_ms, uint32_t current_frequency_hz);
  void resume(uint32_t now_ms);
  void skip(uint32_t now_ms, uint32_t current_frequency_hz);
  void clear_activity();

  // Service the scanner from the UI task. Returns a frequency that should be
  // tuned, or zero when no retune is requested.
  uint32_t service(uint32_t now_ms, uint32_t current_frequency_hz, float signal_dbfs);
  void note_retuned(uint32_t now_ms, uint32_t frequency_hz);

  Settings& settings() { return settings_; }
  const Settings& settings() const { return settings_; }
  ScanState state() const { return state_; }
  bool running() const { return state_ != ScanState::off; }
  uint32_t target_frequency_hz() const { return target_frequency_hz_; }
  size_t bank_count() const { return bank_count_; }
  const BankEntry* bank(size_t index) const {
    return index < bank_count_ ? &bank_[index] : nullptr;
  }
  size_t activity_count() const { return activity_count_; }
  // Index zero is newest.
  const Activity* activity(size_t index) const;
  uint32_t stops() const { return stops_; }
  uint32_t channels_checked() const { return channels_checked_; }
  uint32_t hang_remaining_ms(uint32_t now_ms) const;
  bool squelch_open(float signal_dbfs) const;

  static bool self_check();

 private:
  uint32_t next_target(uint32_t current_frequency_hz);
  uint32_t begin_target(uint32_t now_ms, uint32_t current_frequency_hz,
                        uint32_t target_frequency_hz);
  void begin_episode(uint32_t now_ms, uint32_t frequency_hz, float signal_dbfs);
  void update_episode(uint32_t now_ms, float signal_dbfs);
  void close_episode(uint32_t now_ms);
  void push_activity(const Activity& activity);
  const char* label_for(uint32_t frequency_hz) const;

  Settings settings_{};
  BankEntry bank_[kBankCapacity]{};
  size_t bank_count_ = 0;
  size_t bank_cursor_ = 0;

  Activity activity_[kActivityCapacity]{};
  size_t activity_head_ = 0;
  size_t activity_count_ = 0;

  ScanState state_ = ScanState::off;
  uint32_t target_frequency_hz_ = kGuardFrequencyHz;
  uint32_t settle_until_ms_ = 0;
  uint32_t hang_until_ms_ = 0;
  uint32_t last_above_ms_ = 0;
  uint32_t episode_start_ms_ = 0;
  float episode_peak_dbfs_ = -120.0f;
  uint32_t episode_frequency_hz_ = 0;
  uint32_t skip_once_hz_ = 0;
  uint32_t stops_ = 0;
  uint32_t channels_checked_ = 0;
  uint8_t since_guard_ = 0;
  bool episode_open_ = false;
};

}  // namespace orcsdr::airband
