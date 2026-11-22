#include "iq_transfer_engine.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#include <mbedtls/sha256.h>

#include "sd_transfer.hpp"   // the chunk sizes the IQ transfers share with the SD transfers

namespace orcsdr::iq_transfer_engine {
namespace {

struct IqGetState {
  bool active = false;
  size_t sent = 0;
  mbedtls_sha256_context sha;
};
IqGetState g_iq_get;
struct LoraReplayPutState {
  bool active = false;
  size_t expected = 0;
  size_t received = 0;
  uint32_t rate = 0;
  uint32_t frequency_hz = 0;
  uint32_t bandwidth_hz = 0;
  uint8_t sf = 0;
  uint8_t expected_sha[32]{};
  mbedtls_sha256_context sha;
};
LoraReplayPutState g_lora_replay_put;

// Serial.printf() in main.cpp: format, then one write.
__attribute__((format(printf, 2, 3))) void say(const Hooks& hooks, const char* format, ...) {
  char line[160];
  va_list args;
  va_start(args, format);
  vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  hooks.write(line);
}

void lora_replay_put_reset() {
  if (g_lora_replay_put.active) mbedtls_sha256_free(&g_lora_replay_put.sha);
  g_lora_replay_put = {};
}

}  // namespace

bool get_active() { return g_iq_get.active; }
bool replay_active() { return g_lora_replay_put.active; }

void get_reset() {
  if (g_iq_get.active) mbedtls_sha256_free(&g_iq_get.sha);
  g_iq_get = {};
}

void get_abort(const Hooks& hooks, const char* reason) {
  get_reset();
  say(hooks, "RTL_IQ_GET_ERROR %s\n", reason ? reason : "aborted");
}

void get_begin(const Hooks& hooks) {
  if (g_iq_get.active) {
    hooks.write_line("RTL_IQ_GET_ERROR already_active");
    return;
  }
  size_t bytes = 0;
  if (!hooks.capture_ready(&bytes)) {
    hooks.write_line("RTL_IQ_GET_ERROR capture_not_ready");
    return;
  }
  mbedtls_sha256_init(&g_iq_get.sha);
  if (mbedtls_sha256_starts(&g_iq_get.sha, 0) != 0) {
    g_iq_get.active = true;
    get_abort(hooks, "sha_start");
    return;
  }
  g_iq_get.active = true;
  g_iq_get.sent = 0;
  say(hooks, "RTL_IQ_GET_READY chunk=%u bytes=%u\n",
      static_cast<unsigned>(orcsdr::sd_transfer::kGetChunkBytes), static_cast<unsigned>(bytes));
}

void get_chunk(const Hooks& hooks) {
  if (!g_iq_get.active) {
    hooks.write_line("RTL_IQ_GET_ERROR not_active");
    return;
  }
  const size_t total = hooks.capture_bytes();
  const size_t remaining = total - g_iq_get.sent;
  const size_t count = std::min(remaining, orcsdr::sd_transfer::kGetChunkBytes);
  const uint8_t* data = hooks.capture_data() + g_iq_get.sent;
  if (mbedtls_sha256_update(&g_iq_get.sha, data, count) != 0) {
    get_abort(hooks, "sha_update");
    return;
  }
  say(hooks, "RTL_IQ_GET_DATA bytes=%u\n", static_cast<unsigned>(count));
  if (hooks.write_bytes(data, count) != count) {
    get_abort(hooks, "serial_write");
    return;
  }
  g_iq_get.sent += count;
  if (g_iq_get.sent != total) return;

  uint8_t digest[32];
  if (mbedtls_sha256_finish(&g_iq_get.sha, digest) != 0) {
    get_abort(hooks, "sha_finish");
    return;
  }
  mbedtls_sha256_free(&g_iq_get.sha);
  g_iq_get.active = false;
  say(hooks, "RTL_IQ_GET_DONE bytes=%u sha256=", static_cast<unsigned>(total));
  hooks.write_hex(digest, sizeof(digest));
  hooks.write("\n");   // Serial.println()
  g_iq_get = {};
}

void replay_abort(const Hooks& hooks, const char* reason) {
  lora_replay_put_reset();
  say(hooks, "RTL_LORA_REPLAY_ERROR %s\n", reason ? reason : "aborted");
}

void replay_begin(const Hooks& hooks, const char* arguments) {
  unsigned bytes = 0, rate = 0, frequency_hz = 0, sf = 0, bandwidth_hz = 0;
  char sha_text[65]{};
  char trailing = 0;
  if (g_lora_replay_put.active ||
      sscanf(arguments, "%u %64s %u %u %u %u %c", &bytes, sha_text, &rate,
             &frequency_hz, &sf, &bandwidth_hz, &trailing) != 6 ||
      bytes == 0 || bytes > hooks.replay_max_bytes() || (bytes & 1u) != 0 ||
      rate != hooks.sample_rate() || sf < 7 || sf > 12 || bandwidth_hz == 0 ||
      !hooks.decode_hex(sha_text, g_lora_replay_put.expected_sha,
                        sizeof(g_lora_replay_put.expected_sha))) {
    hooks.write_line("RTL_LORA_REPLAY_ERROR invalid_begin");
    return;
  }
  if (hooks.replay_busy()) {
    hooks.write_line("RTL_LORA_REPLAY_ERROR radio_or_decoder_busy");
    return;
  }
  if (!hooks.prepare_decoder()) {
    hooks.write_line("RTL_LORA_REPLAY_ERROR decoder_unavailable");
    return;
  }
  g_lora_replay_put.expected = bytes;
  g_lora_replay_put.rate = rate;
  g_lora_replay_put.frequency_hz = frequency_hz;
  g_lora_replay_put.sf = static_cast<uint8_t>(sf);
  g_lora_replay_put.bandwidth_hz = bandwidth_hz;
  mbedtls_sha256_init(&g_lora_replay_put.sha);
  if (mbedtls_sha256_starts(&g_lora_replay_put.sha, 0) != 0) {
    g_lora_replay_put.active = true;
    replay_abort(hooks, "sha_start");
    return;
  }
  g_lora_replay_put.active = true;
  say(hooks, "RTL_LORA_REPLAY_READY chunk=%u bytes=%u\n",
      static_cast<unsigned>(orcsdr::sd_transfer::kPutChunkBytes), bytes);
}

void replay_chunk(const Hooks& hooks, const char* length_text) {
  if (!g_lora_replay_put.active) {
    hooks.write_line("RTL_LORA_REPLAY_ERROR not_active");
    return;
  }
  const size_t length = static_cast<size_t>(strtoul(length_text, nullptr, 10));
  if (length == 0 || length > orcsdr::sd_transfer::kPutChunkBytes ||
      g_lora_replay_put.received + length > g_lora_replay_put.expected) {
    replay_abort(hooks, "invalid_chunk");
    return;
  }
  hooks.write_line("RTL_LORA_REPLAY_DATA");
  uint8_t* destination = hooks.replay_buffer() + g_lora_replay_put.received;
  const size_t got = hooks.read_bytes(destination, length, 5000);
  if (got != length ||
      mbedtls_sha256_update(&g_lora_replay_put.sha, destination, length) != 0) {
    replay_abort(hooks, got != length ? "chunk_timeout" : "sha_update");
    return;
  }
  g_lora_replay_put.received += length;
  if (g_lora_replay_put.received != g_lora_replay_put.expected) {
    say(hooks, "RTL_LORA_REPLAY_ACK bytes=%u\n", static_cast<unsigned>(g_lora_replay_put.received));
    return;
  }

  uint8_t digest[32];
  if (mbedtls_sha256_finish(&g_lora_replay_put.sha, digest) != 0) {
    replay_abort(hooks, "sha_finish");
    return;
  }
  uint8_t difference = 0;
  for (size_t i = 0; i < sizeof(digest); ++i)
    difference |= digest[i] ^ g_lora_replay_put.expected_sha[i];
  if (difference != 0) {
    replay_abort(hooks, "sha_mismatch");
    return;
  }
  if (!hooks.queue_decode(g_lora_replay_put.received, g_lora_replay_put.sf, g_lora_replay_put.bandwidth_hz,
                          g_lora_replay_put.frequency_hz)) {
    replay_abort(hooks, "queue_failed");
    return;
  }
  const size_t queued = g_lora_replay_put.received;
  lora_replay_put_reset();
  say(hooks, "RTL_LORA_REPLAY_QUEUED bytes=%u\n", static_cast<unsigned>(queued));
}

}  // namespace orcsdr::iq_transfer_engine
