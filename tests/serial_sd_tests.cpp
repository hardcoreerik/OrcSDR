// Host tests for the SD transfer routes moved out of main.cpp (serial_sd.cpp). Expected behaviour is typed from the
// code this replaced: each route hands its argument to the transfer code unchanged, needs no sign-in, and the two
// ABORT commands answer by themselves when nothing is open.
// Build with g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -Iapps/orcsdr-tab5/ui, from the
// repository root: tests/serial_sd_tests.cpp apps/orcsdr-tab5/ui/serial_sd.cpp
#include <cstdio>
#include <string>
#include <vector>

#include "serial_sd.hpp"

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

std::vector<std::string> g_calls;
bool g_get_active = false, g_put_active = false;

const orcsdr::serial_sd::Hooks kHooks = [] {
  orcsdr::serial_sd::Hooks h;
  h.write_line = [](const char* t) { g_calls.push_back(std::string("line ") + t); };
  h.list = [] { g_calls.push_back("list"); };
  h.get_begin = [](const char* a) { g_calls.push_back(std::string("get_begin[") + a + "]"); };
  h.get_chunk = [] { g_calls.push_back("get_chunk"); };
  h.get_active = [] { return g_get_active; };
  h.get_abort = [](const char* r) { g_calls.push_back(std::string("get_abort ") + r); };
  h.remove = [](const char* a) { g_calls.push_back(std::string("remove[") + a + "]"); };
  h.put_begin = [](char* a) {
    g_calls.push_back(std::string("put_begin[") + a + "]");
    a[0] = '\0';   // the real handler splits the line in place: it must be writable
  };
  h.put_chunk = [](const char* a) { g_calls.push_back(std::string("put_chunk[") + a + "]"); };
  h.put_active = [] { return g_put_active; };
  h.put_abort = [](const char* r) { g_calls.push_back(std::string("put_abort ") + r); };
  return h;
}();

std::string run(const std::string& line, bool authenticated = false, bool* handled = nullptr) {
  g_calls.clear();
  std::vector<char> buffer(line.begin(), line.end());
  buffer.push_back('\0');
  const bool h = orcsdr::serial_sd::handle_command(buffer.data(), authenticated, kHooks);
  if (handled) *handled = h;
  std::string out;
  for (const auto& c : g_calls) out += c + ";";
  return out;
}

}  // namespace

int main() {
  bool handled = false;
  // Signed out and signed in behave the same.
  for (bool auth : {false, true}) {
    CHECK(run("SD_LIST", auth, &handled) == "list;" && handled);
    CHECK(run("SD_GET_BEGIN 2f6f72", auth) == "get_begin[2f6f72];");
    CHECK(run("SD_GET_CHUNK", auth) == "get_chunk;");
    CHECK(run("SD_REMOVE 2f6f72", auth) == "remove[2f6f72];");
    CHECK(run("SD_PUT_BEGIN 10 abcd 2f6f72", auth) == "put_begin[10 abcd 2f6f72];");
    CHECK(run("SD_PUT_CHUNK 512", auth) == "put_chunk[512];");
  }
  // Empty arguments are passed through for the transfer code to refuse.
  CHECK(run("SD_GET_BEGIN ") == "get_begin[];");
  CHECK(run("SD_PUT_CHUNK ") == "put_chunk[];");

  // ABORT: aborts an open transfer, otherwise answers by itself.
  g_get_active = g_put_active = false;
  CHECK(run("SD_GET_ABORT") == "line SD_GET_ABORTED;");
  CHECK(run("SD_PUT_ABORT") == "line SD_PUT_ABORTED;");
  g_get_active = g_put_active = true;
  CHECK(run("SD_GET_ABORT") == "get_abort host_abort;");
  CHECK(run("SD_PUT_ABORT") == "put_abort host_abort;");

  // Near misses are not these routes.
  for (const char* line : {"SD_LISTX", "SD_LIST ", "SD_GET_BEGIN", "SD_GET_CHUNK ", "SD_REMOVE", "SD_PUT_BEGIN",
                           "SD_PUT_CHUNK", "SD_PUT_ABORT ", "sd_list", "SD_GET_ABORTX"}) {
    CHECK(run(line, true, &handled).empty() && !handled);
  }

  if (failures == 0) std::printf("serial_sd: all checks passed\n");
  return failures == 0 ? 0 : 1;
}
