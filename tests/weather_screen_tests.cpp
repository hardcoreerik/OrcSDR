#include "screen_controller.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

[[noreturn]] static void fail(const char* expression, int line) {
  std::fprintf(stderr, "FAIL line=%d check=%s\n", line, expression);
  std::exit(1);
}
#define CHECK(expression) do { if (!(expression)) fail(#expression, __LINE__); } while (false)

int main() {
  using namespace orcsdr::screens;
  begin_transition(Id::weather, 1);
  finish_transition();
  CHECK(owns(Id::weather));
  CHECK(std::strcmp(name(Id::weather), "weather") == 0);
  begin_transition(Id::settings, 2, true);
  finish_transition();
  CHECK(close_settings(3) == Id::weather);
  finish_transition();
  CHECK(owns(Id::weather));
  CHECK(self_check());
  std::puts("weather_screen_tests: PASS");
}
