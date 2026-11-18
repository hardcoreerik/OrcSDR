// Host test for shortwave_actions and shortwave_library_actions: every action kind, with fake hooks that log each call
// in order and each console write on its own. The expectations were read off the former handle_shortwave_dashboard_action
// in main.cpp (commit 033bfc42), so a change in order, in a console line or in a stored value fails here.
#include "shortwave_actions.hpp"

#include "shortwave_audio_dsp.hpp"
#include "shortwave_ssb.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

using orcsdr::shortwave::Action;
using orcsdr::shortwave::ActionKind;
namespace sa = orcsdr::shortwave_actions;
namespace lib = orcsdr::shortwave_library_actions;
namespace ssb = orcsdr::shortwave_ssb;

int g_failures = 0;
#define CHECK(condition)                                                                   \
  do {                                                                                     \
    if (!(condition)) {                                                                    \
      std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition);                    \
      ++g_failures;                                                                        \
    }                                                                                      \
  } while (0)
#define CHECK_LOG(expected)                                                                \
  do {                                                                                     \
    if (g_log != (expected)) {                                                             \
      std::fprintf(stderr, "FAIL line %d: log is\n%s\nexpected\n%s\n", __LINE__,           \
                   join(g_log).c_str(), join(expected).c_str());                           \
      ++g_failures;                                                                        \
    }                                                                                      \
  } while (0)

std::vector<std::string> g_log;

std::string join(const std::vector<std::string>& lines) {
  std::string out;
  for (const auto& line : lines) out += "  " + line + "\n";
  return out;
}

