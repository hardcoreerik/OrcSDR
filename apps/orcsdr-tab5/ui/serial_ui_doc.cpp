#include "serial_ui_doc.hpp"

#include <cstdio>
#include <cstring>

namespace orcsdr::serial_ui_doc {
namespace {

// UI_CAPTURE_DONE in one write, as the dispatcher's printf. valid_slug() allows at most 63 characters, so it always
// fits. noinline: UI_DOC_SHOW goes on to call the ~6 KB renderer, so its frame must not carry this buffer.
__attribute__((noinline)) void write_capture_done(const Hooks& hooks, const char* slug, const CaptureResult& result) {
  char done[384];
  snprintf(done, sizeof(done),
           "UI_CAPTURE_DONE slug=%s path=\"/orcsdr/screenshots/%s.bmp\" "
           "bytes=%u width=%u height=%u firmware=\"%s\" sha256=",
           slug, slug, static_cast<unsigned>(result.bytes), result.width, result.height, hooks.firmware_version());
  hooks.write(done);
  hooks.write_hex(result.sha256, sizeof(result.sha256));
  hooks.write("\r\n");   // what Serial.println() wrote; never an empty write_line (USB-JTAG logs 0-byte writes)
}

}  // namespace

bool handle_command(const char* command, bool authenticated, const Hooks& hooks) {
  if (strncmp(command, "UI_DOC_", 7) == 0 || strncmp(command, "UI_CAPTURE ", 11) == 0) {
    if (!authenticated) {
      hooks.write_line("UI_DOC_ERROR auth_required");
      return true;
    }
    hooks.ping();
    char line[128];   // longest: UI_DOC_SHOW_DONE with a 39-character id, or a firmware name of up to 31
    if (strcmp(command, "UI_DOC_LIST") == 0) {
      snprintf(line, sizeof(line), "UI_DOC_LIST_BEGIN count=%u firmware=\"%s\"\n",
               static_cast<unsigned>(hooks.screen_count()), hooks.firmware_version());
      hooks.write(line);
      for (size_t i = 0; i < hooks.screen_count(); ++i) {
        const char* id = "";
        const char* modes = "";
        hooks.screen_at(i, &id, &modes);
        snprintf(line, sizeof(line), "UI_DOC_SCREEN id=%s modes=%s\n", id, modes);
        hooks.write(line);
      }
      hooks.write_line("UI_DOC_LIST_DONE");
      return true;
    }
    if (strncmp(command, "UI_DOC_SHOW ", 12) == 0) {
      char screen[40]{};
      char mode[8]{};
      char trailing = 0;
      if (sscanf(command + 12, "%39s %7s %c", screen, mode, &trailing) != 2 ||
          (strcmp(mode, "live") != 0 && strcmp(mode, "demo") != 0)) {
        hooks.write_line("UI_DOC_ERROR usage: UI_DOC_SHOW <screen-id> <live|demo>");
        return true;
      }
      if (!hooks.screen_exists(screen, mode)) {
        hooks.write_line("UI_DOC_ERROR unknown_screen_or_mode");
        return true;
      }
      hooks.enter();
      const bool demo = strcmp(mode, "demo") == 0;
      if (demo) {
        if (!hooks.pause_reception()) {
          hooks.write_line("UI_DOC_ERROR reception_stop_timeout");
          return true;
        }
      } else {
        hooks.tune_live(screen);
      }
      const bool settings_screen = strncmp(screen, "settings.", 9) == 0 ||
          strncmp(screen, "overlay.wifi-", 13) == 0 ||
          strcmp(screen, "overlay.masked-keyboard") == 0;
      const bool rendered = settings_screen ? hooks.render_settings(screen, demo) : hooks.render(screen, demo);
      if (!rendered) {
        hooks.write_line("UI_DOC_ERROR unknown_screen_or_mode");
        return true;
      }
      /* Live captures keep the scope painting so a 10 s settle fills waterfall. */
      if (!demo) hooks.keep_graphics_on();
      snprintf(line, sizeof(line), "UI_DOC_SHOW_DONE id=%s mode=%s\n", screen, mode);
      hooks.write(line);
      return true;
    }
    if (strncmp(command, "UI_CAPTURE ", 11) == 0) {
      const char* slug = command + 11;
      // The live Visualizer and Dead Air screens capture as they are, without pausing reception.
      const bool visualizer_capture = hooks.capture_without_doc_mode();
      const bool lab_capture = hooks.lab_active();
      if (!hooks.doc_screen_shown() && !visualizer_capture && !lab_capture) {
        hooks.write_line("UI_CAPTURE_ERROR documentation_mode_inactive");
        return true;
      }
      if (!hooks.valid_slug(slug)) {
        hooks.write_line("UI_CAPTURE_ERROR invalid_slug");
        return true;
      }
      if (!visualizer_capture && !hooks.pause_reception()) {
        hooks.write_line("UI_CAPTURE_ERROR reception_stop_timeout");
        return true;
      }
      if (!hooks.sd_ready()) {
        hooks.write_line("UI_CAPTURE_ERROR sd_unavailable");
        return true;
      }
      CaptureResult result;
      hooks.save_capture(slug, &result);
      if (!result.ok) {
        snprintf(line, sizeof(line), "UI_CAPTURE_ERROR %s\n", result.error ? result.error : "capture_failed");
        hooks.write(line);
        return true;
      }
      write_capture_done(hooks, slug, result);
      return true;
    }
    if (strcmp(command, "UI_DOC_EXIT") == 0) {
      hooks.exit();
      hooks.write_line("UI_DOC_EXIT_DONE restored=true");
      return true;
    }
  }
  return false;
}

}  // namespace orcsdr::serial_ui_doc
