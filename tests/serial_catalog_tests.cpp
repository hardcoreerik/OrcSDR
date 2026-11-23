// Host test for the RTL_CATALOG command family (serial_catalog.cpp). The expected replies are the format strings the
// dispatcher in main.cpp used before the family moved, filled in with the scripted catalog state below.
// Build from the repository root with g++ -std=c++17 -Wall -Wextra -Werror -Iapps/orcsdr-tab5/ui:
// tests/serial_catalog_tests.cpp and apps/orcsdr-tab5/ui/serial_catalog.cpp
#include "serial_catalog.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace orcsdr::serial_catalog;

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

// "W:" is one verbatim write (it carries its own '\n'), "L:" is a line written through println.
std::vector<std::string> g_out;
std::vector<std::string> g_calls;
bool g_busy_after_action = true;   // what the state reports once an action has been queued

orcsdr::catalog::State scripted_state(bool busy) {
  orcsdr::catalog::State state;
  state.ready = true;
  state.busy = busy;
  state.operation = orcsdr::catalog::Operation::install;
  state.progress_percent = 42;
  std::strcpy(state.message, "Checked 3 packs");
  std::strcpy(state.catalog_date, "2031-02-03");
  std::strcpy(state.packs[0].id, "alpha-pack");
  std::strcpy(state.packs[0].version, "1.2");
  std::strcpy(state.packs[0].source_date, "2031-01-01");
  std::strcpy(state.packs[0].status, "installed");
  state.packs[0].runtime_bytes = 123456;
  state.packs[0].archive_bytes = 4000000000u;
  state.packs[0].installed = true;
  // Slot 1 is empty and must be skipped; slot 2 has no version and no source date.
  std::strcpy(state.packs[2].id, "beta");
  std::strcpy(state.packs[2].status, "new");
  state.packs[2].update_available = true;
  return state;
}

bool g_action_queued = false;

Hooks make_hooks() {
  Hooks hooks;
  hooks.write = [](const char* text) { g_out.push_back(std::string("W:") + text); };
  hooks.write_line = [](const char* text) { g_out.push_back(std::string("L:") + text); };
  hooks.state = []() -> orcsdr::catalog::State {
    g_calls.push_back("state");
    return scripted_state(g_action_queued && g_busy_after_action);
  };
  hooks.check = [] { g_calls.push_back("check"); g_action_queued = true; };
  hooks.install = [](int index) { g_calls.push_back("install " + std::to_string(index)); g_action_queued = true; };
  hooks.remove = [](int index) { g_calls.push_back("remove " + std::to_string(index)); g_action_queued = true; };
  return hooks;
}

void expect(const std::vector<std::string>& expected) {
  if (g_out != expected) {
    ++failures;
    std::printf("FAIL output:\n");
    for (size_t i = 0; i < g_out.size() || i < expected.size(); ++i) {
      std::printf("  got:  %s  want: %s", i < g_out.size() ? g_out[i].c_str() : "<nothing>\n",
                  i < expected.size() ? expected[i].c_str() : "<nothing>\n");
      if ((i < g_out.size() ? g_out[i] : "x").back() != '\n') std::printf("\n");
    }
  }
  g_out.clear();
}

void reset() {
  g_out.clear();
  g_calls.clear();
  g_action_queued = false;
  g_busy_after_action = true;
}

const std::vector<std::string> kStatusReply = {
    "W:RTL_CATALOG_STATUS ready=1 busy=0 operation=2 progress=42 date=2031-02-03 message=\"Checked 3 packs\"\n",
    "W:RTL_CATALOG_PACK id=alpha-pack version=1.2 source_date=2031-01-01 runtime_bytes=123456 archive_bytes=4000000000 "
    "installed=1 update=0 status=\"installed\"\n",
    "W:RTL_CATALOG_PACK id=beta version=-- source_date=-- runtime_bytes=0 archive_bytes=0 installed=0 update=1 "
    "status=\"new\"\n"};

}  // namespace

