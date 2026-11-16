// Host tests for UI documentation mode's serial commands (serial_ui_doc.cpp): UI_DOC_LIST, UI_DOC_SHOW, UI_CAPTURE and
// UI_DOC_EXIT. Expected behaviour is typed from the code this replaced; the screens, rendering and capture are fakes.
// Build with g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -Iapps/orcsdr-tab5/ui, from the
// repository root: tests/serial_ui_doc_tests.cpp apps/orcsdr-tab5/ui/serial_ui_doc.cpp
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "serial_ui_doc.hpp"

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

struct Screen { const char* id; const char* modes; };
const Screen kScreens[] = {{"home", "live,demo"}, {"fm.listen", "live,demo"}, {"settings.system", "demo"}};

std::string g_out;
std::vector<std::string> g_calls;
int g_empty_line_writes = 0;
bool g_pause_ok = true, g_render_ok = true, g_doc_shown = false, g_visualizer = false, g_lab = false;
bool g_sd = true, g_capture_ok = true;

bool exists(const char* id, const char* mode) {
  for (const auto& s : kScreens)
    if (strcmp(s.id, id) == 0) return strstr(s.modes, mode) != nullptr;
  return false;
}

const orcsdr::serial_ui_doc::Hooks kHooks = [] {
  orcsdr::serial_ui_doc::Hooks h;
  h.write = [](const char* t) { g_out += t; };
  h.write_line = [](const char* t) {
    if (!*t) ++g_empty_line_writes;
    g_out += std::string(t) + "\n";
  };
  h.write_hex = [](const uint8_t* v, size_t n) {
    char two[3];
    for (size_t i = 0; i < n; ++i) { std::snprintf(two, sizeof(two), "%02x", v[i]); g_out += two; }
  };
  h.ping = [] { g_calls.push_back("ping"); };
  h.firmware_version = []() -> const char* { return "v9.9.9"; };
  h.screen_count = []() -> size_t { return sizeof(kScreens) / sizeof(kScreens[0]); };
  h.screen_at = [](size_t i, const char** id, const char** modes) { *id = kScreens[i].id; *modes = kScreens[i].modes; };
  h.screen_exists = exists;
  h.enter = [] { g_calls.push_back("enter"); };
  h.pause_reception = [] { g_calls.push_back("pause"); return g_pause_ok; };
  h.tune_live = [](const char* s) { g_calls.push_back(std::string("tune ") + s); };
  h.render = [](const char* s, bool demo) { g_calls.push_back(std::string("render ") + s + (demo ? " demo" : " live")); return g_render_ok; };
  h.render_settings = [](const char* s, bool demo) { g_calls.push_back(std::string("settings ") + s + (demo ? " demo" : " live")); return g_render_ok; };
  h.keep_graphics_on = [] { g_calls.push_back("graphics"); };
  h.capture_without_doc_mode = [] { return g_visualizer; };
  h.lab_active = [] { return g_lab; };
  h.doc_screen_shown = [] { return g_doc_shown; };
  h.valid_slug = [](const char* s) { return *s && strlen(s) <= 63 && strspn(s, "abcdefghijklmnopqrstuvwxyz0123456789-_") == strlen(s); };
  h.sd_ready = [] { return g_sd; };
  h.save_capture = [](const char* slug, orcsdr::serial_ui_doc::CaptureResult* r) {
    g_calls.push_back(std::string("save ") + slug);
    r->ok = g_capture_ok;
    r->error = g_capture_ok ? nullptr : "write_failed";
    r->bytes = 2764854;
    r->width = 1280;
    r->height = 720;
    for (int i = 0; i < 32; ++i) r->sha256[i] = static_cast<uint8_t>(i);
  };
  h.exit = [] { g_calls.push_back("exit"); };
  return h;
}();

std::string run(const char* line, bool authenticated = true, bool* handled = nullptr) {
  g_out.clear();
  g_calls.clear();
  const bool h = orcsdr::serial_ui_doc::handle_command(line, authenticated, kHooks);
  if (handled) *handled = h;
  return g_out;
}

std::string calls() {
  std::string out;
  for (const auto& c : g_calls) out += c + ";";
  return out;
}

}  // namespace

