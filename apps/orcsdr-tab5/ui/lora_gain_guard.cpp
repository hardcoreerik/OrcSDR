#include "lora_gain_guard.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace orcsdr::lora_gain_guard {
namespace {

constexpr size_t kMaxGains = 40;

struct State {
  Config config{};
  Phase phase = Phase::watching;
  bool user_override = false;
  int ladder[kMaxGains]{};
  size_t ladder_count = 0;
  int index = -1;              // current ladder index while the guard owns the gain
  uint32_t clipping_since_ms = 0;
  uint32_t last_step_ms = 0;
  uint32_t clean_since_ms = 0;
  uint32_t receiving_since_ms = 0;  // 0 while LoRa is not streaming
  int ceiling_index = -1;      // lowest index known to clip, avoided until it expires
  uint32_t ceiling_set_ms = 0;
  bool probing = false;
  bool owns_gain = false;      // the guard wrote a manual step and the tuner AGC is not restored yet
  uint32_t probe_started_ms = 0;
  uint32_t reductions = 0;
  uint32_t probes = 0;
  float last_clip = 0;
} g;

void say(const Hooks& hooks, const char* format, ...) {
  if (hooks.write_line == nullptr) return;
  char line[200];
  va_list args;
  va_start(args, format);
  vsnprintf(line, sizeof(line) - 1, format, args);
  va_end(args);
  line[sizeof(line) - 1] = '\0';
  const size_t length = strlen(line);
  if (length + 1 < sizeof(line)) {
    line[length] = '\n';
    line[length + 1] = '\0';
  }
  hooks.write_line(line);
}

int nearest_index(int target_tenth_db) {
  int best = 0;
  int best_distance = 1 << 30;
  for (size_t i = 0; i < g.ladder_count; ++i) {
    const int distance = abs(g.ladder[i] - target_tenth_db);
    if (distance < best_distance) {
      best_distance = distance;
      best = static_cast<int>(i);
    }
  }
  return best;
}

bool load_ladder(const Hooks& hooks) {
  if (hooks.gains == nullptr) return false;
  g.ladder_count = hooks.gains(g.ladder, kMaxGains);
  return g.ladder_count > 1;
}

void go_watching() {
  g.phase = Phase::watching;
  g.index = -1;
  g.clipping_since_ms = 0;
  g.clean_since_ms = 0;
  g.probing = false;
}

}  // namespace

void configure(const Config& config) { g.config = config; }
Config config() { return g.config; }

Status status() {
  Status s{};
  s.phase = g.config.enabled ? g.phase : Phase::off;
  s.enabled = g.config.enabled;
  s.user_override = g.user_override;
  s.gain_tenth_db = g.index >= 0 && static_cast<size_t>(g.index) < g.ladder_count
                        ? g.ladder[g.index]
                        : -1;
  s.reductions = g.reductions;
  s.probes = g.probes;
  s.clipping_percent = g.last_clip;
  return s;
}

void note_user_gain_change() {
  g.user_override = true;
  g.owns_gain = false;  // the user chose this gain; never restore over it
  go_watching();
}

void release(const Hooks& hooks) {
  // owns_gain outlives go_watching(): a receiver stop clears the ladder cursor but the
  // dongle keeps the manual step until the tuner AGC is restored.
  const bool owned = g.index >= 0 || g.owns_gain;
  go_watching();
  g.user_override = false;
  g.ceiling_index = -1;
  g.owns_gain = false;
  if (owned && hooks.set_tuner_auto != nullptr) hooks.set_tuner_auto();
}

