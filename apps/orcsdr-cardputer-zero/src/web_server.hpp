// Linux-only loopback static host for original OrcSDR Web/WASM assets.
// This is not a remote radio control server.
#pragma once
#include <cstdint>
#include <string>
namespace orc::linux_sdr {
int run_loopback_web(const std::string& directory,std::uint16_t port,
                     std::uint32_t max_requests=0);
}
