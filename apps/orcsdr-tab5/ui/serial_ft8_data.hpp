#pragma once
// The FT8 verbs that move audio or files: DUMP (no sign-in), ADIF, SAVE, SHOT, SNAP and INJECT (signed in).
// Parsing, the base64 coding and the reply text moved unchanged from process_ft8_command() in main.cpp; the decoder's
// slot audio, the SD card, the screen capture and the recording injection stay there behind Hooks.
#include <cstddef>
#include <cstdint>

#include "serial_ft8_io.hpp"

namespace orcsdr::serial_ft8_data {

enum class InjectMode : unsigned char { ft8, ft4, js8 };

// Decodes base64 text (the alphabet A-Z a-z 0-9 + /; stops at the first '='; skips any other character) into bytes.
// At most `capacity` bytes are written; the rest is dropped. Returns the count written.
size_t decode_base64(const char* text, uint8_t* out, size_t capacity);

// Every pointer must be set.
struct Hooks {
  orcsdr::serial_ft8::Io io;
  // The last decoded slot (12 kHz PCM16). False before the first slot. A true result must be followed by
  // release_slot_audio() when the caller is done with it.
  bool (*slot_audio)(const int16_t** samples, size_t* count, uint64_t* slot_ms) = nullptr;
  void (*release_slot_audio)() = nullptr;
  void (*yield)() = nullptr;                    // give other tasks a tick (every 32 DUMP lines)
  void (*drain_decodes)() = nullptr;
  bool (*sd_ready)() = nullptr;                 // the card is mounted (mounts it when needed)
  // Writes /orcsdr/ft8 (created when missing) `path` as an ADIF log of the decode list; false when the file cannot be opened.
  bool (*adif_write)(const char* path, unsigned* records) = nullptr;
  // Writes the slot as a 16-bit mono WAV at `path`; false when the file cannot be opened.
  bool (*wav_write)(const char* path, const int16_t* samples, size_t count) = nullptr;
  // Saves the screen as /orcsdr/screenshots/<slug>.bmp; on failure sets *error (may stay null).
  bool (*save_screen)(const char* slug, const char** error, unsigned* bytes) = nullptr;
  void (*stream_screen_png)() = nullptr;        // the screen as a PNG over the console, no reply line
  bool (*inject_begin)(size_t samples) = nullptr;
  size_t (*inject_written)() = nullptr;
  unsigned long (*inject_crc32)() = nullptr;
  bool (*inject_run)(InjectMode mode) = nullptr;
  bool (*inject_write)(unsigned long offset, const int16_t* samples, size_t count) = nullptr;
};

// `verb` is upper case, `rest` the remainder of the line without leading spaces. True when the verb was one of these
// (also when it was refused for lack of a session).
bool handle(const char* verb, const char* rest, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_ft8_data
