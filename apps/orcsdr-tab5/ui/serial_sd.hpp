#pragma once
// SD card transfer over serial: SD_LIST, SD_GET_BEGIN/CHUNK/ABORT, SD_REMOVE, SD_PUT_BEGIN/CHUNK/ABORT.
//
// A serial command family moved out of process_command() in main.cpp (see docs/SERIAL_COMMAND_MODULES.md). The
// routes, their matching and every reply are the ones the dispatcher had. None needs sign-in (owner ruling
// 2026-11-14: keep it open for the copy scripts); the path rules (/orcsdr/ only), the radio-busy refusal and the
// SHA-256 checks stay in main.cpp's transfer code, which this file only calls.
// The handler takes a writable line because SD_PUT_BEGIN splits its arguments in place.
namespace orcsdr::serial_sd {

// Every pointer must be set.
struct Hooks {
  void (*write_line)(const char* text) = nullptr;     // prints the text, then the line ending (Serial.println)
  void (*list)() = nullptr;                           // sd_list()
  void (*get_begin)(const char* path_hex) = nullptr;  // sd_get_begin()
  void (*get_chunk)() = nullptr;                      // sd_get_chunk()
  bool (*get_active)() = nullptr;                     // a download is open
  void (*get_abort)(const char* reason) = nullptr;    // sd_get_abort()
  void (*remove)(const char* path_hex) = nullptr;     // sd_remove()
  void (*put_begin)(char* arguments) = nullptr;       // sd_put_begin(): "<bytes> <sha256 hex> <path hex>", split in place
  void (*put_chunk)(const char* length_text) = nullptr;   // sd_put_chunk()
  bool (*put_active)() = nullptr;                     // an upload is open
  void (*put_abort)(const char* reason) = nullptr;    // sd_put_abort()
};

// Returns true when `command` (not null) was one of these commands.
bool handle_command(char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_sd
