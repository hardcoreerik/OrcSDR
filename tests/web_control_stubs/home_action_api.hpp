#pragma once
#include <cstdint>
namespace home_app {
enum class WebAction : uint8_t { ok, bad_action, bad_value, not_available, queue_full };
WebAction request_web_action(const char* action, uint32_t value);
}
