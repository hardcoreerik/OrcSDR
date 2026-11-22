#include "serial_ft8_data.hpp"

#include <strings.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace orcsdr::serial_ft8_data {

using orcsdr::serial_ft8::file_slug;
using orcsdr::serial_ft8::refuse_signed_out;
using orcsdr::serial_ft8::say;

size_t decode_base64(const char* text, uint8_t* out, size_t capacity) {
  size_t count = 0;
  uint32_t acc = 0;
  int bits = 0;
  for (const char* p = text; *p != '\0' && *p != '='; ++p) {
    int v;
    const char c = *p;
    if (c >= 'A' && c <= 'Z') v = c - 'A';
    else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
    else if (c >= '0' && c <= '9') v = c - '0' + 52;
    else if (c == '+') v = 62;
    else if (c == '/') v = 63;
    else continue;
    acc = (acc << 6) | static_cast<uint32_t>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (count < capacity) out[count++] = static_cast<uint8_t>((acc >> bits) & 0xffu);
    }
  }
  return count;
}

namespace {

// The last decoded slot as base64 PCM16 over serial (about 480 KB), for offline analysis; tools/tab5_ft8.py dump writes a WAV.
void dump(const Hooks& hooks) {
  const int16_t* samples = nullptr;
  size_t count = 0;
  uint64_t slot_ms = 0;
  if (!hooks.slot_audio(&samples, &count, &slot_ms)) {
    hooks.io.write_line("ORC_FT8_ERROR DUMP no_slot_yet");
    return;
  }
  static const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  say(hooks.io, "ORC_FT8_DUMP_BEGIN samples=%u rate=12000 slot_utc=%llu\n", static_cast<unsigned>(count),
      static_cast<unsigned long long>(slot_ms / 1000u));
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(samples);
  const size_t total = count * 2u;
  char line[120];
  size_t pos = 0;
  unsigned index = 0;
  while (pos < total) {
    const size_t chunk = (total - pos) > 72u ? 72u : (total - pos);
    int n = snprintf(line, sizeof(line), "ORC_FT8_DUMP_DATA %u ", index++);
    for (size_t i = 0; i < chunk; i += 3) {
      const uint32_t b0 = bytes[pos + i];
      const uint32_t b1 = (i + 1 < chunk) ? bytes[pos + i + 1] : 0u;
      const uint32_t b2 = (i + 2 < chunk) ? bytes[pos + i + 2] : 0u;
      const uint32_t v = (b0 << 16) | (b1 << 8) | b2;
      line[n++] = kB64[(v >> 18) & 63u];
      line[n++] = kB64[(v >> 12) & 63u];
      line[n++] = (i + 1 < chunk) ? kB64[(v >> 6) & 63u] : '=';
      line[n++] = (i + 2 < chunk) ? kB64[v & 63u] : '=';
    }
    line[n++] = '\n';
    line[n] = '\0';
    hooks.io.write(line);   // one write per line: other tasks cannot split it
    pos += chunk;
    if ((index % 32u) == 0) hooks.yield();
  }
  hooks.release_slot_audio();
  hooks.io.write_line("ORC_FT8_DUMP_END");
}

// FT8 INJECT BEGIN <samples> | FT8 INJECT <offset> <base64 PCM16 little-endian> | FT8 INJECT RUN <FT8|FT4>
#if defined(__GNUC__)
__attribute__((noinline))
#endif
void inject(const char* rest, const Hooks& hooks) {
  char sub[16]{};
  unsigned long number = 0;
  int consumed = 0;
  if (sscanf(rest, "%15s %lu%n", sub, &number, &consumed) >= 1 && strcasecmp(sub, "BEGIN") == 0) {
    const bool ok = hooks.inject_begin(static_cast<size_t>(number));
    say(hooks.io, "ORC_FT8_INJECT_%s\n", ok ? "BEGIN_OK" : "ERROR begin (start the decoder with RUN 1 first; at most 16 s)");
    return;
  }
  if (strncasecmp(rest, "PING", 4) == 0) {
    say(hooks.io, "ORC_FT8_INJECT_ACK written=%u crc=%08lx\n", static_cast<unsigned>(hooks.inject_written()),
        hooks.inject_crc32());
    return;
  }
  if (strncasecmp(rest, "RUN", 3) == 0) {
    const char* m = rest + 3;
    while (*m == ' ') ++m;
    const bool ok = hooks.inject_run(strcasecmp(m, "FT4") == 0 ? InjectMode::ft4 : strcasecmp(m, "JS8") == 0 ? InjectMode::js8 : InjectMode::ft8);
    say(hooks.io, "ORC_FT8_INJECT_%s\n", ok ? "RUN_OK" : "ERROR run");
    return;
  }
  // data line: "<offset> <base64>"
  char* end = nullptr;
  const unsigned long offset = strtoul(rest, &end, 10);
  if (end == rest || *end != ' ') {
    hooks.io.write_line("ORC_FT8_INJECT_ERROR usage");
    return;
  }
  const char* b64 = end + 1;
  int16_t chunk[256];
  const size_t out = decode_base64(b64, reinterpret_cast<uint8_t*>(chunk), sizeof(chunk));
  if (!hooks.inject_write(offset, chunk, out / 2)) hooks.io.write_line("ORC_FT8_INJECT_ERROR write");
}

}  // namespace

