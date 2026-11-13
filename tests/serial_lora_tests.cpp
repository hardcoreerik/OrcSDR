// Host test for the LoRa command family (serial_lora.cpp): region list, auto detect, tune, SD log, history,
// host-injected messages and packets, clear and memory. The expected replies are the format strings the dispatcher
// in main.cpp used before the family moved, filled in with the scripted state below (made-up names, ids and
// coordinates only).
// Build from the repository root with g++ -std=c++17 -Wall -Wextra -Werror -Iapps/orcsdr-tab5/ui:
// tests/serial_lora_tests.cpp and apps/orcsdr-tab5/ui/serial_lora.cpp
#include "serial_lora.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace orcsdr::serial_lora;

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

// "W:" is one verbatim write (it carries its own line ending), "L:" is a line written through println.
// Device calls are recorded in the same list, as "C:...", so the order of side effects and replies is pinned.
std::vector<std::string> g_log;
std::string g_fields;          // what the last present_message/present_packet received
bool g_message_ok = true;
bool g_packet_ok = true;
HistoryFiles g_files_result = HistoryFiles::ok;
bool g_node_found = true;
bool g_hex_ok = true;
uint32_t g_hex_value = 0;
std::string g_hex_text;
uint32_t g_node_id = 0;
size_t g_region_total = 3;

Region region_at(size_t index) {
  static const char* const codes[] = {"US", "EU_868", "ANZ"};
  const unsigned slots[] = {104, 3, 20};
  const unsigned defaults[] = {20, 1, 10};
  return {codes[index], slots[index], defaults[index]};
}

HistoryFiles history_files(void* context, HistoryFileEmit emit) {
  g_log.push_back("C:history_files");
  if (g_files_result == HistoryFiles::ok) {
    emit(context, 1234567890123ull, "2026-11-12.log");
    emit(context, 42ull, "2026-11-13.log");
  }
  return g_files_result;
}

Hooks make_hooks() {
  Hooks hooks;
  hooks.write = [](const char* text) { g_log.push_back(std::string("W:") + text); };
  hooks.write_line = [](const char* text) { g_log.push_back(std::string("L:") + text); };
  hooks.region_count = []() -> size_t { return g_region_total; };
  hooks.region_at = region_at;
  hooks.set_detector = [](bool enabled) { g_log.push_back(enabled ? "C:detector_on" : "C:detector_off"); };
  hooks.min_hz = 400000000u;
  hooks.max_hz = 1000000000u;
  hooks.tune = [](uint32_t hz) { g_log.push_back("C:tune " + std::to_string(hz)); };
  hooks.set_sd_logging = [](bool enabled) { g_log.push_back(enabled ? "C:sd_log_on" : "C:sd_log_off"); };
  hooks.history_files = history_files;
  hooks.history_status = []() -> HistoryStatus {
    g_log.push_back("C:history_status");
    return {1, 7, 100, 3, 250, 0};
  };
  hooks.parse_hex_u32 = [](const char* text, uint32_t* value) -> bool {
    g_hex_text = text;
    g_log.push_back(std::string("C:parse_hex ") + text);
    if (g_hex_ok) *value = g_hex_value;
    return g_hex_ok;
  };
  hooks.history_node = [](uint32_t id, HistoryNode* node) -> bool {
    g_node_id = id;
    g_log.push_back("C:history_node");
    if (!g_node_found) return false;
    node->first_utc = 1700000000ul;
    node->packets = 9;
    node->name_utc = 1700000100ul;
    node->position_utc = 1700000200ul;
    std::strcpy(node->data, "1700000300,ab12cd34,Testy,Test Node One,123456789,-987654321\n");
    return true;
  };
  hooks.sd_log_status = []() -> SdLogStatus {
    g_log.push_back("C:sd_log_status");
    return {true, false, true, 5, 2, "/orcsdr/lora/test.log", 1, 3, "open", 5};
  };
  hooks.present_message = [](const char* fields) -> bool {
    g_fields = fields;
    g_log.push_back("C:present_message");
    return g_message_ok;
  };
  hooks.present_packet = [](const char* fields) -> bool {
    g_fields = fields;
    g_log.push_back("C:present_packet");
    return g_packet_ok;
  };
  hooks.clear_messages = []() { g_log.push_back("C:clear_messages"); };
  hooks.log_memory = []() { g_log.push_back("C:log_memory"); };
  return hooks;
}

