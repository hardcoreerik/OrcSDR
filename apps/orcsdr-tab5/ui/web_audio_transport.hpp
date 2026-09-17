#pragma once

#include <cstddef>
#include <cstdint>
#include <esp_http_server.h>

namespace orcsdr::web_audio {
// start/stop belong to the main lifecycle owner; stop precedes httpd_stop.
bool start(httpd_handle_t server);
void stop();
bool demanded();
void note_generated(size_t count);
void publish(const int16_t* samples, size_t count);
void invalidate();
struct Counters {
  uint64_t elapsed_us;
  uint64_t producer_samples, ring_written_samples, ring_read_samples;
  uint64_t sent_samples, sent_frames, contention_samples, dropped_samples;
  uint32_t clients, queue_failures, send_errors;
};
Counters counters();
}
