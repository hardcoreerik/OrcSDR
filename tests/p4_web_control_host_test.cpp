#include <cassert>
#include <cstring>
#include <string>
#include "home_action_api.hpp"
#include "web_control.hpp"

namespace {
std::string seen_action;
uint32_t seen_value = 0;
uint32_t next_request_id = 42;
home_app::WebAction next_result = home_app::WebAction::ok;
}

namespace home_app {
WebAction request_web_action(const char* action, uint32_t value, uint32_t* request_id) {
  seen_action = action ? action : "";
  seen_value = value;
  if (request_id) *request_id = next_request_id++;
  return next_result;
}
}

int main() {
  char out[128]{};

  auto result = web_control::run("home", "action=tune&value=144800000", 0, out, sizeof(out));
  assert(result == web_control::Result::ok);
  assert(seen_action == "tune" && seen_value == 144800000u);
  assert(std::strstr(out, "\"queued\":true") != nullptr);
  assert(std::strstr(out, "\"request_id\":42") != nullptr);
  assert(std::strstr(out, "\"dispatch\":\"pending\"") != nullptr);

  result = web_control::run("home", "action=step_up", 0, out, sizeof(out));
  assert(result == web_control::Result::ok && seen_action == "step_up" && seen_value == 0);

  for (const char* forbidden : {"volume", "mute", "screen", "open_settings", "bogus"}) {
    std::string query = std::string("action=") + forbidden;
    result = web_control::run("home", query.c_str(), 0, out, sizeof(out));
    assert(result == web_control::Result::bad_request);
    assert(std::strstr(out, "\"ok\":false") != nullptr);
  }

  result = web_control::run("home", "action=tune&value=-1", 0, out, sizeof(out));
  assert(result == web_control::Result::bad_request);
  result = web_control::run("home", "action=tune&value=4294967296", 0, out, sizeof(out));
  assert(result == web_control::Result::bad_request);
  result = web_control::run("home", "action=tune&value=not-a-number", 0, out, sizeof(out));
  assert(result == web_control::Result::bad_request);

  result = web_control::run("home", "action=gain_tenth_db&value=-140", 0, out, sizeof(out));
  assert(result == web_control::Result::ok && seen_action == "gain_tenth_db");
  assert(static_cast<int32_t>(seen_value) == -140);
  result = web_control::run("home", "action=gain_tenth_db&value=-2147483648", 0, out, sizeof(out));
  assert(result == web_control::Result::ok && static_cast<int32_t>(seen_value) == INT32_MIN);
  result = web_control::run("home", "action=gain_tenth_db&value=-2147483649", 0, out, sizeof(out));
  assert(result == web_control::Result::bad_request);
  result = web_control::run("home", "action=filter_set&value=0", 0, out, sizeof(out));
  assert(result == web_control::Result::ok && seen_value == 0);

  next_result = home_app::WebAction::queue_full;
  result = web_control::run("home", "action=step_down", 0, out, sizeof(out));
  assert(result == web_control::Result::not_available);
  assert(std::strstr(out, "queue_full") != nullptr);

  result = web_control::run("receivers/tune", "index=0&hz=100000000", 0, out, sizeof(out));
  assert(result == web_control::Result::not_available);
  assert(std::strstr(out, "not_integrated") != nullptr);
  result = web_control::run("not-a-route", "", 0, out, sizeof(out));
  assert(result == web_control::Result::unknown_route);
}