void reset() {
  g_log.clear();
  g_fields.clear();
  g_message_ok = true;
  g_packet_ok = true;
  g_files_result = HistoryFiles::ok;
  g_node_found = true;
  g_hex_ok = true;
  g_hex_value = 0xab12cd34u;
  g_hex_text.clear();
  g_node_id = 0;
  g_region_total = 3;
}

bool is(const std::vector<std::string>& expected) {
  if (g_log == expected) return true;
  std::printf("FAIL output:\n");
  for (size_t i = 0; i < g_log.size() || i < expected.size(); ++i) {
    std::printf("  got:  %s\n  want: %s\n", i < g_log.size() ? g_log[i].c_str() : "<nothing>",
                i < expected.size() ? expected[i].c_str() : "<nothing>");
  }
  return false;
}

// Runs one line; returns what handle_command returned.
bool run(const char* line, bool authenticated, const Hooks& hooks) {
  reset();
  return handle_command(line, authenticated, hooks);
}

}  // namespace

int main() {
  const Hooks hooks = make_hooks();

  // RTL_LORA_REGION_LIST: one write per region, no authentication needed, indexes are one-based.
  CHECK(run("RTL_LORA_REGION_LIST", false, hooks));
  CHECK(is({"W:RTL_LORA_REGION index=1 code=US slots=104 default_slot=20\n",
            "W:RTL_LORA_REGION index=2 code=EU_868 slots=3 default_slot=1\n",
            "W:RTL_LORA_REGION index=3 code=ANZ slots=20 default_slot=10\n"}));
  reset();
  g_region_total = 0;
  CHECK(handle_command("RTL_LORA_REGION_LIST", true, hooks));
  CHECK(g_log.empty());   // handled, nothing printed

  // RTL_LORA_AUTO: authenticated only; unauthenticated lines are not handled here (no refusal reply).
  CHECK(run("RTL_LORA_AUTO ON", true, hooks));
  CHECK(is({"C:detector_on", "W:RTL_LORA_AUTO ON\n"}));
  CHECK(run("RTL_LORA_AUTO OFF", true, hooks));
  CHECK(is({"C:detector_off", "W:RTL_LORA_AUTO OFF\n"}));
  CHECK(!run("RTL_LORA_AUTO ON", false, hooks));
  CHECK(g_log.empty());
  CHECK(!run("RTL_LORA_AUTO OFF", false, hooks));
  CHECK(g_log.empty());
  CHECK(!run("RTL_LORA_AUTO", true, hooks));
  CHECK(!run("RTL_LORA_AUTO ON ", true, hooks));
  CHECK(!run("RTL_LORA_AUTO on", true, hooks));
  CHECK(!run("RTL_LORA_AUTO MAYBE", true, hooks));
  CHECK(g_log.empty());

  // RTL_LORA_TUNE <hz>: authenticated only, range min_hz..max_hz inclusive, whole line must be a number.
  CHECK(run("RTL_LORA_TUNE 869525000", true, hooks));
  CHECK(is({"C:tune 869525000", "W:RTL_LORA_TUNE_OK frequency_hz=869525000\n"}));
  CHECK(run("RTL_LORA_TUNE 400000000", true, hooks));
  CHECK(is({"C:tune 400000000", "W:RTL_LORA_TUNE_OK frequency_hz=400000000\n"}));
  CHECK(run("RTL_LORA_TUNE 1000000000", true, hooks));
  CHECK(is({"C:tune 1000000000", "W:RTL_LORA_TUNE_OK frequency_hz=1000000000\n"}));
  const char* const bad_tune[] = {"RTL_LORA_TUNE 399999999", "RTL_LORA_TUNE 1000000001", "RTL_LORA_TUNE abc",
                                  "RTL_LORA_TUNE ", "RTL_LORA_TUNE 869525000x", "RTL_LORA_TUNE 869525000 ",
                                  "RTL_LORA_TUNE -5", "RTL_LORA_TUNE 0"};
  for (const char* line : bad_tune) {
    CHECK(run(line, true, hooks));
    CHECK(is({"W:RTL_LORA_TUNE_ERROR range=400000000-1000000000\n"}));
  }
  CHECK(!run("RTL_LORA_TUNE 869525000", false, hooks));
  CHECK(g_log.empty());
  CHECK(!run("RTL_LORA_TUNE abc", false, hooks));   // unauthenticated: not handled, not even the range error
  CHECK(g_log.empty());
  CHECK(!run("RTL_LORA_TUNE", true, hooks));        // the space is part of the prefix
  CHECK(!run("rtl_lora_tune 869525000", true, hooks));
  CHECK(g_log.empty());

  // LORA_SD_LOG ON/OFF: no authentication, no reply of their own.
  CHECK(run("LORA_SD_LOG ON", false, hooks));
  CHECK(is({"C:sd_log_on"}));
  CHECK(run("LORA_SD_LOG OFF", false, hooks));
  CHECK(is({"C:sd_log_off"}));
  CHECK(!run("LORA_SD_LOG", true, hooks));
  CHECK(!run("LORA_SD_LOG ON ", true, hooks));
  CHECK(!run("LORA_SD_LOG on", true, hooks));
  CHECK(g_log.empty());

  // LORA_SD_LOG STATUS.
  CHECK(run("LORA_SD_LOG STATUS", false, hooks));
  CHECK(is({"C:sd_log_status",
            "W:LORA_SD_LOG_STATUS requested=true ready=false error=true queued=5 dropped=2 "
            "path=\"/orcsdr/lora/test.log\" pending=1 retry_attempts=3 last_error_stage=open last_errno=5\n"}));
  CHECK(!run("LORA_SD_LOG STATUS ", true, hooks));
  CHECK(g_log.empty());

  // LORA_HISTORY FILES: each file is one write, then the count; every failure is one println.
  CHECK(run("LORA_HISTORY FILES", false, hooks));
  CHECK(is({"C:history_files", "W:LORA_HISTORY_FILE bytes=1234567890123 path=/orcsdr/lora/2026-11-12.log\n",
            "W:LORA_HISTORY_FILE bytes=42 path=/orcsdr/lora/2026-11-13.log\n",
            "W:LORA_HISTORY_FILES_DONE count=2\n"}));
  g_files_result = HistoryFiles::ok;
  const struct { HistoryFiles result; const char* line; } file_errors[] = {
      {HistoryFiles::radio_busy, "L:LORA_HISTORY_ERROR radio_busy"},
      {HistoryFiles::sd_unavailable, "L:LORA_HISTORY_ERROR sd_unavailable"},
      {HistoryFiles::open_failed, "L:LORA_HISTORY_ERROR open_failed"},
  };
  for (const auto& error : file_errors) {
    reset();
    g_files_result = error.result;
    CHECK(handle_command("LORA_HISTORY FILES", true, hooks));
    CHECK(is({"C:history_files", error.line}));
  }
  CHECK(!run("LORA_HISTORY FILES ", true, hooks));
  CHECK(!run("LORA_HISTORY", true, hooks));
  CHECK(g_log.empty());

  // LORA_HISTORY STATUS.
  CHECK(run("LORA_HISTORY STATUS", false, hooks));
  CHECK(is({"C:history_status",
            "W:LORA_HISTORY_STATUS loaded=1 nodes=7 replayed=100 rejected=3 written=250 error=0 "
            "directory=/orcsdr/lora\n"}));
  CHECK(!run("LORA_HISTORY STATUS ", true, hooks));
  CHECK(g_log.empty());

  // LORA_HISTORY NODE <hex id>.
  CHECK(run("LORA_HISTORY NODE ab12cd34", false, hooks));
  CHECK(g_hex_text == "ab12cd34" && g_node_id == 0xab12cd34u);
  CHECK(is({"C:parse_hex ab12cd34", "C:history_node",
            "W:LORA_HISTORY_NODE first_utc=1700000000 packets=9 name_utc=1700000100 position_utc=1700000200 "
            "data=1700000300,ab12cd34,Testy,Test Node One,123456789,-987654321\n"}));
  reset();
  g_hex_ok = false;
  CHECK(handle_command("LORA_HISTORY NODE zz", true, hooks));
  CHECK(is({"C:parse_hex zz", "L:LORA_HISTORY_ERROR invalid_id"}));
  reset();
  g_node_found = false;
  CHECK(handle_command("LORA_HISTORY NODE 00000001", true, hooks));
  CHECK(is({"C:parse_hex 00000001", "C:history_node", "L:LORA_HISTORY_ERROR not_found"}));
  reset();
  CHECK(handle_command("LORA_HISTORY NODE ", true, hooks));   // empty id goes to the parser, which decides
  CHECK(g_hex_text.empty());
  CHECK(!run("LORA_HISTORY NODE", true, hooks));              // prefix has a trailing space
  CHECK(!run("LORA_HISTORY NOD ab", true, hooks));
  CHECK(g_log.empty());

  // LORA_MESSAGE <fields>: the hook gets the text after the prefix.
  CHECK(run("LORA_MESSAGE ab12cd34 0000beef 48656c6c6f", false, hooks));
  CHECK(g_fields == "ab12cd34 0000beef 48656c6c6f");
  CHECK(is({"C:present_message", "L:LORA_MESSAGE_OK"}));
  reset();
  g_message_ok = false;
  CHECK(handle_command("LORA_MESSAGE bad", true, hooks));
  CHECK(is({"C:present_message", "L:LORA_MESSAGE_ERROR invalid_fields"}));
  reset();
  CHECK(handle_command("LORA_MESSAGE ", true, hooks));
  CHECK(g_fields.empty());
  CHECK(!run("LORA_MESSAGE", true, hooks));
  CHECK(g_log.empty());

  // LORA_PACKET <fields>.
  CHECK(run("LORA_PACKET ab12cd34 ffffffff 0000beef 1 -7 -90 123456789 -987654321 -", false, hooks));
  CHECK(g_fields == "ab12cd34 ffffffff 0000beef 1 -7 -90 123456789 -987654321 -");
  CHECK(is({"C:present_packet", "L:LORA_PACKET_OK"}));
  reset();
  g_packet_ok = false;
  CHECK(handle_command("LORA_PACKET bad", true, hooks));
  CHECK(is({"C:present_packet", "L:LORA_PACKET_ERROR invalid_fields"}));
  CHECK(!run("LORA_PACKET", true, hooks));
  CHECK(g_log.empty());

  // LORA_MESSAGE_CLEAR: exact; must not be taken for LORA_MESSAGE <fields>.
  CHECK(run("LORA_MESSAGE_CLEAR", false, hooks));
  CHECK(is({"C:clear_messages", "L:LORA_MESSAGE_CLEARED"}));
  CHECK(!run("LORA_MESSAGE_CLEAR ", true, hooks));
  CHECK(g_log.empty());

  // RTL_LORA_MEMORY.
  CHECK(run("RTL_LORA_MEMORY", false, hooks));
  CHECK(is({"C:log_memory"}));
  CHECK(!run("RTL_LORA_MEMORY ", true, hooks));
  CHECK(!run("RTL_LORA_MEM", true, hooks));
  CHECK(g_log.empty());

  // Routes that belong to other families or are not LoRa commands at all.
  const char* const not_ours[] = {"RTL_LORA_NATIVE_STATUS", "RTL_LORA_PLAN_STATUS", "RTL_LORA_REPLAY_ABORT",
                                  "RTL_LORA_REPLAY_BEGIN 1 2", "RTL_LORA_REGION_LIST ", "RTL_LORA_REGION", "", "HELP"};
  for (const char* line : not_ours) {
    CHECK(!run(line, true, hooks));
    CHECK(g_log.empty());
  }

  if (failures == 0) std::printf("serial_lora_tests: all passed\n");
  return failures == 0 ? 0 : 1;
}
