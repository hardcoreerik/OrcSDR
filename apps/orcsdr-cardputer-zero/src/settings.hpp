#pragma once
#include <cstdint>
#include <string>

namespace orc::zero {
struct UserSettings {
  std::uint32_t frequency_hz=96100000;
  unsigned volume=30;
  unsigned step_index=2;
  bool mute=false,gain_auto=true;
  int gain_tenth_db=0;
  std::string audio_device="default";
};
bool validate_settings(const UserSettings& s);
std::string settings_path();
// On parse failure, leave output unchanged. Caller uses safe defaults.
bool load_settings(const std::string& filename,UserSettings& out);
// Write to 0600 temp, fsync, atomically rename; never persist transient errors.
bool save_settings(const std::string& filename,const UserSettings& s);
}