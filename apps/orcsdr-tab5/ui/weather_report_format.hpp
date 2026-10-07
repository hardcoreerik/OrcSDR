#pragma once
#include "weather_model.hpp"
#include <cstddef>
#include <cstdint>

namespace orcsdr::weather::report {
enum class OnlinePolicy : uint8_t { disabled, manual, automatic };
struct Session {
  char id[48]{};
  bool utc_valid = false;
  uint32_t started_utc = 0;
  uint32_t started_uptime_ms = 0;
  OnlinePolicy online_policy = OnlinePolicy::disabled;
};
bool encode_observation_csv(const Observation& observation, char* output, size_t capacity);
bool encode_session_json(const Session& session, char* output, size_t capacity);
bool encode_history_jsonl(const Session& session, const char* type, char* output, size_t capacity);
const char* online_policy_key(OnlinePolicy policy);\nbool escape_html(const char* input, char* output, size_t capacity);
}  // namespace orcsdr::weather::report
