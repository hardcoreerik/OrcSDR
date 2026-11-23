// Host test for the RTL_LORA_NATIVE_STATUS / RTL_LORA_PLAN_STATUS commands (serial_lora_status.cpp). The expected
// replies are the format strings the dispatcher in main.cpp used before the family moved, filled in with the scripted
// decoder state below (made-up numbers).
// Build from the repository root with g++ -std=c++17 -Wall -Wextra -Werror -Iapps/orcsdr-tab5/ui:
// tests/serial_lora_status_tests.cpp and apps/orcsdr-tab5/ui/serial_lora_status.cpp
#include "serial_lora_status.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace orcsdr::serial_lora_status;

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

std::vector<std::string> g_out;
int g_native_calls = 0;
int g_plan_calls = 0;
bool g_plan_persisted = true;

Hooks make_hooks() {
  Hooks hooks;
  hooks.write = [](const char* text) { g_out.push_back(std::string("W:") + text); };
  hooks.native_status = []() -> NativeStatus {
    ++g_native_calls;
    NativeStatus s;
    s.ready = true;
    s.busy = false;
    s.key_loaded = true;
    s.crc_ok = 12;
    s.encrypted = 3;
    s.failures = 4;
    s.last_decode_ms = 987654;
    s.preambles = 21;
    s.header_failures = 5;
    s.crc_failures = 6;
    s.raw_cfo_hz = -1234.5;
    s.cfo_hz = 67.8;
    return s;
  };
  hooks.plan_status = []() -> PlanStatus {
    ++g_plan_calls;
    PlanStatus p;
    p.region_index = 2;
    p.region_name = "EU_868";
    p.slot = 1;
    p.slots = 3;
    p.default_slot = 1;
    p.frequency_hz = 869525000;
    p.persisted = g_plan_persisted;
    return p;
  };
  return hooks;
}

void reset() {
  g_out.clear();
  g_native_calls = 0;
  g_plan_calls = 0;
  g_plan_persisted = true;
}

}  // namespace

int main() {
  const Hooks hooks = make_hooks();

  reset();
  CHECK(handle_command("RTL_LORA_NATIVE_STATUS", false, hooks));
  CHECK(g_out.size() == 1);
  CHECK(g_out.size() == 1 &&
        g_out[0] == "W:RTL_LORA_NATIVE_STATUS ready=true busy=false key_loaded=true crc_ok=12 encrypted=3 failures=4 "
                    "last_decode_ms=987654 preambles=21 header_failures=5 crc_failures=6 raw_cfo_hz=-1234.5 "
                    "cfo_hz=67.8\n");
  CHECK(g_native_calls == 1 && g_plan_calls == 0);

  // No authentication needed: same reply when authenticated.
  reset();
  CHECK(handle_command("RTL_LORA_NATIVE_STATUS", true, hooks));
  CHECK(g_out.size() == 1 && g_native_calls == 1);

  reset();
  CHECK(handle_command("RTL_LORA_PLAN_STATUS", false, hooks));
  CHECK(g_out.size() == 1 &&
        g_out[0] == "W:RTL_LORA_PLAN_STATUS region_index=2 region=EU_868 slot=1 slots=3 default_slot=1 "
                    "frequency_hz=869525000 profile=LONGFAST persisted=true\n");
  CHECK(g_plan_calls == 1 && g_native_calls == 0);

  reset();
  g_plan_persisted = false;
  CHECK(handle_command("RTL_LORA_PLAN_STATUS", true, hooks));
  CHECK(g_out.size() == 1 && g_out[0].find("persisted=false\n") != std::string::npos);

  // Lines that must not match: exact comparison only.
  const char* const not_ours[] = {"RTL_LORA_NATIVE_STATUS ", "RTL_LORA_NATIVE_STATU", "rtl_lora_native_status",
                                  "RTL_LORA_PLAN_STATUS 1", "RTL_LORA_PLAN_STATU", " RTL_LORA_PLAN_STATUS", "",
                                  "RTL_LORA_STATUS"};
  for (const char* line : not_ours) {
    reset();
    CHECK(!handle_command(line, true, hooks));
    CHECK(g_out.empty() && g_native_calls == 0 && g_plan_calls == 0);
  }

  if (failures == 0) std::printf("serial_lora_status_tests: all passed\n");
  return failures == 0 ? 0 : 1;
}
