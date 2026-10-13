#include "sstv_settings.hpp"

namespace orcsdr::sstv_settings {

using orcsdr::sstv::AutoStrategy;
using orcsdr::sstv::Mode;

namespace {
constexpr char kKeyDetect[] = "sstv_detect";
constexpr char kKeyManual[] = "sstv_manual";
constexpr char kKeyAutosave[] = "sstv_autosave";   // key already used by the first SSTV release
}  // namespace

bool valid_manual_mode(uint8_t ordinal) {
  return ordinal >= static_cast<uint8_t>(Mode::robot36) && ordinal <= static_cast<uint8_t>(Mode::pd290);
}

Settings load(const Store& store) {
  Settings s;
  if (store.get_u8) {
    const uint8_t detect = store.get_u8(kKeyDetect, static_cast<uint8_t>(Detection::automatic));
    s.detection = detect <= static_cast<uint8_t>(Detection::manual) ? static_cast<Detection>(detect)
                                                                    : Detection::automatic;
    const uint8_t manual = store.get_u8(kKeyManual, static_cast<uint8_t>(Mode::robot36));
    s.manual_mode = valid_manual_mode(manual) ? static_cast<Mode>(manual) : Mode::robot36;
  }
  if (store.get_bool) s.autosave = store.get_bool(kKeyAutosave, true);
  return s;
}

void save(const Store& store, const Settings& s) {
  if (store.put_u8) {
    store.put_u8(kKeyDetect, static_cast<uint8_t>(s.detection));
    store.put_u8(kKeyManual, static_cast<uint8_t>(s.manual_mode));
  }
  if (store.put_bool) store.put_bool(kKeyAutosave, s.autosave);
}

void apply(const Settings& s) {
  switch (s.detection) {
    case Detection::manual:
      orcsdr::sstv::set_auto_strategy(AutoStrategy::vis_only);  // sync recovery needs auto mode
      orcsdr::sstv::set_mode(s.manual_mode);
      break;
    case Detection::vis_only:
      orcsdr::sstv::set_mode(Mode::auto_vis);
      orcsdr::sstv::set_auto_strategy(AutoStrategy::vis_only);
      break;
    default:
      orcsdr::sstv::set_mode(Mode::auto_vis);
      orcsdr::sstv::set_auto_strategy(AutoStrategy::vis_sync);
      break;
  }
}

Settings with_detection(Settings s, Detection detection) {
  s.detection = detection;
  return s;
}

Settings with_manual_mode(Settings s, Mode mode) {
  if (!valid_manual_mode(static_cast<uint8_t>(mode))) return s;
  s.manual_mode = mode;
  s.detection = Detection::manual;
  return s;
}

}  // namespace orcsdr::sstv_settings
