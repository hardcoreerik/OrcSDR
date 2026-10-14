#include <algorithm>
#include "sstv_diag.hpp"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "sstv_archive.hpp"
#include "sstv_dashboard.hpp"
#include "sstv_receiver.hpp"
#include "sstv_status.hpp"

#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

namespace orcsdr::sstv_diag {
namespace {

enum class Scenario : uint8_t { none, robot36, robot36_novis, pd120 };

constexpr double kPi = 3.14159265358979323846;
constexpr uint32_t kRate = 48000;

// The ORC SSTV test pattern (320x240, 24-bit RGB), embedded from main/sstv_testpattern_320x240.rgb.
// It is the same picture the host colour test uses; its gray version drives the host fidelity test.
extern const uint8_t kPattern[] asm("_binary_sstv_testpattern_320x240_rgb_start");
constexpr uint32_t kPatternW = 320, kPatternH = 240;

// Y, R-Y and B-Y of the pattern as the receiver's colour decoder expects them (see sstv_receiver.cpp).
struct Rgb { double r, g, b; };
Rgb pattern_at(uint32_t x, uint32_t y) {
  const uint8_t* p = kPattern + (static_cast<size_t>(y) * kPatternW + x) * 3u;
  return {static_cast<double>(p[0]), static_cast<double>(p[1]), static_cast<double>(p[2])};
}
double clamp255(double v) { return v < 0 ? 0 : v > 255 ? 255 : v; }
double luma_of(Rgb c) { return clamp255(0.299 * c.r + 0.587 * c.g + 0.114 * c.b); }
// Chroma byte (R-Y when `red`, else B-Y) averaged over a block of source pixels.
double chroma_of(uint32_t x0, uint32_t y0, uint32_t across, uint32_t rows, bool red) {
  double sum = 0;
  for (uint32_t dy = 0; dy < rows; ++dy)
    for (uint32_t dx = 0; dx < across; ++dx) {
      const Rgb c = pattern_at(std::min(kPatternW - 1u, x0 + dx), std::min(kPatternH - 1u, y0 + dy));
      const double y = luma_of(c);
      sum += red ? clamp255((c.r - y) / 1.40 + 128.0) : clamp255((c.b - y) / 1.78 + 128.0);
    }
  return sum / (across * rows);
}

struct Script {
  Scenario scenario = Scenario::none;
  // Position in the transmission: stage 0 leader, 1 VIS, 2 image, 3 tail.
  uint8_t stage = 0;
  uint16_t row = 0;
  uint8_t part = 0;
  uint8_t vis_bit = 0;
  uint8_t vis_ones = 0;
  // Current tone segment.
  double hz = 0;
  uint32_t remaining = 0;     // samples left in the segment
  double phase = 0;
  uint32_t total_samples = 0;
  uint32_t speed = 1;
  uint32_t last_ms = 0;
  double carry = 0;           // fractional samples owed from pacing
  double cum_ms = 0;          // exact clock for pixel-level segments: no rounding drift over a line
  long planned = 0;           // samples planned so far on that clock
  uint16_t pix = 0;           // pixel index within a scan
  uint32_t started_ms = 0;
  bool done = false;
  uint32_t peak_samples_per_tick = 0;
};
Script g_script;

const Hooks* g_hooks = nullptr;
Hooks g_hooks_copy;   // handle_command's argument is not guaranteed to outlive the call

void say(const char* format, ...) {
  if (!g_hooks || !g_hooks->write_line) return;
  char line[320];
  va_list args;
  va_start(args, format);
  vsnprintf(line, sizeof(line) - 1, format, args);
  va_end(args);
  line[sizeof(line) - 1] = '\0';
  const size_t length = std::strlen(line);
  if (length + 1 < sizeof(line)) { line[length] = '\n'; line[length + 1] = '\0'; }
  g_hooks->write_line(line);
}

void segment(double hz, double ms) {
  g_script.hz = hz;
  g_script.remaining = static_cast<uint32_t>(ms * kRate / 1000.0 + 0.5);
}
// Pixel-level segment: boundaries come from a cumulative exact clock, so 320 pixels of 0.275 ms add up to
// exactly 88 ms instead of accumulating a rounding error that would walk the line sync.
void exact_segment(double hz, double ms) {
  g_script.cum_ms += ms;
  const long target = std::lround(g_script.cum_ms * kRate / 1000.0);
  g_script.hz = hz;
  g_script.remaining = static_cast<uint32_t>(target - g_script.planned);
  g_script.planned = target;
}
double luma_hz(double v) { return 1500.0 + 800.0 * v / 255.0; }

// Next tone of the synthetic transmission; false when it has finished.
bool next_segment() {
  Script& s = g_script;
  const bool pd = s.scenario == Scenario::pd120;
  switch (s.stage) {
    case 0:  // leader, break, leader (omitted for the headerless scenario)
      if (s.scenario == Scenario::robot36_novis) { s.stage = 2; s.cum_ms = 0; s.planned = 0; return next_segment(); }
      if (s.part == 0) { segment(1900, 300); s.part = 1; return true; }
      if (s.part == 1) { segment(1200, 10); s.part = 2; return true; }
      segment(1900, 300);
      s.stage = 1; s.part = 0; s.vis_bit = 0; s.vis_ones = 0;
      return true;
    case 1: {  // VIS: start bit, 7 data bits, parity, stop bit
      const uint8_t code = pd ? 95 : 8;
      if (s.part == 0) { segment(1200, 30); s.part = 1; return true; }
      if (s.part == 1) {
        if (s.vis_bit < 7) {
          const bool one = (code >> s.vis_bit) & 1u;
          if (one) ++s.vis_ones;
          ++s.vis_bit;
          segment(one ? 1100 : 1300, 30);
          return true;
        }
        segment((s.vis_ones & 1u) ? 1100 : 1300, 30);   // even parity
        s.part = 2;
        return true;
      }
      segment(1200, 30);
      s.stage = 2; s.part = 0; s.row = 0; s.pix = 0; s.cum_ms = 0; s.planned = 0;
      return true;
    }
    case 2: {  // image lines, painted from the test pattern
      if (pd) {
        if (s.row >= 248) { s.stage = 3; s.part = 0; return next_segment(); }
        // PD120 pair: sync 20, porch 2.08, Y0 (640 px x 0.19 ms), Cr and Cb neutral (121.6 ms each), Y1.
        switch (s.part) {
          case 0: exact_segment(1200, 20); s.part = 1; return true;
          case 1: exact_segment(1500, 2.08); s.part = 2; s.pix = 0; return true;
          case 2:
          case 5: {
            const uint32_t row = s.row * 2u + (s.part == 5 ? 1u : 0u);
            const uint32_t src = row * kPatternH / 496u;
            exact_segment(luma_hz(luma_of(pattern_at(s.pix / 2u, src))), 0.19);
            if (++s.pix >= 640) { s.pix = 0; s.part = s.part == 2 ? 3 : 6; }
            return true;
          }
          case 3:
          case 4: {   // Cr then Cb, full width, shared by the two rows of the pair
            const uint32_t y0 = std::min(kPatternH - 2u, (s.row * 2u) * kPatternH / 496u);
            exact_segment(luma_hz(chroma_of(s.pix / 2u, y0, 1, 2, s.part == 3)), 0.19);
            if (++s.pix >= 640) { s.pix = 0; s.part = s.part == 3 ? 4 : 5; }
            return true;
          }
          default: s.part = 0; ++s.row; return next_segment();
        }
      }
      if (s.row >= 240) { s.stage = 3; s.part = 0; return next_segment(); }
      // Robot 36 line: sync 9, porch 3, Y (320 px x 0.275 ms), chroma separator 4.5, porch 1.5, chroma 44.
      switch (s.part) {
        case 0: exact_segment(1200, 9); s.part = 1; return true;
        case 1: exact_segment(1500, 3); s.part = 2; s.pix = 0; return true;
        case 2:
          exact_segment(luma_hz(luma_of(pattern_at(s.pix, s.row))), 0.275);
          if (++s.pix >= 320) { s.pix = 0; s.part = 3; }
          return true;
        case 3: exact_segment((s.row & 1u) ? 2300 : 1500, 4.5); s.part = 4; return true;
        case 4: exact_segment(1900, 1.5); s.part = 5; return true;
        case 5: {   // chroma: R-Y on even lines, B-Y on odd, averaged over the line pair
          exact_segment(luma_hz(chroma_of(s.pix * 2u, s.row & ~1u, 2, 2, (s.row & 1u) == 0)), 0.275);
          if (++s.pix >= 160) { s.pix = 0; s.part = 0; ++s.row; }
          return true;
        }
        default: return false;
      }
    }
    default:   // tail: let the last line finish, then quiet
      if (s.part == 0) { segment(1900, 60); s.part = 1; return true; }
      if (s.part == 1) { segment(0, 2000); s.part = 2; return true; }
      return false;
  }
}

void begin(Scenario scenario, uint32_t speed, uint32_t now_ms) {
  g_script = Script{};
  g_script.scenario = scenario;
  g_script.speed = speed < 1 ? 1 : (speed > 8 ? 8 : speed);
  g_script.last_ms = now_ms;
  g_script.started_ms = now_ms;
  g_script.done = !next_segment();
}

const char* scenario_name(Scenario s) {
  switch (s) {
    case Scenario::robot36: return "ROBOT36";
    case Scenario::robot36_novis: return "ROBOT36_NOVIS";
    case Scenario::pd120: return "PD120";
    default: return "NONE";
  }
}

void print_index_tail() {
  const auto& cfg = orcsdr::sstv_archive::config();
  char path[96];
  std::snprintf(path, sizeof(path), "%s/index.csv", cfg.root);
  std::FILE* f = std::fopen(path, "rb");
  if (!f) { say("SSTV_INDEX unavailable"); return; }
  std::fseek(f, 0, SEEK_END);
  const long size = std::ftell(f);
  const long start = size > 900 ? size - 900 : 0;
  std::fseek(f, start, SEEK_SET);
  char buf[901];
  const size_t n = std::fread(buf, 1, 900, f);
  std::fclose(f);
  buf[n] = '\0';
  char* line = buf;
  if (start > 0) {                       // drop the partial first line
    char* nl = std::strchr(buf, '\n');
    line = nl ? nl + 1 : buf;
  }
  say("SSTV_INDEX bytes=%ld", size);
  while (*line) {
    char* nl = std::strchr(line, '\n');
    if (nl) *nl = '\0';
    if (*line) say("SSTV_INDEX_ROW %s", line);
    if (!nl) break;
    line = nl + 1;
  }
}

void print_status() {
  using namespace orcsdr::sstv;
  const Metrics m = metrics();
  say("SSTV_STATUS active=%d dashboard=%d state=\"%s\" mode=\"%s\" ident=%d score=%u row=%u/%u receiving=%d "
      "start_unknown=%d vis_ok=%lu vis_bad=%lu sync_locked=%u sync_missed=%u overruns=%lu gaps=%lu "
      "frames=%lu partial=%lu auto_locks=%lu tone_hz=%d audio_level_milli=%d audio_offered=%lu samples=%lu "
      "sync_tone_hz=%d sync_tone_pulses=%u status=\"%s\"",
      active() ? 1 : 0, ui_active() ? 1 : 0, state_name(m.state),
      m.receiving ? mode_name(m.received_mode) : "-", static_cast<int>(m.identification),
      static_cast<unsigned>(m.identification_score), static_cast<unsigned>(m.row),
      static_cast<unsigned>(m.rows), m.receiving ? 1 : 0, m.start_unknown ? 1 : 0,
      static_cast<unsigned long>(m.good_vis), static_cast<unsigned long>(m.bad_vis),
      static_cast<unsigned>(m.session_sync_locked), static_cast<unsigned>(m.session_sync_missed),
      static_cast<unsigned long>(m.audio_overruns), static_cast<unsigned long>(m.audio_gaps),
      static_cast<unsigned long>(m.frames), static_cast<unsigned long>(m.partial_frames),
      static_cast<unsigned long>(m.auto_sync_locks), static_cast<int>(m.tone_hz), static_cast<int>(m.audio_peak * 1000.0f),
      static_cast<unsigned long>(m.audio_offered), static_cast<unsigned long>(m.samples),
      static_cast<int>(m.sync_tone_hz + 0.5f), static_cast<unsigned>(m.sync_tone_pulses), m.status);
  if (g_hooks && g_hooks->tune_summary) {
    char tune[160];
    g_hooks->tune_summary(tune, sizeof(tune));
    say("SSTV_TUNE %s", tune);
  }
  const UiStats ui = ui_stats();
  say("SSTV_PERF service_us_last=%lu service_us_max=%lu service_us_avg=%lu service_cpu_pct=%u ring_max=%u/4096 "
      "frames_alive=%lu frame_kb=%lu draw_us_last=%lu draw_us_max=%lu draws=%lu",
      static_cast<unsigned long>(m.service_us_last), static_cast<unsigned long>(m.service_us_max),
      static_cast<unsigned long>(m.service_us_avg), static_cast<unsigned>(m.service_cpu_pct),
      static_cast<unsigned>(m.ring_fill_max), static_cast<unsigned long>(m.frames_alive),
      static_cast<unsigned long>(m.frame_bytes / 1024u), static_cast<unsigned long>(ui.draw_us_last),
      static_cast<unsigned long>(ui.draw_us_max), static_cast<unsigned long>(ui.draws));
#ifdef ESP_PLATFORM
  say("SSTV_MEM frame_alloc_failures=%lu internal_free=%u internal_min=%u internal_largest=%u psram_free=%u psram_min=%u psram_largest=%u",
      static_cast<unsigned long>(m.frame_alloc_failures),
      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
      static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)),
      static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
      static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM)),
      static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));
