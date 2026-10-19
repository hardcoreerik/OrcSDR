#pragma once
#include <cstdint>
namespace lgfx { inline namespace v1 { class LovyanGFX; } }
namespace orcsdr::ft8::map_source {
enum class State : uint8_t { pending, ready, unavailable };
struct Result {
  State state=State::unavailable;
  bool low_memory=false;
  char credit[192]{};
  char status[64]{};
};
// The fixed, whole-world Mercator square is rendered by the shared async worker.
// Only the UI task remaps ready pixels; pending frames must not be sampled.
Result draw(lgfx::v1::LovyanGFX& target, bool labels);
uint32_t revision();
}
