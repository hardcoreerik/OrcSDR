#pragma once
#include <cstdint>
namespace c6_recovery_app {
enum class Phase : uint8_t { idle, quiescing, connecting, release_boot, writing, verifying, restarting, complete, failed, release_boot_failure };
struct Status {Phase phase; uint8_t percent; int result; bool busy;};
Status status();
const char* phase_name(Phase phase);
bool begin(); // caller instructs user to hold physical C6 BOOT before this action
bool confirm_boot_released(bool install); // false: read-only connection test, then restart C6
bool cancel(); // refused once writing has begun
}
