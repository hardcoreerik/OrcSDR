// Host test for the memory reclaim registry: nothing registered, what is held and released, ordering, replacement, limits.
#include "memory_reclaim.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace orcsdr::memory_reclaim;

namespace {
void require(bool pass, const char* why) {
  if (!pass) {
    std::fprintf(stderr, "MEMORY RECLAIM FAIL: %s\n", why);
    std::exit(1);
  }
}
size_t g_a = 900000, g_b = 40000, g_c = 0;
size_t a_held() { return g_a; }
size_t a_release() { const size_t n = g_a; g_a = 0; return n; }
size_t b_held() { return g_b; }
size_t b_release() { const size_t n = g_b; g_b = 0; return n; }
size_t c_held() { return g_c; }
size_t c_release() { return 0; }   // a holder that has nothing to give up right now
}  // namespace

int main() {
  clear_for_tests();
  Report out[kMaxHolders];
  size_t n = 0;
  require(total_held() == 0 && list(out, kMaxHolders) == 0, "nothing registered: nothing held");
  require(reclaim_all(out, kMaxHolders, &n) == 0 && n == 0, "nothing registered: nothing released");

  require(!register_holder({nullptr, a_held, a_release}), "a holder needs a name");
  require(!register_holder({"x", nullptr, a_release}) && !register_holder({"x", a_held, nullptr}), "a holder needs both callbacks");

  require(register_holder({"SSTV pictures", a_held, a_release}), "register A");
  require(register_holder({"LoRa capture", b_held, b_release}), "register B");
  require(register_holder({"Idle", c_held, c_release}), "register C");
  require(total_held() == 940000, "total held is the sum");

  const size_t listed = list(out, kMaxHolders);
  require(listed == 3 && std::strcmp(out[0].name, "SSTV pictures") == 0 && out[0].held == 900000, "largest holder first");
  require(std::strcmp(out[1].name, "LoRa capture") == 0 && std::strcmp(out[2].name, "Idle") == 0 && out[2].held == 0, "the rest in size order");
  require(list(out, 2) == 2, "a short output buffer is respected");

  const size_t released = reclaim_all(out, kMaxHolders, &n);
  require(released == 940000 && n == 3, "everything a holder can give up is released");
  require(std::strcmp(out[0].name, "SSTV pictures") == 0 && out[0].held == 900000, "released amounts are reported per holder, largest first");
  require(total_held() == 0 && g_a == 0 && g_b == 0, "after reclaim nothing is held");
  require(reclaim_all(nullptr, 0, nullptr) == 0, "a second reclaim releases nothing and accepts a null report");

  g_a = 5;
  require(register_holder({"SSTV pictures", a_held, a_release}) && list(out, kMaxHolders) == 3, "registering the same name replaces it");
  clear_for_tests();
  static const char* names[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13"};
  size_t accepted = 0;
  for (const char* name : names) accepted += register_holder({name, c_held, c_release}) ? 1u : 0u;
  require(accepted == kMaxHolders, "the table holds exactly kMaxHolders entries and refuses the rest");
  std::puts("MEMORY RECLAIM: PASS");
  return 0;
}
