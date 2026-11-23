// Host test for the RTL_HELP family. The golden files were captured from process_command() in main.cpp before the
// listing moved to serial_help.cpp, so a pass means the moved text is the same, line for line.
// Build from the repository root with g++ -std=c++17 -Wall -Wextra -Werror -Iapps/orcsdr-tab5/ui:
// tests/serial_help_tests.cpp apps/orcsdr-tab5/ui/serial_help.cpp. Run from the repository root.
#include "serial_help.hpp"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

std::vector<std::string> g_lines;
void capture(const char* text) { g_lines.emplace_back(text); }

std::vector<std::string> read_lines(const char* path) {
  std::vector<std::string> lines;
  std::ifstream file(path, std::ios::binary);
  std::string line;
  while (std::getline(file, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();   // a CRLF checkout of the golden file
    lines.push_back(line);
  }
  return lines;
}

void expect_listing(bool orcdial, const char* golden_path) {
  const std::vector<std::string> golden = read_lines(golden_path);
  CHECK(golden.size() > 80);
  g_lines.clear();
  const orcsdr::serial_help::Hooks hooks{capture, orcdial};
  CHECK(orcsdr::serial_help::handle_command("RTL_HELP", false, hooks));
  CHECK(g_lines.size() == golden.size());
  for (size_t i = 0; i < g_lines.size() && i < golden.size(); ++i) {
    if (g_lines[i] != golden[i]) {
      std::printf("FAIL %s line %zu\n  expected: %s\n  printed:  %s\n", golden_path, i + 1, golden[i].c_str(),
                  g_lines[i].c_str());
      ++failures;
      break;
    }
  }
  CHECK(!g_lines.empty() && g_lines.front() == "RTL_HELP_BEGIN" && g_lines.back() == "RTL_HELP_END");
}

}  // namespace

int main() {
  expect_listing(false, "tests/data/serial_help_golden.txt");
  expect_listing(true, "tests/data/serial_help_golden_orcdial.txt");

  // Authentication makes no difference: the listing was public in the dispatcher.
  const orcsdr::serial_help::Hooks hooks{capture, false};
  g_lines.clear();
  CHECK(orcsdr::serial_help::handle_command("RTL_HELP", true, hooks));
  CHECK(g_lines.size() == read_lines("tests/data/serial_help_golden.txt").size());

  // The match is exact, as it was: anything else is left to the dispatcher and prints nothing.
  g_lines.clear();
  for (const char* other : {"RTL_HELP ", "RTL_HELP X", "RTL_HEL", "rtl_help", "", "RTL_HELP_BEGIN", "RTL_HEALTH"}) {
    CHECK(!orcsdr::serial_help::handle_command(other, true, hooks));
  }
  CHECK(g_lines.empty());

  // No output hook: the command is still recognised (the dispatcher must not fall through) and nothing is called.
  CHECK(orcsdr::serial_help::handle_command("RTL_HELP", false, orcsdr::serial_help::Hooks{}));

  if (failures == 0) std::printf("PASS: RTL_HELP listing matches the text captured from main.cpp, with and without OrcDial\n");
  return failures == 0 ? 0 : 1;
}