int main() {
  const Hooks hooks = make_hooks();

  // STATUS and LIST need no authentication and are the same reply.
  for (const char* command : {"RTL_CATALOG_STATUS", "RTL_CATALOG_LIST"}) {
    reset();
    CHECK(handle_command(command, false, hooks));
    expect(kStatusReply);
    CHECK(g_calls.size() == 1 && g_calls[0] == "state");
  }
  // An empty catalog date prints "--".
  {
    reset();
    Hooks empty = hooks;
    empty.state = []() -> orcsdr::catalog::State { return orcsdr::catalog::State{}; };
    CHECK(handle_command("RTL_CATALOG_STATUS", true, empty));
    expect({"W:RTL_CATALOG_STATUS ready=0 busy=0 operation=0 progress=0 date=-- message=\"\"\n"});
  }

  // CHECK and FETCH refuse an unauthenticated caller before doing anything.
  for (const char* command : {"RTL_CATALOG_CHECK", "RTL_CATALOG_FETCH"}) {
    reset();
    CHECK(handle_command(command, false, hooks));
    CHECK(g_calls.empty());
    expect({"L:RTL_CATALOG_ERROR auth_required"});
  }
  // Both queue the same check; the reply names the command and whether the worker went busy.
  reset();
  CHECK(handle_command("RTL_CATALOG_CHECK", true, hooks));
  CHECK(g_calls.size() == 2 && g_calls[0] == "check" && g_calls[1] == "state");   // state is read after the action
  expect({"W:RTL_CATALOG_CHECK_QUEUED\n"});
  reset();
  CHECK(handle_command("RTL_CATALOG_FETCH", true, hooks));
  CHECK(g_calls.size() == 2 && g_calls[0] == "check");
  expect({"W:RTL_CATALOG_FETCH_QUEUED\n"});
  reset();
  g_busy_after_action = false;
  CHECK(handle_command("RTL_CATALOG_CHECK", true, hooks));
  expect({"W:RTL_CATALOG_CHECK_REJECTED\n"});

  // INSTALL and REMOVE refuse an unauthenticated caller before doing anything.
  for (const char* command : {"RTL_CATALOG_INSTALL alpha-pack", "RTL_CATALOG_REMOVE alpha-pack CONFIRM",
                              "RTL_CATALOG_INSTALL ", "RTL_CATALOG_REMOVE "}) {
    reset();
    CHECK(handle_command(command, false, hooks));
    CHECK(g_calls.empty());
    expect({"L:RTL_CATALOG_ERROR auth_required"});
  }

  // INSTALL takes exactly one word.
  for (const char* bad : {"RTL_CATALOG_INSTALL ", "RTL_CATALOG_INSTALL   ", "RTL_CATALOG_INSTALL alpha-pack extra"}) {
    reset();
    CHECK(handle_command(bad, true, hooks));
    CHECK(g_calls.empty());
    expect({"L:RTL_CATALOG_INSTALL_ERROR usage: RTL_CATALOG_INSTALL <id>"});
  }
  // REMOVE takes the id and the word CONFIRM, exactly.
  for (const char* bad : {"RTL_CATALOG_REMOVE ", "RTL_CATALOG_REMOVE alpha-pack", "RTL_CATALOG_REMOVE alpha-pack confirm",
                          "RTL_CATALOG_REMOVE alpha-pack CONFIRM now", "RTL_CATALOG_REMOVE alpha-pack YES"}) {
    reset();
    CHECK(handle_command(bad, true, hooks));
    CHECK(g_calls.empty());
    expect({"L:RTL_CATALOG_REMOVE_ERROR usage: RTL_CATALOG_REMOVE <id> CONFIRM"});
  }
  // An id that is not in the catalog state is refused; the state was read, nothing was queued.
  reset();
  CHECK(handle_command("RTL_CATALOG_INSTALL gamma", true, hooks));
  CHECK(g_calls.size() == 1 && g_calls[0] == "state");
  expect({"L:RTL_CATALOG_ERROR unknown_or_unchecked_pack"});
  reset();
  CHECK(handle_command("RTL_CATALOG_REMOVE gamma CONFIRM", true, hooks));
  expect({"L:RTL_CATALOG_ERROR unknown_or_unchecked_pack"});
  // An id longer than 19 characters is cut by the scan; the rest reads as a second word, so INSTALL is a usage error.
  reset();
  CHECK(handle_command("RTL_CATALOG_INSTALL abcdefghijklmnopqrstuvwxyz", true, hooks));
  CHECK(g_calls.empty());
  expect({"L:RTL_CATALOG_INSTALL_ERROR usage: RTL_CATALOG_INSTALL <id>"});

  // A pack found in slot 0 and slot 2.
  reset();
  CHECK(handle_command("RTL_CATALOG_INSTALL alpha-pack", true, hooks));
  CHECK(g_calls.size() == 3 && g_calls[0] == "state" && g_calls[1] == "install 0" && g_calls[2] == "state");
  expect({"W:RTL_CATALOG_INSTALL_QUEUED\n"});
  reset();
  CHECK(handle_command("RTL_CATALOG_INSTALL beta", true, hooks));
  CHECK(g_calls[1] == "install 2");
  expect({"W:RTL_CATALOG_INSTALL_QUEUED\n"});
  reset();
  CHECK(handle_command("RTL_CATALOG_REMOVE beta CONFIRM", true, hooks));
  CHECK(g_calls.size() == 3 && g_calls[1] == "remove 2");
  expect({"W:RTL_CATALOG_REMOVE_QUEUED\n"});
  reset();
  g_busy_after_action = false;
  CHECK(handle_command("RTL_CATALOG_REMOVE alpha-pack CONFIRM", true, hooks));
  expect({"W:RTL_CATALOG_REMOVE_REJECTED\n"});
  reset();
  g_busy_after_action = false;
  CHECK(handle_command("RTL_CATALOG_INSTALL alpha-pack", true, hooks));
  expect({"W:RTL_CATALOG_INSTALL_REJECTED\n"});

  // Lines that are not this family's: exact match, and the prefixes need their trailing space.
  reset();
  for (const char* other : {"RTL_CATALOG", "RTL_CATALOG_STATUS ", "RTL_CATALOG_STATU", "rtl_catalog_status", "RTL_CATALOG_CHECK ",
                            "RTL_CATALOG_FETCH x", "RTL_CATALOG_INSTALL", "RTL_CATALOG_REMOVE", "RTL_CATALOG_INSTALLALPHA",
                            "RTL_CATALOGS", "RTL_LOCATION STATUS", ""}) {
    CHECK(!handle_command(other, true, hooks));
  }
  CHECK(g_out.empty() && g_calls.empty());

  if (failures == 0) std::printf("serial_catalog_tests: all passed\n");
  return failures == 0 ? 0 : 1;
}
