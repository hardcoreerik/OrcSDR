// Host tests for the recording and tool serial commands (apps/orcsdr-tab5/ui/serial_rec.cpp). Expected replies are
// typed from the code this replaced in main.cpp.
// Build with g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -Iapps/orcsdr-tab5/ui, from the
// repository root: tests/serial_rec_tests.cpp apps/orcsdr-tab5/ui/serial_rec.cpp
#include "serial_rec.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace orcsdr::serial_rec;

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

std::vector<std::string> g_writes;
std::vector<std::string> g_calls;
const char* g_tool = "RADIO";

const Hooks kHooks = [] {
  Hooks h;
  h.write = [](const char* t) { g_writes.emplace_back(t); };
  h.write_line = [](const char* t) { g_writes.emplace_back(t); g_writes.emplace_back("\n"); };
  h.rec_start = [] { g_calls.emplace_back("start"); };
  h.rec_stop = [] { g_calls.emplace_back("stop"); };
  h.rec_status = [] { g_calls.emplace_back("status"); };
  h.rec_save = [] { g_calls.emplace_back("save"); };
  h.set_tool = [](Tool t) { g_calls.emplace_back("tool" + std::to_string(static_cast<int>(t))); };
  h.tool_name = [] { return g_tool; };
  return h;
}();

void reset() {
  g_writes.clear();
  g_calls.clear();
}

std::string joined() {
  std::string s;
  for (const auto& w : g_writes) s += w;
  return s;
}

void expect_call(const char* command, const char* call, bool authenticated) {
  reset();
  CHECK(handle_command(command, authenticated, kHooks));
  CHECK(g_calls.size() == 1 && g_calls[0] == call && g_writes.empty());
}

}  // namespace

int main() {
  for (bool auth : {false, true}) {   // none of these is signed-in only
    expect_call("RTL_REC_START", "start", auth);
    expect_call("RTL_REC_STOP", "stop", auth);
    expect_call("RTL_REC_STATUS", "status", auth);
    expect_call("RTL_REC_SAVE", "save", auth);
    expect_call("RTL_TOOL RADIO", "tool0", auth);
    expect_call("RTL_TOOL radio", "tool0", auth);
    expect_call("RTL_TOOL SCOPE", "tool1", auth);
    expect_call("RTL_TOOL scope", "tool1", auth);
    expect_call("RTL_TOOL CAPTURE", "tool2", auth);
    expect_call("RTL_TOOL capture", "tool2", auth);
  }
  for (const char* bad : {"RTL_TOOL Radio", "RTL_TOOL ", "RTL_TOOL X", "RTL_TOOL RADIO "}) {
    reset();
    CHECK(handle_command(bad, false, kHooks));
    CHECK(g_calls.empty() && joined() == "RTL_TOOL_INVALID use RADIO|SCOPE|CAPTURE\n" && g_writes.size() == 2);
  }
  reset();
  g_tool = "SCOPE";
  CHECK(handle_command("RTL_TOOL", false, kHooks));
  CHECK(g_calls.empty() && g_writes.size() == 1 && g_writes[0] == "RTL_TOOL_STATUS tool=SCOPE\n");
  for (const char* other : {"RTL_REC_STARTX", "RTL_REC", "RTL_TOOLS", "rtl_rec_start", "RTL_REC_STATUS "}) {
    reset();
    CHECK(!handle_command(other, true, kHooks));
    CHECK(g_calls.empty() && g_writes.empty());
  }
  if (failures) {
    std::printf("serial_rec_tests: %d FAILED\n", failures);
    return 1;
  }
  std::printf("serial_rec_tests: ok\n");
  return 0;
}