int main() {
  bool handled = false;
  // Signed out: one refusal for every form, nothing else runs.
  for (const char* line : {"UI_DOC_LIST", "UI_DOC_SHOW home live", "UI_CAPTURE x", "UI_DOC_EXIT", "UI_DOC_WHATEVER"}) {
    CHECK(run(line, false, &handled) == "UI_DOC_ERROR auth_required\n" && handled && calls().empty());
  }

  CHECK(run("UI_DOC_LIST") ==
        "UI_DOC_LIST_BEGIN count=3 firmware=\"v9.9.9\"\n"
        "UI_DOC_SCREEN id=home modes=live,demo\n"
        "UI_DOC_SCREEN id=fm.listen modes=live,demo\n"
        "UI_DOC_SCREEN id=settings.system modes=demo\n"
        "UI_DOC_LIST_DONE\n");
  CHECK(calls() == "ping;");

  // UI_DOC_SHOW.
  const std::string usage = "UI_DOC_ERROR usage: UI_DOC_SHOW <screen-id> <live|demo>\n";
  CHECK(run("UI_DOC_SHOW home") == usage);
  CHECK(run("UI_DOC_SHOW home live extra") == usage);
  CHECK(run("UI_DOC_SHOW home other") == usage);
  CHECK(run("UI_DOC_SHOW nope live") == "UI_DOC_ERROR unknown_screen_or_mode\n" && calls() == "ping;");
  CHECK(run("UI_DOC_SHOW settings.system live") == "UI_DOC_ERROR unknown_screen_or_mode\n");
  CHECK(run("UI_DOC_SHOW fm.listen live") == "UI_DOC_SHOW_DONE id=fm.listen mode=live\n");
  CHECK(calls() == "ping;enter;tune fm.listen;render fm.listen live;graphics;");
  CHECK(run("UI_DOC_SHOW home demo") == "UI_DOC_SHOW_DONE id=home mode=demo\n");
  CHECK(calls() == "ping;enter;pause;render home demo;");
  CHECK(run("UI_DOC_SHOW settings.system demo") == "UI_DOC_SHOW_DONE id=settings.system mode=demo\n");
  CHECK(calls() == "ping;enter;pause;settings settings.system demo;");
  g_pause_ok = false;
  CHECK(run("UI_DOC_SHOW home demo") == "UI_DOC_ERROR reception_stop_timeout\n" && calls() == "ping;enter;pause;");
  g_pause_ok = true;
  g_render_ok = false;
  CHECK(run("UI_DOC_SHOW home live") == "UI_DOC_ERROR unknown_screen_or_mode\n");
  g_render_ok = true;

  // UI_CAPTURE.
  g_doc_shown = false;
  CHECK(run("UI_CAPTURE shot") == "UI_CAPTURE_ERROR documentation_mode_inactive\n");
  g_doc_shown = true;
  CHECK(run("UI_CAPTURE Bad!") == "UI_CAPTURE_ERROR invalid_slug\n");
  g_pause_ok = false;
  CHECK(run("UI_CAPTURE shot") == "UI_CAPTURE_ERROR reception_stop_timeout\n");
  g_pause_ok = true;
  g_sd = false;
  CHECK(run("UI_CAPTURE shot") == "UI_CAPTURE_ERROR sd_unavailable\n");
  g_sd = true;
  g_capture_ok = false;
  CHECK(run("UI_CAPTURE shot") == "UI_CAPTURE_ERROR write_failed\n");
  g_capture_ok = true;
  const std::string sha = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
  const std::string slug63(63, 'a');
  CHECK(run(("UI_CAPTURE " + slug63).c_str()) ==
        "UI_CAPTURE_DONE slug=" + slug63 + " path=\"/orcsdr/screenshots/" + slug63 +
        ".bmp\" bytes=2764854 width=1280 height=720 firmware=\"v9.9.9\" sha256=" + sha + "\n");
  CHECK(calls() == "ping;pause;save " + slug63 + ";");
  // Visualizer / Dead Air: allowed outside documentation mode and reception keeps running.
  g_doc_shown = false;
  g_visualizer = true;
  CHECK(run("UI_CAPTURE viz").rfind("UI_CAPTURE_DONE slug=viz ", 0) == 0 && calls() == "ping;save viz;");
  g_visualizer = false;
  g_lab = true;
  CHECK(run("UI_CAPTURE lab").rfind("UI_CAPTURE_DONE slug=lab ", 0) == 0 && calls() == "ping;pause;save lab;");
  g_lab = false;

  CHECK(run("UI_DOC_EXIT") == "UI_DOC_EXIT_DONE restored=true\n" && calls() == "ping;exit;");

  // An unknown UI_DOC_ line from a signed-in host restarts the ping clock and is not answered (falls through).
  CHECK(run("UI_DOC_WHATEVER", true, &handled).empty() && !handled && calls() == "ping;");
  CHECK(run("UI_DOC_LIST ", true, &handled).empty() && !handled);
  // Not these routes at all.
  CHECK(run("UI_CAPTURE", true, &handled).empty() && !handled && calls().empty());
  CHECK(run("UI_DOC", true, &handled).empty() && !handled && calls().empty());
  CHECK(g_empty_line_writes == 0);

  if (failures == 0) std::printf("serial_ui_doc: all checks passed\n");
  return failures == 0 ? 0 : 1;
}
