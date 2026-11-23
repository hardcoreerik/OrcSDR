// Host tests for the IQ transfer serial commands (apps/orcsdr-tab5/ui/serial_iq_transfer.cpp). Expected replies are
// typed from the code this replaced in main.cpp.
// Build with g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -Iapps/orcsdr-tab5/ui, from the
// repository root: tests/serial_iq_transfer_tests.cpp apps/orcsdr-tab5/ui/serial_iq_transfer.cpp
#include "serial_iq_transfer.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace orcsdr::serial_iq_transfer;

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

std::vector<std::string> g_writes;
std::vector<std::string> g_calls;
bool g_ready = false;
bool g_resume = false;

const Hooks kHooks = [] {
  Hooks h;
  h.write = [](const char* t) { g_writes.emplace_back(t); };
  h.write_line = [](const char* t) { g_writes.emplace_back(t); g_writes.emplace_back("\n"); };
  h.replay_begin = [](const char* a) { g_calls.push_back(std::string("begin ") + a); };
  h.replay_chunk = [](const char* a) { g_calls.push_back(std::string("chunk ") + a); };
  h.replay_abort = [] { g_calls.emplace_back("abort"); };
  h.retrieve_begin = [](RetrieveInfo* info) {
    g_calls.emplace_back("retrieve");
    if (!g_ready) return false;
    info->bytes = 7680000;
    info->rate = 960000;
    info->frequency_hz = 869525000;
    info->sf = 11;
    info->bandwidth_hz = 250000;
    return true;
  };
  h.get_begin = [] { g_calls.emplace_back("get_begin"); };
  h.get_chunk = [] { g_calls.emplace_back("get_chunk"); };
  h.get_abort = [] {
    g_calls.emplace_back("get_abort");
    AbortInfo a;
    a.ready = g_ready;
    a.bytes = g_ready ? 4000000000u : 0;
    return a;
  };
  h.retrieve_end = [] {
    g_calls.emplace_back("end");
    return g_resume;
  };
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

}  // namespace

int main() {
  // The replay upload needs sign-in; nothing is called when refused.
  for (const char* c : {"RTL_LORA_REPLAY_BEGIN 100 abc", "RTL_LORA_REPLAY_CHUNK 0 ff", "RTL_LORA_REPLAY_ABORT"}) {
    reset();
    CHECK(handle_command(c, false, kHooks));
    CHECK(g_calls.empty() && joined() == "RTL_LORA_REPLAY_ERROR auth_required\n" && g_writes.size() == 2);
  }
  reset();
  CHECK(handle_command("RTL_LORA_REPLAY_BEGIN 100 abc", true, kHooks));
  CHECK(g_calls.size() == 1 && g_calls[0] == "begin 100 abc" && g_writes.empty());
  reset();
  CHECK(handle_command("RTL_LORA_REPLAY_CHUNK 0 ff", true, kHooks));
  CHECK(g_calls.size() == 1 && g_calls[0] == "chunk 0 ff");
  reset();
  CHECK(handle_command("RTL_LORA_REPLAY_ABORT", true, kHooks));
  CHECK(g_calls.size() == 1 && g_calls[0] == "abort");

  // The download needs no sign-in.
  reset();
  g_ready = false;
  CHECK(handle_command("RTL_IQ_RETRIEVE_BEGIN", false, kHooks));
  CHECK(joined() == "RTL_IQ_RETRIEVE_ERROR capture_not_ready\n");
  reset();
  g_ready = true;
  CHECK(handle_command("RTL_IQ_RETRIEVE_BEGIN", false, kHooks));
  CHECK(g_writes.size() == 2 && g_writes[0] == "RTL_IQ_RETRIEVE_READY storage=psram" &&
        g_writes[1] == " bytes=7680000 rate=960000 frequency_hz=869525000 sf=11 bw=250000\n");
  reset();
  CHECK(handle_command("RTL_IQ_GET_BEGIN", false, kHooks) && g_calls.size() == 1 && g_calls[0] == "get_begin");
  reset();
  CHECK(handle_command("RTL_IQ_GET_CHUNK", false, kHooks) && g_calls.size() == 1 && g_calls[0] == "get_chunk");
  reset();
  CHECK(handle_command("RTL_IQ_GET_ABORT", false, kHooks));
  CHECK(g_writes.size() == 1 && g_writes[0] == "RTL_IQ_GET_ABORTED ready=true bytes=4000000000\n");
  reset();
  g_ready = false;
  CHECK(handle_command("RTL_IQ_GET_ABORT", false, kHooks));
  CHECK(g_writes.size() == 1 && g_writes[0] == "RTL_IQ_GET_ABORTED ready=false bytes=0\n");
  reset();
  g_resume = true;
  CHECK(handle_command("RTL_IQ_RETRIEVE_END", false, kHooks));
  CHECK(g_calls.size() == 1 && joined() == "RTL_IQ_RETRIEVE_RESUMING\n");
  reset();
  g_resume = false;
  CHECK(handle_command("RTL_IQ_RETRIEVE_END", false, kHooks));
  CHECK(joined() == "RTL_IQ_RETRIEVE_DONE\n");

  for (const char* other : {"RTL_LORA_REPLAY_BEGIN", "RTL_LORA_REPLAY_ABORT ", "RTL_IQ_GET", "RTL_IQ_GET_CHUNK 1",
                            "RTL_IQ_RETRIEVE", "RTL_IQ_STATUS"}) {
    reset();
    CHECK(!handle_command(other, true, kHooks));
    CHECK(g_calls.empty() && g_writes.empty());
  }
  if (failures) {
    std::printf("serial_iq_transfer_tests: %d FAILED\n", failures);
    return 1;
  }
  std::printf("serial_iq_transfer_tests: ok\n");
  return 0;
}