#endif
  const auto a = orcsdr::sstv_archive::stats();
  say("SSTV_ARCHIVE worker=%d enabled=%d queued=%lu saved=%lu failed=%lu skipped=%lu dropped=%lu pending=%lu "
      "write_last_ms=%lu write_max_ms=%lu write_retries=%lu last_error=\"%s\"",
      orcsdr::sstv_archive::worker_running() ? 1 : 0, orcsdr::sstv_archive::enabled() ? 1 : 0,
      static_cast<unsigned long>(a.queued), static_cast<unsigned long>(a.saved),
      static_cast<unsigned long>(a.failed), static_cast<unsigned long>(a.skipped),
      static_cast<unsigned long>(a.dropped_queue_full), static_cast<unsigned long>(a.pending),
      static_cast<unsigned long>(a.last_write_ms), static_cast<unsigned long>(a.max_write_ms),
      static_cast<unsigned long>(a.write_retries), orcsdr::sstv_archive::error_name(a.last_error));
  orcsdr::sstv_archive::Result recent[3];
  const size_t n = orcsdr::sstv_archive::recent_results(recent, 3);
  for (size_t i = 0; i < n; ++i)
    say("SSTV_RESULT session=%lu state=%s error=\"%s\" bytes=%lu write_ms=%lu errno=%d file=%s",
        static_cast<unsigned long>(recent[i].session_id),
        orcsdr::sstv_archive::state_name(recent[i].state),
        orcsdr::sstv_archive::error_name(recent[i].error), static_cast<unsigned long>(recent[i].bytes),
        static_cast<unsigned long>(recent[i].duration_ms), recent[i].os_error, recent[i].filename);
  say("SSTV_SELFTEST running=%d scenario=%s stage=%u row=%u samples=%lu speed=%lu peak_chunk=%lu",
      selftest_running() ? 1 : 0, scenario_name(g_script.scenario), static_cast<unsigned>(g_script.stage),
      static_cast<unsigned>(g_script.row), static_cast<unsigned long>(g_script.total_samples),
      static_cast<unsigned long>(g_script.speed), static_cast<unsigned long>(g_script.peak_samples_per_tick));
}

