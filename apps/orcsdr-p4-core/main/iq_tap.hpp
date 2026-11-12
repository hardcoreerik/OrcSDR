#pragma once
// A second consumer of the receiver's IQ for the decoder dashboards (POCSAG, P25, ADS-B, LoRa, ...): the radio
// task copies each block into a PSRAM stream buffer without blocking, and one decoder task drains it. Only the
// dashboard on screen runs a tap, so there is at most one. If the decoder falls behind, whole blocks are dropped
// (counted), never the receiver.
#include <cstddef>
#include <cstdint>

namespace iq_tap {
using Consumer = void (*)(const uint8_t* iq, size_t bytes, void* context);

// Starts the decoder task. Returns false if one is already running or memory is short.
bool start(const char* name, Consumer consumer, void* context, size_t buffer_bytes = 256 * 1024,
           int core = 1, int priority = 3, size_t stack_bytes = 8192);
void stop();          // detaches from the radio and ends the task (waits for it)
bool running();
uint32_t dropped();   // blocks dropped because the consumer was behind
uint32_t blocks();    // blocks delivered
}  // namespace iq_tap
