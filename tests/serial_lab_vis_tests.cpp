// Host tests for the RF Lab and Visualizer serial gate (apps/orcsdr-tab5/ui/serial_lab_vis.cpp). Expected replies are
// typed from the code this replaced in main.cpp.
// Build with g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -Iapps/orcsdr-tab5/ui, from the
// repository root: tests/serial_lab_vis_tests.cpp apps/orcsdr-tab5/ui/serial_lab_vis.cpp
#include "serial_lab_vis.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace orcsdr::serial_lab_vis;

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

std::vector<std::string> g_lines;
std::vector<std::string> g_calls;
const char* g_reply = "";      // what the module's own parser answers
bool g_known = true;           // whether it knows the subcommand
bool g_active = false;
size_t g_size_seen = 0;

bool fake_module(const char* command, char* response, size_t size, const char* tag) {
  g_calls.push_back(std::string(tag) + " " + command);
  g_size_seen = size;
  if (!g_known) return false;
  std::snprintf(response, size, "%s", g_reply);
  return true;
}

const Hooks kHooks = [] {
  Hooks h;
  h.write_line = [](const char* t) { g_lines.emplace_back(t); };
  h.lab_command = [](const char* c, char* r, size_t s) { return fake_module(c, r, s, "lab"); };
  h.open_lab = [] { g_calls.emplace_back("open lab"); };
  h.lab_active = [] { return g_active; };
  h.vis_command = [](const char* c, char* r, size_t s) { return fake_module(c, r, s, "vis"); };
  h.open_visualizer = [] { g_calls.emplace_back("open vis"); };
  h.vis_active = [] { return g_active; };
  return h;
}();

void reset(const char* reply = "", bool known = true, bool active = false) {
  g_lines.clear();
  g_calls.clear();
  g_reply = reply;
  g_known = known;
  g_active = active;
  g_size_seen = 0;
}

}  // namespace

int main() {
  // Read-only subcommands need no sign-in.
  for (const char* c : {"RTL_LAB STATUS", "RTL_LAB SELF_CHECK", "RTL_LAB GET gain", "RTL_LAB RECIPE LIST",
                        "RTL_LAB RECORDS LIST"}) {
    reset("RTL_LAB_STATUS ok");
    CHECK(handle_command(c, false, kHooks));
    CHECK(g_calls.size() == 1 && g_lines.size() == 1 && g_lines[0] == "RTL_LAB_STATUS ok" && g_size_seen == 512);
  }
  for (const char* c : {"RTL_VIS STATUS", "RTL_VIS SELF_CHECK", "RTL_VIS GET view", "RTL_VIS PRESET LIST"}) {
    reset("RTL_VIS_STATUS ok");
    CHECK(handle_command(c, false, kHooks));
    CHECK(g_calls.size() == 1 && g_lines.size() == 1 && g_lines[0] == "RTL_VIS_STATUS ok" && g_size_seen == 320);
  }
  // Anything else is refused without sign-in, before the module is asked.
  for (const char* c : {"RTL_LAB OPEN", "RTL_LAB SET gain 10", "RTL_LAB", "RTL_LAB GET", "RTL_LABX"}) {
    reset();
    CHECK(handle_command(c, false, kHooks));
    CHECK(g_calls.empty() && g_lines.size() == 1 && g_lines[0] == "RTL_LAB_ERROR auth_required");
  }
  for (const char* c : {"RTL_VIS OPEN", "RTL_VIS SET x 1", "RTL_VIS", "RTL_VIS PRESET LOAD a"}) {
    reset();
    CHECK(handle_command(c, false, kHooks));
    CHECK(g_calls.empty() && g_lines.size() == 1 && g_lines[0] == "RTL_VIS_ERROR auth_required");
  }
  // Signed in: unknown, open (ok and failed), and a plain reply.
  reset("", false);
  CHECK(handle_command("RTL_LAB BOGUS", true, kHooks));
  CHECK(g_lines.size() == 1 && g_lines[0] == "RTL_LAB_ERROR unknown_command");
  reset("RTL_LAB_OPEN_REQUEST", true, true);
  CHECK(handle_command("RTL_LAB OPEN", true, kHooks));
  CHECK(g_calls.size() == 2 && g_calls[1] == "open lab" && g_lines.size() == 1 && g_lines[0] == "RTL_LAB_OK");
  reset("RTL_LAB_OPEN_REQUEST", true, false);
  CHECK(handle_command("RTL_LAB OPEN", true, kHooks));
  CHECK(g_lines.size() == 1 && g_lines[0] == "RTL_LAB_ERROR init_failed");
  reset("", false);
  CHECK(handle_command("RTL_VIS BOGUS", true, kHooks));
  CHECK(g_lines.size() == 1 && g_lines[0] == "RTL_VIS_ERROR unknown_command");
  reset("RTL_VIS_OPEN_REQUEST", true, true);
  CHECK(handle_command("RTL_VIS OPEN", true, kHooks));
  CHECK(g_calls.size() == 2 && g_calls[1] == "open vis" && g_lines.size() == 1 && g_lines[0] == "RTL_VIS_OK");
  reset("RTL_VIS_OPEN_REQUEST", true, false);
  CHECK(handle_command("RTL_VIS OPEN", true, kHooks));
  CHECK(g_lines.size() == 1 && g_lines[0] == "RTL_VIS_ERROR source_unavailable");
  reset("RTL_VIS_OK preset=2");
  CHECK(handle_command("RTL_VIS PRESET LOAD 2", true, kHooks));
  CHECK(g_lines.size() == 1 && g_lines[0] == "RTL_VIS_OK preset=2");
  // Other commands fall through.
  for (const char* c : {"RTL_LA", "RTL_VI", "rtl_lab status", "RTL_UI STATUS"}) {
    reset();
    CHECK(!handle_command(c, true, kHooks));
    CHECK(g_calls.empty() && g_lines.empty());
  }
  if (failures) {
    std::printf("serial_lab_vis_tests: %d FAILED\n", failures);
    return 1;
  }
  std::printf("serial_lab_vis_tests: ok\n");
  return 0;
}
