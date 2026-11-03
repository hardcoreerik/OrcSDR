#pragma once
#include <cstddef>
#include <cstdint>

namespace web_control {
enum class Result : uint8_t { ok, bad_request, not_available, unknown_route };

// Called only after web_auth has verified the signed request. The Home action is queued to
// the Home UI owner; an ok reply means accepted into that queue, not yet applied.
Result run(const char* route, const char* query, uint32_t peer, char* out, size_t cap);
}  // namespace web_control