void note(const char* format, ...) __attribute__((format(printf, 1, 2)));
void note(const char* format, ...) {
  char buffer[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  g_log.emplace_back(buffer);
}

// ---- fake device ----
uint32_t g_frequency = 7100000;
uint32_t g_step = 1000;
std::atomic<uint32_t> g_filter{6000};
std::atomic<uint32_t> g_span{2400000};
std::atomic<bool> g_boost{false};
std::atomic<bool> g_hunt_requested{false};
std::atomic<bool> g_hunt_cancel{false};
std::atomic<uint8_t> g_hunt_band{0};
bool g_running = false;
bool g_gain_ready = true;
bool g_gain_auto = true;
bool g_agc_ready = true;
bool g_audio_enabled = true;
size_t g_band_count = 5;
bool g_card = true;
bool g_save_ok = true;
bool g_clock_valid = true;
float g_dbfs = -41.5f;
orcsdr::shortwave::LibraryState g_library;
char g_label[64] = "WWV";
char g_notes[160] = "clear";
char g_antenna[64] = "";
char g_log_notes[240] = "fine";

uint32_t span_cap() { return 3000000; }

const char* mode_at(uint32_t) { return "AM"; }

sa::Hooks make_hooks() {
  sa::Hooks h;
  h.io.write = [](const char* t) { note("W:%s", t); };
  h.io.write_line = [](const char* t) { note("L:%s", t); };
  h.ui_frequency_hz = &g_frequency;
  h.shortwave_step_hz = &g_step;
  h.filter_bandwidth_hz = &g_filter;
  h.scope_span_hz = &g_span;
  h.audio_boost = &g_boost;
  h.hunt_requested = &g_hunt_requested;
  h.hunt_cancel_requested = &g_hunt_cancel;
  h.hunt_band_index = &g_hunt_band;
  h.scope_span_min_hz = 120000;
  h.volume_step = 16;
  h.clamp_frequency = [](uint32_t hz) { note("clamp %u", static_cast<unsigned>(hz)); return hz + 1; };
  h.capture_running = []() { return g_running; };
  h.request_hot_retune = [](uint32_t hz) { note("hot %u", static_cast<unsigned>(hz)); };
  h.queue_local_listen = [](uint32_t hz) { note("queue %u", static_cast<unsigned>(hz)); };
  h.note_tuned = [](uint32_t hz) { note("tuned %u", static_cast<unsigned>(hz)); };
  h.step_frequency = [](int d) { note("step %d", d); return g_frequency + static_cast<uint32_t>(d * 5); };
  h.next_tuning_step = [](uint32_t s) { note("next_step %u", static_cast<unsigned>(s)); return s * 10; };
  h.audio_reset_demod_filters = []() { note("reset_filters"); };
  h.reset_spectrum_renderer = []() { note("reset_spectrum"); };
  h.clamp_filter_hz = [](uint32_t hz) { note("clamp_filter %u", static_cast<unsigned>(hz)); return hz / 2; };
  h.span_cap_hz = span_cap;
  h.audio_user_enabled = []() { return g_audio_enabled; };
  h.set_audio_user_enabled = [](bool e) { note("audio_enabled %d", e); };
  h.adjust_volume = [](int d) { note("volume %d", d); };
  h.tuner_gain_ready = []() { return g_gain_ready; };
  h.tuner_gain_is_auto = []() { return g_gain_auto; };
  h.set_tuner_gain_mode = [](bool a) { note("gain_mode %d", a); return "ESP_OK"; };
  h.set_tuner_gain_manual = []() { note("gain_manual"); };
  h.set_tuner_gain = [](int t) { note("gain %d", t); return "ESP_OK"; };
  h.rtl_agc_available = []() { return g_agc_ready; };
  h.set_rtl_agc = [](bool e) { note("rtl_agc %d", e); return "ESP_OK"; };
  h.hunt_band_count = []() { return g_band_count; };
  h.open_radio_defaults_settings = []() { note("open_settings"); };
  h.leave_dashboard = []() { note("leave"); };
  h.show_home = []() { note("home"); };
  h.refresh_active_screen = []() { note("refresh"); };

  auto& l = h.library;
  l.io = h.io;
  l.state = &g_library;
  l.ensure_loaded = []() { note("ensure_loaded"); };
  l.save_memories = [](const orcsdr::shortwave::MemoryTable& m, char* e, size_t n) {
    note("save_memories %u", static_cast<unsigned>(m.size()));
    if (!g_card) return false;
    if (!g_save_ok) { snprintf(e, n, "disk_full"); return false; }
    return true;
  };
  l.save_logs = [](const orcsdr::shortwave::LogTable& t, char* e, size_t n) {
    note("save_logs %u", static_cast<unsigned>(t.size()));
    if (!g_card) return false;
    if (!g_save_ok) { snprintf(e, n, "disk_full"); return false; }
    return true;
  };
  l.export_logs = [](const orcsdr::shortwave::LogTable& t, char* e, size_t n) {
    note("export_logs %u", static_cast<unsigned>(t.size()));
    if (!g_card) return false;
    if (!g_save_ok) { snprintf(e, n, "no_space"); return false; }
    return true;
  };
  l.now = []() { return lib::Clock{1700000000u, g_clock_valid}; };
  l.ui_frequency_hz = []() { return g_frequency; };
  l.filter_bandwidth_hz = []() { return g_filter.load(); };
  l.signal_dbfs = []() { return g_dbfs; };
  l.mode_name_at = mode_at;
  l.snapshot_device = [](char* out, size_t n) { note("snapshot_device"); snprintf(out, n, "Blog V4"); };
  l.pending_memory_label = []() -> const char* { return g_label; };
  l.pending_memory_notes = []() -> const char* { return g_notes; };
  l.pending_log_antenna = []() -> const char* { return g_antenna; };
  l.pending_log_notes = []() -> const char* { return g_log_notes; };
  return h;
}

void reset() {
  g_log.clear();
  g_frequency = 7100000;
  g_step = 1000;
  g_filter = 6000;
  g_span = 2400000;
  g_boost = false;
  g_hunt_requested = false;
  g_hunt_cancel = false;
  g_hunt_band = 0;
  g_running = false;
  g_gain_ready = true;
  g_gain_auto = true;
  g_agc_ready = true;
  g_audio_enabled = true;
  g_band_count = 5;
  g_card = true;
  g_save_ok = true;
  g_clock_valid = true;
  g_library.memories.clear();
  g_library.logs.clear();
  g_library.status = orcsdr::shortwave::StorageStatus::unavailable;
  snprintf(g_antenna, sizeof(g_antenna), "%s", "");
  ssb::control().choice.store(0);
  ssb::control().clarifier_hz.store(0);
}

void run(ActionKind kind, int32_t value = 0) {
  const auto hooks = make_hooks();
  sa::handle(Action{kind, value}, hooks);
}

using Log = std::vector<std::string>;

void test_tuning() {
  reset();
  run(ActionKind::tune_hz, 5000000);
  CHECK_LOG((Log{"clamp 5000000", "tuned 5000001", "queue 5000001", "refresh"}));
  reset(); g_running = true;
  run(ActionKind::tune_hz, 5000000);
  CHECK_LOG((Log{"clamp 5000000", "tuned 5000001", "hot 5000001", "refresh"}));
  reset();
  run(ActionKind::open_frequency);
  CHECK_LOG(Log{});   // no refresh
  reset();
  run(ActionKind::step_down);
  CHECK_LOG((Log{"step -1", "clamp 7099995", "tuned 7099996", "queue 7099996", "refresh"}));
  reset(); g_running = true;
  run(ActionKind::step_up);
  CHECK_LOG((Log{"step 1", "clamp 7100005", "tuned 7100006", "hot 7100006", "refresh"}));
  reset();
  run(ActionKind::step_cycle);
  CHECK_LOG((Log{"next_step 1000", "refresh"}));
  CHECK(g_step == 10000);
  reset();
  run(ActionKind::none);
  CHECK_LOG((Log{"refresh"}));
}

void test_filter_and_span() {
  const Log reset_and_refresh{"reset_filters", "reset_spectrum", "refresh"};
  struct Row { uint32_t from; ActionKind kind; uint32_t to; };
  const Row rows[] = {
      {3000, ActionKind::filter_cycle, 6000}, {4000, ActionKind::filter_cycle, 6000},
      {6000, ActionKind::filter_cycle, 9000}, {9000, ActionKind::filter_cycle, 4000},
      {4000, ActionKind::filter_down, 4000}, {6000, ActionKind::filter_down, 4000},
      {9000, ActionKind::filter_down, 6000}, {3000, ActionKind::filter_up, 6000},
      {5999, ActionKind::filter_up, 6000}, {6000, ActionKind::filter_up, 9000},
      {9000, ActionKind::filter_up, 9000},
  };
  for (const Row& row : rows) {
    reset(); g_filter = row.from;
    run(row.kind);
    CHECK_LOG(reset_and_refresh);
    CHECK(g_filter.load() == row.to);
  }
  reset();
  run(ActionKind::filter_bandwidth_hz, 7000);
  CHECK_LOG((Log{"clamp_filter 7000", "reset_filters", "reset_spectrum", "refresh"}));
  CHECK(g_filter.load() == 3500);

  const Log spectrum_and_refresh{"reset_spectrum", "refresh"};
  reset(); g_span = 2400000;
  run(ActionKind::span_down);
  CHECK_LOG(spectrum_and_refresh); CHECK(g_span.load() == 1200000);
  reset(); g_span = 200000;
  run(ActionKind::span_down);
  CHECK(g_span.load() == 120000);
  reset(); g_span = 2400000;
  run(ActionKind::span_up);
  CHECK(g_span.load() == 3000000);
  reset(); g_span = 1000000;
  run(ActionKind::span_up);
  CHECK(g_span.load() == 2000000);
  reset();
  run(ActionKind::span_hz, 10);
  CHECK_LOG(spectrum_and_refresh); CHECK(g_span.load() == 120000);
  reset();
  run(ActionKind::span_hz, 9000000);
  CHECK(g_span.load() == 3000000);
  reset();
  run(ActionKind::span_hz, 1500000);
  CHECK(g_span.load() == 1500000);
}

void test_sound_and_gain() {
  reset(); g_audio_enabled = true;
  run(ActionKind::sound_toggle);
  CHECK_LOG((Log{"audio_enabled 0", "refresh"}));
  reset(); g_audio_enabled = false;
  run(ActionKind::sound_toggle);
  CHECK_LOG((Log{"audio_enabled 1", "refresh"}));
  reset(); run(ActionKind::volume_down);
  CHECK_LOG((Log{"volume -16", "refresh"}));
  reset(); run(ActionKind::volume_up);
  CHECK_LOG((Log{"volume 16", "refresh"}));

  reset(); g_gain_auto = true;
  run(ActionKind::gain_auto);
  CHECK_LOG((Log{"gain_mode 0", "W:RTL_SHORTWAVE_TUNER_AGC enabled=0 result=ESP_OK\n", "refresh"}));
  reset(); g_gain_auto = false;
  run(ActionKind::gain_auto);
  CHECK_LOG((Log{"gain_mode 1", "W:RTL_SHORTWAVE_TUNER_AGC enabled=1 result=ESP_OK\n", "refresh"}));
  reset(); g_gain_ready = false;
  run(ActionKind::gain_auto);
  CHECK_LOG((Log{"refresh"}));

  reset();
  run(ActionKind::gain_tenth_db, 197);
  CHECK_LOG((Log{"gain_manual", "gain 197", "W:RTL_SHORTWAVE_RF_GAIN gain_tenth_db=197 result=ESP_OK\n", "refresh"}));
  reset(); g_gain_ready = false;
  run(ActionKind::gain_tenth_db, 197);
  CHECK_LOG((Log{"refresh"}));

  reset();
  run(ActionKind::rtl_agc, 1);
  CHECK_LOG((Log{"rtl_agc 1", "W:RTL_SHORTWAVE_RTL_AGC enabled=1 result=ESP_OK\n", "refresh"}));
  reset();
  run(ActionKind::rtl_agc, 0);
  CHECK_LOG((Log{"rtl_agc 0", "W:RTL_SHORTWAVE_RTL_AGC enabled=0 result=ESP_OK\n", "refresh"}));
  reset(); g_agc_ready = false;
  run(ActionKind::rtl_agc, 1);
  CHECK_LOG((Log{"refresh"}));
}

void test_mode_clarifier_dsp() {
  reset();
  run(ActionKind::mode_cycle);
  CHECK_LOG((Log{"W:RTL_SHORTWAVE_MODE choice=1 mode=AM\n", "refresh"}));
  CHECK(ssb::control().choice.load() == 1);
  reset(); ssb::control().choice.store(2);
  run(ActionKind::mode_cycle);
  CHECK_LOG((Log{"W:RTL_SHORTWAVE_MODE choice=3 mode=LSB\n", "refresh"}));
  reset(); ssb::control().choice.store(3);
  run(ActionKind::mode_cycle);
  CHECK(ssb::control().choice.load() == 0);

  reset();
  run(ActionKind::clarifier_up);
  CHECK(ssb::control().clarifier_hz.load() == 50);
  run(ActionKind::clarifier_down);
  run(ActionKind::clarifier_down);
  CHECK(ssb::control().clarifier_hz.load() == -50);
  ssb::control().clarifier_hz.store(ssb::kClarifierLimitHz);
  run(ActionKind::clarifier_up);
  CHECK(ssb::control().clarifier_hz.load() == ssb::kClarifierLimitHz);

  reset(); g_boost = false;
  run(ActionKind::audio_boost, 1);
  CHECK_LOG((Log{"W:RTL_SHORTWAVE_AUDIO_BOOST enabled=1 gain_db=12\n", "refresh"}));
  CHECK(g_boost.load());
  reset(); g_boost = true;
  run(ActionKind::audio_boost, 0);
  CHECK(!g_boost.load());

  reset(); g_boost = true; g_filter = 9000;
  run(ActionKind::clean_audio);
  CHECK_LOG((Log{"reset_filters", "reset_spectrum",
                 "L:RTL_SHORTWAVE_DSP clean=1 bandwidth_hz=6000 nr=low notch=auto sql=off", "refresh"}));
  CHECK(g_filter.load() == 6000);
  CHECK(!g_boost.load());

  namespace dsp = orcsdr::shortwave::audio_dsp;
  reset(); run(ActionKind::clean_audio); g_log.clear();
  const auto before = dsp::settings();
  run(ActionKind::noise_reduction_cycle);
  CHECK(g_log.size() == 2 && g_log[1] == "refresh");
  char expected[64];
  snprintf(expected, sizeof(expected), "W:RTL_SHORTWAVE_DSP nr=%u\n", static_cast<unsigned>(dsp::settings().noise_reduction));
  CHECK(g_log[0] == expected);
  CHECK(dsp::settings().noise_reduction != before.noise_reduction);
  g_log.clear();
  const bool notch_before = dsp::settings().auto_notch;
  run(ActionKind::auto_notch_toggle);
  snprintf(expected, sizeof(expected), "W:RTL_SHORTWAVE_DSP notch=%d\n", dsp::settings().auto_notch);
  CHECK(g_log.size() == 2 && g_log[0] == expected);
  CHECK(dsp::settings().auto_notch != notch_before);
  g_log.clear();
  run(ActionKind::squelch_cycle);
  snprintf(expected, sizeof(expected), "W:RTL_SHORTWAVE_DSP squelch=%u\n", static_cast<unsigned>(dsp::settings().squelch));
  CHECK(g_log.size() == 2 && g_log[0] == expected);
  g_log.clear();
  run(ActionKind::squelch_up);
  CHECK_LOG((Log{"refresh"}));
  run(ActionKind::squelch_down);
}

void test_hunt_and_navigation() {
  reset();
  run(ActionKind::hunt_start, 3);
  CHECK_LOG((Log{"refresh"}));
  CHECK(g_hunt_requested.load() && g_hunt_band.load() == 3);
  reset();
  run(ActionKind::hunt_start, 5);   // == band count: refused
  CHECK(!g_hunt_requested.load() && g_hunt_band.load() == 0);
  reset();
  run(ActionKind::hunt_start, -1);
  CHECK(!g_hunt_requested.load());
  reset(); g_hunt_requested = true;
  run(ActionKind::hunt_cancel);
  CHECK(!g_hunt_requested.load() && g_hunt_cancel.load());
  CHECK_LOG((Log{"refresh"}));

  reset();
  run(ActionKind::open_settings);
  CHECK_LOG((Log{"open_settings"}));   // no refresh
  reset(); g_hunt_requested = true;
  run(ActionKind::exit_home);
  CHECK_LOG((Log{"leave", "home"}));   // no refresh
  CHECK(!g_hunt_requested.load() && g_hunt_cancel.load());
}

orcsdr::shortwave::Memory memory(uint32_t hz, const char* station) {
  orcsdr::shortwave::Memory m{};
  m.frequency_hz = hz;
  m.bandwidth_hz = 6000;
  snprintf(m.mode, sizeof(m.mode), "AM");
  snprintf(m.station, sizeof(m.station), "%s", station);
  return m;
}

void test_memories() {
  reset();
  run(ActionKind::save_memory);
  CHECK_LOG((Log{"ensure_loaded", "save_memories 1",
                 "W:RTL_SHORTWAVE_MEMORY saved frequency_hz=7100000 label=WWV\n", "refresh"}));
  CHECK(g_library.memories.size() == 1);
  CHECK(g_library.status == orcsdr::shortwave::StorageStatus::ready);
  CHECK(g_library.memories.at(0)->saved_utc == 1700000000u);
  CHECK(std::strcmp(g_library.memories.at(0)->notes, "clear") == 0);
  // The same frequency and mode again: a duplicate prints nothing and changes nothing.
  g_log.clear();
  g_library.status = orcsdr::shortwave::StorageStatus::unavailable;
  run(ActionKind::save_memory);
  CHECK_LOG((Log{"ensure_loaded", "refresh"}));
  CHECK(g_library.memories.size() == 1);
  CHECK(g_library.status == orcsdr::shortwave::StorageStatus::unavailable);
  // Write failure: the new record is taken back out.
  reset(); g_save_ok = false;
  run(ActionKind::save_memory);
  CHECK_LOG((Log{"ensure_loaded", "save_memories 1",
                 "W:RTL_SHORTWAVE_MEMORY save_failed result=0 error=disk_full\n", "refresh"}));
  CHECK(g_library.memories.size() == 0);
  CHECK(g_library.status == orcsdr::shortwave::StorageStatus::write_failed);
  // No card: nothing is written, error stays empty.
  reset(); g_card = false;
  run(ActionKind::save_memory);
  CHECK_LOG((Log{"ensure_loaded", "save_memories 1",
                 "W:RTL_SHORTWAVE_MEMORY save_failed result=0 error=\n", "refresh"}));
  CHECK(g_library.memories.size() == 0);
  // An invalid record (frequency 0) is refused before any write.
  reset(); g_frequency = 0;
  run(ActionKind::save_memory);
  CHECK_LOG((Log{"ensure_loaded", "W:RTL_SHORTWAVE_MEMORY save_failed result=3 error=\n", "refresh"}));

  // update / favourite / delete
  reset();
  g_library.memories.upsert(memory(7100000, "OLD"));
  g_library.memories.upsert(memory(9500000, "BBC"));
  run(ActionKind::update_memory, 1);
  CHECK_LOG((Log{"ensure_loaded", "save_memories 2",
                 "W:RTL_SHORTWAVE_MEMORY changed action=" + std::to_string(static_cast<unsigned>(ActionKind::update_memory)) + " index=1\n",
                 "refresh"}));
  CHECK(std::strcmp(g_library.memories.at(1)->station, "WWV") == 0);
  g_log.clear();
  run(ActionKind::favorite_memory, 0);
  CHECK(g_log.size() == 4 && g_log[1] == "save_memories 2");
  g_log.clear();
  run(ActionKind::delete_memory, 0);
  CHECK(g_library.memories.size() == 1);
  CHECK(std::strcmp(g_library.memories.at(0)->station, "WWV") == 0);
  // delete with a failing write puts the record back where it was
  reset();
  g_library.memories.upsert(memory(7100000, "A"));
  g_library.memories.upsert(memory(9500000, "B"));
  g_save_ok = false;
  run(ActionKind::delete_memory, 0);
  CHECK(g_library.memories.size() == 2 && std::strcmp(g_library.memories.at(0)->station, "A") == 0);
  CHECK(g_library.status == orcsdr::shortwave::StorageStatus::write_failed);
  CHECK(g_log.size() == 4 && g_log[2].find("change_failed") != std::string::npos &&
        g_log[2].find("index=0 error=disk_full") != std::string::npos);
  // a failing update restores the former text
  g_log.clear();
  run(ActionKind::update_memory, 1);
  CHECK(std::strcmp(g_library.memories.at(1)->station, "B") == 0);
  // a record that is not there
  reset();
  run(ActionKind::delete_memory, 7);
  CHECK(g_log.size() == 3 && g_log[1].find("change_failed action=") != std::string::npos &&
        g_log[1].find("index=7 error=") != std::string::npos);   // nothing to save: no write is tried
  reset();
  run(ActionKind::delete_memory, -1);
  CHECK(g_log[1].find("index=4294967295 error=") != std::string::npos);
}

void test_logs() {
  reset(); g_clock_valid = false;
  run(ActionKind::save_log);
  CHECK_LOG((Log{"ensure_loaded", "L:RTL_SHORTWAVE_LOG save_failed error=utc_clock_not_set", "refresh"}));

  reset();
  g_library.memories.upsert(memory(7100000, "Named"));
  run(ActionKind::save_log);
  CHECK_LOG((Log{"ensure_loaded", "snapshot_device", "save_logs 1",
                 "W:RTL_SHORTWAVE_LOG saved frequency_hz=7100000 antenna=Not recorded\n", "refresh"}));
  const auto* entry = g_library.logs.at(0);
  CHECK(entry != nullptr);
  CHECK(std::strcmp(entry->station, "Named") == 0);
  CHECK(std::strcmp(entry->device, "Blog V4") == 0);
  CHECK(std::strcmp(entry->notes, "fine") == 0);
  CHECK(entry->timestamp_utc == 1700000000u && entry->signal_dbfs == -41.5f);
  CHECK(g_library.status == orcsdr::shortwave::StorageStatus::ready);

  reset(); g_save_ok = false; snprintf(g_antenna, sizeof(g_antenna), "Dipole");
  run(ActionKind::save_log);
  CHECK_LOG((Log{"ensure_loaded", "snapshot_device", "save_logs 1",
                 "W:RTL_SHORTWAVE_LOG save_failed result=0 error=disk_full\n", "refresh"}));
  CHECK(g_library.logs.size() == 0);

  reset(); snprintf(g_antenna, sizeof(g_antenna), "Loop");
  run(ActionKind::save_log);
  g_log.clear();
  snprintf(g_antenna, sizeof(g_antenna), "Vertical");
  run(ActionKind::update_log, 0);
  CHECK(g_log.size() == 4 && g_log[1] == "save_logs 1");
  CHECK(std::strcmp(g_library.logs.at(0)->antenna, "Vertical") == 0);
  g_log.clear();
  g_save_ok = false;
  run(ActionKind::update_log, 0);
  CHECK(std::strcmp(g_library.logs.at(0)->antenna, "Vertical") == 0);
  g_save_ok = true;
  g_log.clear();
  run(ActionKind::delete_log, 0);
  CHECK(g_library.logs.size() == 0);
  CHECK(g_log[2].find("RTL_SHORTWAVE_LOG changed action=") != std::string::npos);

  reset();
  run(ActionKind::export_log);
  CHECK_LOG((Log{"export_logs 0", "W:RTL_SHORTWAVE_EXPORT ok logs=0 formats=csv,adif\n", "refresh"}));
  reset(); g_card = false;
  run(ActionKind::export_log);
  CHECK_LOG((Log{"export_logs 0", "W:RTL_SHORTWAVE_EXPORT failed error=\n", "refresh"}));
}

void test_handle_reports_kind() {
  const auto hooks = make_hooks();
  const ActionKind library_kinds[] = {ActionKind::save_memory, ActionKind::update_memory, ActionKind::favorite_memory,
                                      ActionKind::delete_memory, ActionKind::save_log, ActionKind::update_log,
                                      ActionKind::delete_log, ActionKind::export_log};
  for (ActionKind kind : library_kinds) {
    reset();
    CHECK(lib::handle(Action{kind, 0}, hooks.library));
  }
  CHECK(!lib::handle(Action{ActionKind::tune_hz, 0}, hooks.library));
  CHECK(!lib::handle(Action{ActionKind::none, 0}, hooks.library));
}

}  // namespace

int main() {
  test_tuning();
  test_filter_and_span();
  test_sound_and_gain();
  test_mode_clarifier_dsp();
  test_hunt_and_navigation();
  test_memories();
  test_logs();
  test_handle_reports_kind();
  if (g_failures != 0) {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  std::puts("PASS shortwave_actions");
  return 0;
}
