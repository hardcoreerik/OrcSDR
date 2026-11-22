#include "serial_status.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace orcsdr::serial_status {

namespace {

// One formatted line, one write: what Serial.printf did for these replies.
void say(const Hooks& hooks, const char* format, ...) __attribute__((format(printf, 2, 3)));
void say(const Hooks& hooks, const char* format, ...) {
  char line[512];
  va_list args;
  va_start(args, format);
  const int length = vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  if (length > 0) hooks.write(line);
}

}  // namespace

bool handle_command(const char* command, bool authenticated, const Hooks& hooks) {
  (void)authenticated;
  if (strcmp(command, "RTL_STATUS") == 0) {
    const RtlStatus s = hooks.rtl_status();
    say(hooks, "RTL_SDR_STATUS connected=%s vid=%04x pid=%04x speed=%s serial=\"%s\"\n",
        s.connected ? "true" : "false", s.vid, s.pid, s.speed, s.serial);
    return true;
  }
  if (strcmp(command, "RTL_HOTPLUG_STATUS") == 0) {
    const HotplugStatus h = hooks.hotplug_status();
    say(hooks, "RTL_HOTPLUG_STATUS pending=%d ready=%d state=%s band=%s frequency_hz=%u\n", h.pending, h.ready,
        h.state, h.band, h.frequency_hz);
    return true;
  }
  if (strcmp(command, "RTL_ADSB STATUS") == 0) {
    hooks.print_adsb_status();
    return true;
  }
  if (strcmp(command, "AUDIO_CODEC") == 0) {
    // ES8388 power/output state: reg4 0x3C = LOUT1/ROUT1 (jack) + LOUT2/ROUT2 on;
    // reg46-49 are the LOUT1/ROUT1/LOUT2/ROUT2 volumes.
    static constexpr uint8_t regs[] = {2, 4, 25, 26, 27, 39, 42, 46, 47, 48, 49};
    hooks.write("AUDIO_CODEC");
    for (const uint8_t reg : regs) say(hooks, " r%u=0x%02x", static_cast<unsigned>(reg), hooks.codec_register(reg));
    const AudioState a = hooks.audio_state();
    say(hooks, " hp_detect=%d amp_pin=%d muted=%d running=%d volume=%u\n", a.hp_detect, a.amp_pin, a.muted,
        a.running, a.volume);
    return true;
  }
  if (strcmp(command, "RTL_DRIVER STATUS") == 0) {
    hooks.print_driver_status();
    return true;
  }
  if (strcmp(command, "RTL_AM_SCAN STATUS") == 0) {
    const AmScanStatus s = hooks.am_scan_status();
    say(hooks,
        "RTL_AM_SCAN_STATUS active=%d step=%u total=%u found=%u frequency_hz=%lu "
        "prompt=%d\n",
        s.active, s.step, s.total, s.found, s.frequency_hz, s.prompt);
    return true;
  }
  if (strcmp(command, "UI_SELF_CHECK") == 0) {
    const SelfCheck c = hooks.self_check();
    say(hooks, "UI_SELF_CHECK am=%d fm=%d home=%d shortwave=%d keypad=%d editor=%d\n", c.am, c.fm, c.home,
        c.shortwave, c.keypad, c.editor);
    return true;
  }
  if (strcmp(command, "HOME_PLOT STATUS") == 0) {
    char response[384];
    hooks.home_plot_status(response, sizeof(response));
    hooks.write_line(response);
    return true;
  }
  return false;
}

}  // namespace orcsdr::serial_status