bool denied(const Hooks& hooks, bool authenticated) {
  if (authenticated) return false;
  if (hooks.write_line) hooks.write_line("SSTV_ERROR auth_required\n");
  return true;
}

}  // namespace

bool selftest_running() { return g_script.scenario != Scenario::none && !g_script.done; }
// Leaving the screen ends a running test: it must not resume mid-pattern later, and live audio must come back.
void abort_selftest() {
  g_script.done = true;
  orcsdr::sstv::set_live_audio_muted(false);
}

void tick(uint32_t now_ms) {
  orcsdr::sstv::set_live_audio_muted(selftest_running());   // test audio must not be mixed with live noise
  if (!selftest_running()) return;
  Script& s = g_script;
  if (s.last_ms == 0) {                         // first tick after the command: start the clock
    s.last_ms = now_ms == 0 ? 1 : now_ms;
    s.started_ms = s.last_ms;
    return;
  }
  // Audio owed since the last tick at the chosen speed, produced in blocks the demodulator's size.
  const uint32_t elapsed = now_ms - s.last_ms;
  s.last_ms = now_ms;
  s.carry += static_cast<double>(elapsed) * kRate / 1000.0 * s.speed;
  uint32_t owed = static_cast<uint32_t>(s.carry);
  if (owed > 6144) owed = 6144;                 // bounded work per tick; the next tick catches up
  s.carry -= owed;
  if (owed > s.peak_samples_per_tick) s.peak_samples_per_tick = owed;
  static int16_t block[480];
  while (owed > 0 && !s.done) {
    uint32_t n = owed < 480 ? owed : 480;
    uint32_t filled = 0;
    while (filled < n && !s.done) {
      if (s.remaining == 0 && !next_segment()) { s.done = true; break; }
      const uint32_t take = (n - filled) < s.remaining ? (n - filled) : s.remaining;
      const double delta = 2.0 * kPi * s.hz / kRate;
      for (uint32_t i = 0; i < take; ++i) {
        block[filled + i] = s.hz > 0 ? static_cast<int16_t>(11000.0 * std::sin(s.phase)) : 0;
        s.phase += delta;
        if (s.phase >= 2.0 * kPi) s.phase -= 2.0 * kPi;
      }
      filled += take;
      s.remaining -= take;
    }
    if (filled) orcsdr::sstv::offer_injected_48k(block, filled);
    s.total_samples += filled;
    owed -= n;
  }
  if (s.done) say("SSTV_SELFTEST_DONE scenario=%s samples=%lu wall_ms=%lu speed=%lu",
                  scenario_name(s.scenario), static_cast<unsigned long>(s.total_samples),
                  static_cast<unsigned long>(now_ms - s.started_ms), static_cast<unsigned long>(s.speed));
}

