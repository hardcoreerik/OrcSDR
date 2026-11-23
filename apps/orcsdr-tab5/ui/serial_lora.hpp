#pragma once
// The LoRa serial commands: RTL_LORA_REGION_LIST, RTL_LORA_AUTO, RTL_LORA_TUNE, LORA_SD_LOG, LORA_HISTORY,
// LORA_MESSAGE, LORA_PACKET, LORA_MESSAGE_CLEAR and RTL_LORA_MEMORY.
//
// A serial command family moved out of process_command() in main.cpp (see serial_help.hpp for the convention). The
// routes, their order, the authentication checks and every reply are the ones the dispatcher had. Everything the
// commands read from or do to the device comes through Hooks, so the family builds and is tested on the host; it
// keeps no state of its own. The LoRa decode buffers stay in main.cpp: each hook does its whole access (including
// the critical section) exactly as the dispatcher did.
#include <cstddef>
#include <cstdint>

namespace orcsdr::serial_lora {

// Size of HistoryNode::data; main.cpp checks it equals lora_history::kLineMax.
constexpr size_t kHistoryLineMax = 768;

struct Region {
  const char* code = "";
  unsigned slots = 0;
  unsigned default_slot = 0;
};

enum class HistoryFiles : uint8_t {
  ok,
  radio_busy,
  sd_unavailable,
  open_failed,
};

struct HistoryStatus {
  unsigned loaded = 0;
  unsigned nodes = 0;
  unsigned long replayed = 0;
  unsigned long rejected = 0;
  unsigned long written = 0;
  unsigned error = 0;
};

struct HistoryNode {
  unsigned long first_utc = 0;
  unsigned long packets = 0;
  unsigned long name_utc = 0;
  unsigned long position_utc = 0;
  char data[kHistoryLineMax] = {};   // the formatted history line for the node (ends in a line ending)
};

struct SdLogStatus {
  bool requested = false;
  bool ready = false;
  bool error = false;
  unsigned queued = 0;
  unsigned long dropped = 0;
  const char* path = "";
  unsigned pending = 0;
  unsigned long retry_attempts = 0;
  const char* last_error_stage = "";
  int last_errno = 0;
};

// Called once per archive file found by history_files, in directory order.
using HistoryFileEmit = void (*)(void* context, unsigned long long bytes, const char* name);

// Every pointer must be set.
struct Hooks {
  void (*write)(const char* text) = nullptr;        // prints the text as given, in one write
  void (*write_line)(const char* text) = nullptr;   // prints the text, then the line ending (Serial.println)
  size_t (*region_count)() = nullptr;
  Region (*region_at)(size_t index) = nullptr;
  void (*set_detector)(bool enabled) = nullptr;     // RTL_LORA_AUTO
  uint32_t min_hz = 0;                              // RTL_LORA_TUNE accepts min_hz..max_hz
  uint32_t max_hz = 0;
  void (*tune)(uint32_t frequency_hz) = nullptr;    // hot retune when the LoRa band is running, else queue a listen
  void (*set_sd_logging)(bool enabled) = nullptr;
  HistoryFiles (*history_files)(void* context, HistoryFileEmit emit) = nullptr;
  HistoryStatus (*history_status)() = nullptr;
  bool (*parse_hex_u32)(const char* text, uint32_t* value) = nullptr;
  bool (*history_node)(uint32_t id, HistoryNode* node) = nullptr;   // false when the node is not known
  SdLogStatus (*sd_log_status)() = nullptr;
  bool (*present_message)(const char* fields) = nullptr;   // the text after "LORA_MESSAGE "; false when invalid
  bool (*present_packet)(const char* fields) = nullptr;    // the text after "LORA_PACKET "; false when invalid
  void (*clear_messages)() = nullptr;
  void (*log_memory)() = nullptr;                   // RTL_LORA_MEMORY: prints its own report
};

// Returns true when `command` (not null) was one of these commands, whether it ran or was refused.
bool handle_command(const char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_lora
