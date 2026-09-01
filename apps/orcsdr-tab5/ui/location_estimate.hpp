#pragma once
#include <cstdint>
namespace orcsdr::location_estimate {
struct State { bool busy=false; bool ready=false; int32_t latitude_e7=0; int32_t longitude_e7=0; char label[40]{}; char message[48]{}; };
bool request(bool wifi_connected);
bool request(const char* query, bool wifi_connected);
State state();
bool self_check();
}