bool handle_command(const char* command, bool authenticated, const Hooks& hooks) {
  if (!command || std::strncmp(command, "SSTV ", 5) != 0) return false;
  g_hooks_copy = hooks;
  g_hooks = &g_hooks_copy;
  const char* args = command + 5;
  if (std::strcmp(args, "STATUS") == 0) { print_status(); return true; }
  if (std::strcmp(args, "INDEX") == 0) { print_index_tail(); return true; }
  if (std::strcmp(args, "LAST") == 0) {
    const auto image = orcsdr::sstv::archived_image(0);
    if (!image.pixels) { say("SSTV_LAST none=1"); return true; }
    orcsdr::sstv_status::ImageChips chips;
    orcsdr::sstv_status::image_chips(image, &chips);
    say("SSTV_LAST serial=%lu mode=\"%s\" rows=%u of=%u partial=%d sync_locked=%u sync_missed=%u start_unknown=%d "
        "ident=\"%s\" end=\"%s\" frequency_hz=%lu",
        static_cast<unsigned long>(image.serial), orcsdr::sstv::mode_name(image.mode),
        static_cast<unsigned>(image.decoded_rows), static_cast<unsigned>(image.height), image.partial ? 1 : 0,
        static_cast<unsigned>(image.sync_locked), static_cast<unsigned>(image.sync_missed), image.start_unknown ? 1 : 0,
        chips.identification, chips.ending, static_cast<unsigned long>(image.frequency_hz));
    return true;
  }
  if (denied(hooks, authenticated)) return true;
  if (std::strcmp(args, "OPEN") == 0) {
    if (hooks.open_dashboard) hooks.open_dashboard();
    say("SSTV_OK dashboard=%d", orcsdr::sstv::ui_active() ? 1 : 0);
    return true;
  }
  if (std::strcmp(args, "SELFTEST STOP") == 0) {
    g_script.done = true;
    say("SSTV_OK selftest=stopped");
    return true;
  }
  if (std::strncmp(args, "SELFTEST ", 9) == 0) {
    char name[24] = {};
    unsigned speed = 1;
    const int fields = std::sscanf(args + 9, "%23s SPEED %u", name, &speed);
    Scenario scenario = Scenario::none;
    if (fields >= 1) {
      if (std::strcmp(name, "ROBOT36") == 0) scenario = Scenario::robot36;
      else if (std::strcmp(name, "ROBOT36_NOVIS") == 0) scenario = Scenario::robot36_novis;
      else if (std::strcmp(name, "PD120") == 0) scenario = Scenario::pd120;
    }
    if (scenario == Scenario::none) {
      say("SSTV_ERROR usage=SSTV SELFTEST ROBOT36|ROBOT36_NOVIS|PD120 [SPEED 1..8] | STOP");
      return true;
    }
    if (!orcsdr::sstv::active()) {
      say("SSTV_ERROR dashboard_not_open use=SSTV OPEN");
      return true;
    }
    begin(scenario, speed, 0);   // pacing starts at the first tick (last_ms == 0)
    say("SSTV_OK selftest=%s speed=%u", scenario_name(scenario), g_script.speed);
    return true;
  }
  if (std::strncmp(args, "MODE ", 5) == 0 || std::strncmp(args, "IFBW ", 5) == 0 || std::strncmp(args, "STEP ", 5) == 0 ||
      std::strncmp(args, "RAILS ", 6) == 0 || std::strncmp(args, "REC ", 4) == 0) {
    char reply[96] = {};
    if (hooks.tune_command && hooks.tune_command(args, reply, sizeof(reply))) say("SSTV_OK %s", reply);
    else say("SSTV_ERROR %s", reply[0] ? reply : "unavailable");
    return true;
  }
  if (std::strncmp(args, "TUNE ", 5) == 0) {
    char* end = nullptr;
    const char first = args[5];
    const unsigned long long hz = (first >= '0' && first <= '9') ? std::strtoull(args + 5, &end, 10) : 0ull;   // digits only: no sign
    const bool ok = end != nullptr && end != args + 5 && *end == 0 && hz <= 0xFFFFFFFFull && hooks.tune &&
                    hooks.tune(static_cast<uint32_t>(hz));
    if (ok) say("SSTV_OK tuned_hz=%llu", hz);
    else say("SSTV_ERROR tune refused (dashboard closed or frequency out of range) use=SSTV TUNE <hz>");
    return true;
  }
  if (std::strncmp(args, "SAVE ", 5) == 0) {
    const bool live = std::strcmp(args + 5, "LIVE") == 0;
    if (!live && std::strcmp(args + 5, "NEWEST") != 0) {
      say("SSTV_ERROR usage=SSTV SAVE LIVE|NEWEST");
      return true;
    }
    const bool queued = hooks.save_image && hooks.save_image(live ? -1 : 0);
    say("SSTV_OK queued=%d", queued ? 1 : 0);
    return true;
  }
  say("SSTV_ERROR usage=SSTV STATUS|INDEX|LAST|OPEN|REC START <s>|TUNE <hz>|MODE <m>|IFBW <hz>|STEP <hz>|RAILS <arg>|SAVE LIVE|SAVE NEWEST|SELFTEST <scenario> [SPEED n]|SELFTEST STOP");
  return true;
}

}  // namespace orcsdr::sstv_diag
