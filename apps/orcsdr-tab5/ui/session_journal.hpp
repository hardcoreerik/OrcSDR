#pragma once
// The host session's local journal (an 8-entry ring of events kept across restarts) and the offline workflow
// counters. Moved unchanged from main.cpp, which now only supplies storage and the console through Hooks.
// Both records are stored as raw structs under the "journal" and "workflow" settings keys, so their layout is part
// of the stored format (see the static_asserts). Host-tested in tests/session_journal_tests.cpp.
#include <cstddef>
#include <cstdint>

namespace orcsdr::session_journal {

constexpr uint32_t kJournalMagic = 0x4f4a4e32;
constexpr uint32_t kWorkflowMagic = 0x4f574631;
constexpr size_t kJournalCapacity = 8;
constexpr size_t kEventLineMax = 320;   // one journal event line; the longest possible is 265 bytes

struct JournalEntry {
  uint32_t sequence;
  char kind[20];
  int16_t x;
  int16_t y;
};

struct JournalState {
  uint32_t magic;
  uint32_t next_sequence;
  uint32_t acknowledged_sequence;
  uint8_t head;
  uint8_t count;
  uint32_t dropped_events;
  JournalEntry entries[kJournalCapacity];
};

struct WorkflowState {
  uint32_t magic;
  uint32_t config_revision;
  uint16_t max_runs;
  uint16_t runs;
};

// The stored blobs: a different size is ignored at load and the record starts empty.
static_assert(sizeof(JournalEntry) == 28);
static_assert(sizeof(JournalState) == 244);
static_assert(sizeof(WorkflowState) == 12);

// Every pointer must be set.
struct Hooks {
  size_t (*stored_size)(const char* key) = nullptr;                          // preferences.getBytesLength
  void (*read)(const char* key, void* out, size_t size) = nullptr;           // preferences.getBytes
  void (*store)(const char* key, const void* data, size_t size) = nullptr;   // preferences.putBytes
  void (*write)(const char* text) = nullptr;                                 // one console write, as given
  const char* (*node_id)() = nullptr;
};

// Reads both records; a missing, wrong-size or invalid record is reset and stored. Call once at boot.
void load(const Hooks& hooks);

// Adds an event (kind is cut to 19 characters), dropping the oldest when the ring is full. Stores the journal on
// every call. Returns the event's sequence number.
uint32_t append(const Hooks& hooks, const char* kind, int16_t x = -1, int16_t y = -1);

// Drops every pending event up to `sequence`; stores only when the acknowledged sequence moved forward.
void acknowledge(const Hooks& hooks, uint32_t sequence);

// One JSON event line per pending event, oldest first. Prints nothing when signed out.
void emit_pending(const Hooks& hooks, bool authenticated);

void clear_dropped_events(const Hooks& hooks);   // set to 0 and store

// New workflow revision: runs start again at 0; stored.
void install_workflow(const Hooks& hooks, uint32_t revision, uint16_t max_runs);

// Counts one offline run and stores it. False (nothing changed) when no workflow is installed or its runs are used up.
bool begin_workflow_run(const Hooks& hooks);

const JournalState& journal();
const WorkflowState& workflow();

// Writes one event line (with its '\n') into `out`; returns snprintf's result.
int format_event(const JournalEntry& entry, const char* node_id, char* out, size_t size);

}  // namespace orcsdr::session_journal
