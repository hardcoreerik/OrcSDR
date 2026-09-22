#include <cstdio>
#include <cstdlib>

#include "shortwave_dashboard_state.hpp"

namespace {

[[noreturn]] void fail(const char* expression, int line) {
  std::fprintf(stderr, "FAIL line=%d check=%s\n", line, expression);
  std::exit(1);
}

#define CHECK(expression) \
  do { if (!(expression)) fail(#expression, __LINE__); } while (false)

void test_frequency_modal_owns_content_updates() {
  using namespace orcsdr::shortwave;
  DashboardState state;
  CHECK(state.background_redraw_allowed());
  CHECK(state.spectrum_allowed());

  state.open(Modal::frequency);
  CHECK(state.modal() == Modal::frequency);
  CHECK(!state.background_redraw_allowed());
  CHECK(!state.spectrum_allowed());

  state.close_modal();
  CHECK(state.modal() == Modal::none);
  CHECK(state.background_redraw_allowed());
  CHECK(state.spectrum_allowed());
}

void test_modal_switch_does_not_fall_through_to_live() {
  using namespace orcsdr::shortwave;
  DashboardState state;
  state.open(Modal::frequency);
  state.open(Modal::memory_label);
  CHECK(state.modal() == Modal::memory_label);
  CHECK(!state.background_redraw_allowed());
  CHECK(!state.spectrum_allowed());
}

void test_all_tabs_are_selectable() {
  using namespace orcsdr::shortwave;
  DashboardState state;
  CHECK(state.tab() == Tab::live);
  CHECK(!state.select_tab(Tab::on_air));
  CHECK(state.tab() == Tab::on_air);
  CHECK(!state.select_tab(Tab::hunt));
  CHECK(state.tab() == Tab::hunt);
  CHECK(state.select_tab(Tab::memory));
  CHECK(state.tab() == Tab::memory);
  CHECK(!state.select_tab(Tab::logbook));
  CHECK(state.tab() == Tab::logbook);
}

}  // namespace

int main() {
  test_frequency_modal_owns_content_updates();
  test_modal_switch_does_not_fall_through_to_live();
  test_all_tabs_are_selectable();
  std::puts("shortwave_dashboard_state_tests: PASS");
  return 0;
}
