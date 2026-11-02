#pragma once
#include <cstdint>
namespace orcsdr::wifi {
struct ScanResult {};
enum class C6UpdateState : uint8_t { current, updating, rebooting };
struct C6UpdateStatus { C6UpdateState state = C6UpdateState::current; };
C6UpdateStatus c6_update_status();
}
