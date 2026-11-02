#pragma once
#include <cstdint>
namespace wifi_app {
enum class RecoveryAccess : uint8_t { idle, requested, ready, failed, releasing };
struct Status { bool started = false; uint32_t generation=0; char c6_version[16] = {}; };
bool request_recovery_access();
RecoveryAccess recovery_access();
void release_recovery_access();
Status status();
}