bool handle(const char* verb, const char* rest, bool authenticated, const Hooks& hooks) {
  if (strcmp(verb, "DUMP") == 0) {
    dump(hooks);
    return true;
  }
  if (strcmp(verb, "ADIF") != 0 && strcmp(verb, "SNAP") != 0 && strcmp(verb, "SHOT") != 0 &&
      strcmp(verb, "INJECT") != 0 && strcmp(verb, "SAVE") != 0) {
    return false;
  }
  if (refuse_signed_out(hooks.io, verb, authenticated)) return true;

  if (strcmp(verb, "ADIF") == 0) {
    // Writes the decode list as an ADIF heard-stations log (receive only: stations heard, not contacts) to /sd/ft8/<name>.adi.
    hooks.drain_decodes();
    if (!hooks.sd_ready()) {
      hooks.io.write_line("ORC_FT8_ERROR ADIF no_sd_card");
      return true;
    }
    char name[40]{};
    if (file_slug(rest, name, sizeof(name)) == 0) snprintf(name, sizeof(name), "heard");
    char path[96];
    snprintf(path, sizeof(path), "/orcsdr/ft8/%s.adi", name);
    unsigned written = 0;
    if (!hooks.adif_write(path, &written)) {
      say(hooks.io, "ORC_FT8_ERROR ADIF open_failed path=%s\n", path);
      return true;
    }
    say(hooks.io, "ORC_FT8_ADIF_OK path=/sd%s records=%u\n", path, written);
    return true;
  }

  if (strcmp(verb, "SNAP") == 0) {
    // The screen as a PNG over serial: no SD card, no RTL_STOP, no retune (tools/tab5_ft8_shot.py uses this).
    hooks.stream_screen_png();
    return true;
  }

  if (strcmp(verb, "SHOT") == 0) {
    // Saves the screen as a BMP on the SD card without pausing reception (UI_CAPTURE needs documentation mode); fetch it with
    // SD_GET_BEGIN / tools/tab5_ft8.py shot.
    char slug[40]{};
    if (file_slug(rest, slug, sizeof(slug)) == 0) snprintf(slug, sizeof(slug), "ft8_screen");
    if (!hooks.sd_ready()) {
      hooks.io.write_line("ORC_FT8_ERROR SHOT no_sd_card");
      return true;
    }
    const char* error = nullptr;
    unsigned bytes = 0;
    if (!hooks.save_screen(slug, &error, &bytes)) {
      say(hooks.io, "ORC_FT8_ERROR SHOT %s\n", error ? error : "capture_failed");
      return true;
    }
    say(hooks.io, "ORC_FT8_SHOT_OK path=/orcsdr/screenshots/%s.bmp bytes=%u\n", slug, bytes);
    return true;
  }

  if (strcmp(verb, "INJECT") == 0) {
    inject(rest, hooks);
    return true;
  }

  // SAVE
  const int16_t* samples = nullptr;
  size_t count = 0;
  uint64_t slot_ms = 0;
  if (!hooks.slot_audio(&samples, &count, &slot_ms)) {
    hooks.io.write_line("ORC_FT8_ERROR SAVE no_slot_yet");
    return true;
  }
  if (!hooks.sd_ready()) {
    hooks.io.write_line("ORC_FT8_ERROR SAVE no_sd_card");
    return true;
  }
  char name[40]{};
  if (file_slug(rest, name, sizeof(name)) == 0)
    snprintf(name, sizeof(name), "slot_%llu", static_cast<unsigned long long>(slot_ms / 1000u));
  char path[96];
  snprintf(path, sizeof(path), "/orcsdr/ft8/%s.wav", name);
  if (!hooks.wav_write(path, samples, count)) {
    say(hooks.io, "ORC_FT8_ERROR SAVE open_failed path=%s\n", path);
    return true;
  }
  hooks.release_slot_audio();
  say(hooks.io, "ORC_FT8_SAVE_OK path=/sd%s samples=%u slot_utc=%llu\n", path, static_cast<unsigned>(count),
      static_cast<unsigned long long>(slot_ms / 1000u));
  return true;
}

}  // namespace orcsdr::serial_ft8_data