void tick(uint32_t now_ms, const Hooks& hooks) {
  if (hooks.lora_receiving == nullptr || hooks.clipping_percent == nullptr ||
      hooks.set_manual_gain == nullptr)
    return;
  const bool receiving = hooks.lora_receiving();
  if (!receiving) {
    g.receiving_since_ms = 0;
  } else if (g.receiving_since_ms == 0) {
    g.receiving_since_ms = now_ms == 0 ? 1 : now_ms;
    // Streaming restarted while a manual step from before the stop may still be set.
    if (g.owns_gain) release(hooks);
  }
  if (!g.config.enabled || g.user_override || !receiving) {
    if (g.phase != Phase::watching && !g.user_override) go_watching();
    return;
  }
  const float clip = hooks.clipping_percent();
  g.last_clip = clip;
  if (g.phase == Phase::watching && now_ms - g.receiving_since_ms < g.config.arm_delay_ms) {
    g.clipping_since_ms = 0;  // still inside the start-up transient
    return;
  }
  if (g.ceiling_index >= 0 && now_ms - g.ceiling_set_ms > g.config.ceiling_memory_ms)
    g.ceiling_index = -1;

  if (g.phase == Phase::watching) {
    if (clip <= g.config.engage_clip_percent) {
      g.clipping_since_ms = 0;
      return;
    }
    if (g.clipping_since_ms == 0) g.clipping_since_ms = now_ms;
    if (now_ms - g.clipping_since_ms < g.config.engage_ms) return;
    if (!load_ladder(hooks) || hooks.set_manual_gain == nullptr) return;
    g.index = nearest_index(g.config.start_gain_tenth_db);
    if (g.ceiling_index >= 0 && g.index >= g.ceiling_index) g.index = g.ceiling_index - 1;
    if (g.index < 0) g.index = 0;
    if (!hooks.set_manual_gain(g.ladder[g.index])) { g.index = -1; return; }
    g.owns_gain = true;
    g.phase = Phase::reducing;
    g.last_step_ms = now_ms;
    say(hooks, "RTL_LORA_GAIN_GUARD engage gain_tenth_db=%d clipping_percent=%.2f", g.ladder[g.index],
        static_cast<double>(clip));
    return;
  }

  if (g.index < 0) { go_watching(); return; }

  if (g.phase == Phase::reducing) {
    if (now_ms - g.last_step_ms < g.config.step_interval_ms) return;
    if (clip > g.config.release_clip_percent && g.index > 0) {
      --g.index;
      ++g.reductions;
      g.last_step_ms = now_ms;
      hooks.set_manual_gain(g.ladder[g.index]);
      say(hooks, "RTL_LORA_GAIN_GUARD reduce gain_tenth_db=%d clipping_percent=%.2f", g.ladder[g.index],
          static_cast<double>(clip));
      return;
    }
    g.phase = Phase::holding;
    g.clean_since_ms = now_ms;
    say(hooks, "RTL_LORA_GAIN_GUARD hold gain_tenth_db=%d clipping_percent=%.2f", g.ladder[g.index],
        static_cast<double>(clip));
    return;
  }

  // holding: stay put; periodically try one step up, and back off if that clips.
  if (g.probing) {
    if (clip > g.config.engage_clip_percent) {
      g.ceiling_index = g.index;
      g.ceiling_set_ms = now_ms;
      --g.index;
      g.probing = false;
      g.clean_since_ms = now_ms;
      hooks.set_manual_gain(g.ladder[g.index]);
      say(hooks, "RTL_LORA_GAIN_GUARD probe_failed back_to=%d clipping_percent=%.2f", g.ladder[g.index],
          static_cast<double>(clip));
    } else if (now_ms - g.probe_started_ms > 5000) {
      g.probing = false;
      g.clean_since_ms = now_ms;
    }
    return;
  }
  if (clip > g.config.engage_clip_percent) {
    g.phase = Phase::reducing;
    g.last_step_ms = now_ms;
    return;
  }
  if (clip > g.config.release_clip_percent) g.clean_since_ms = now_ms;
  const bool can_go_up = static_cast<size_t>(g.index) + 1 < g.ladder_count &&
                         (g.ceiling_index < 0 || g.index + 1 < g.ceiling_index);
  if (can_go_up && now_ms - g.clean_since_ms >= g.config.probe_up_ms) {
    ++g.index;
    ++g.probes;
    g.probing = true;
    g.probe_started_ms = now_ms;
    hooks.set_manual_gain(g.ladder[g.index]);
    say(hooks, "RTL_LORA_GAIN_GUARD probe gain_tenth_db=%d", g.ladder[g.index]);
  }
}

bool handle_command(const char* command, bool authenticated, const Hooks& hooks) {
  if (command == nullptr || strncmp(command, "RTL_LORA_GAIN ", 14) != 0) return false;
  const char* args = command + 14;
  if (strcmp(args, "STATUS") == 0) {
    static constexpr const char* kPhase[] = {"off", "watching", "reducing", "holding"};
    const Status s = status();
    say(hooks,
        "RTL_LORA_GAIN_STATUS guard=%s phase=%s override=%d gain_tenth_db=%d clipping_percent=%.2f "
        "engage_pct=%.2f release_pct=%.2f reductions=%lu probes=%lu",
        s.enabled ? "on" : "off", kPhase[static_cast<int>(s.phase)], s.user_override ? 1 : 0,
        s.gain_tenth_db, static_cast<double>(s.clipping_percent),
        static_cast<double>(g.config.engage_clip_percent),
        static_cast<double>(g.config.release_clip_percent),
        static_cast<unsigned long>(s.reductions), static_cast<unsigned long>(s.probes));
    return true;
  }
  if (!authenticated) {
    say(hooks, "RTL_LORA_GAIN_ERROR auth_required");
    return true;
  }
  if (strcmp(args, "GUARD ON") == 0 || strcmp(args, "GUARD OFF") == 0) {
    const bool on = args[6] == 'O' && args[7] == 'N';
    g.config.enabled = on;
    if (!on) release(hooks);
    else g.user_override = false;
    say(hooks, "RTL_LORA_GAIN_OK guard=%s", on ? "on" : "off");
    return true;
  }
  float limit = 0;
  if (sscanf(args, "LIMIT %f", &limit) == 1 && limit > 0.0f && limit <= 50.0f) {
    g.config.engage_clip_percent = limit;
    g.config.release_clip_percent = limit * 0.2f;
    say(hooks, "RTL_LORA_GAIN_OK engage_pct=%.2f release_pct=%.2f",
        static_cast<double>(g.config.engage_clip_percent),
        static_cast<double>(g.config.release_clip_percent));
    return true;
  }
  say(hooks, "RTL_LORA_GAIN_ERROR usage=RTL_LORA_GAIN STATUS|GUARD ON|GUARD OFF|LIMIT <percent>");
  return true;
}

}  // namespace orcsdr::lora_gain_guard
