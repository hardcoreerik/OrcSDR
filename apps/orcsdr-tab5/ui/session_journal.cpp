#include "session_journal.hpp"

#include <cstdio>
#include <cstring>

namespace orcsdr::session_journal {

namespace {

JournalState g_journal{};
WorkflowState g_workflow{};

void persist_journal(const Hooks& hooks) {
  hooks.store("journal", &g_journal, sizeof(g_journal));
}

void persist_workflow(const Hooks& hooks) {
  hooks.store("workflow", &g_workflow, sizeof(g_workflow));
}

}  // namespace

void load(const Hooks& hooks) {
  if (hooks.stored_size("journal") == sizeof(g_journal)) {
    hooks.read("journal", &g_journal, sizeof(g_journal));
  }
  if (g_journal.magic != kJournalMagic || g_journal.head >= kJournalCapacity ||
      g_journal.count > kJournalCapacity) {
    memset(&g_journal, 0, sizeof(g_journal));
    g_journal.magic = kJournalMagic;
    persist_journal(hooks);
  }
  if (hooks.stored_size("workflow") == sizeof(g_workflow)) {
    hooks.read("workflow", &g_workflow, sizeof(g_workflow));
  }
  if (g_workflow.magic != kWorkflowMagic) {
    memset(&g_workflow, 0, sizeof(g_workflow));
    g_workflow.magic = kWorkflowMagic;
    persist_workflow(hooks);
  }
}

uint32_t append(const Hooks& hooks, const char* kind, int16_t x, int16_t y) {
  size_t index;
  if (g_journal.count < kJournalCapacity) {
    index = (g_journal.head + g_journal.count) % kJournalCapacity;
    ++g_journal.count;
  } else {
    index = g_journal.head;
    g_journal.head = (g_journal.head + 1) % kJournalCapacity;
    ++g_journal.dropped_events;
  }
  JournalEntry& entry = g_journal.entries[index];
  entry.sequence = ++g_journal.next_sequence;
  strncpy(entry.kind, kind, sizeof(entry.kind) - 1);
  entry.kind[sizeof(entry.kind) - 1] = '\0';
  entry.x = x;
  entry.y = y;
  persist_journal(hooks);
  return entry.sequence;
}

void acknowledge(const Hooks& hooks, uint32_t sequence) {
  while (g_journal.count > 0 && g_journal.entries[g_journal.head].sequence <= sequence) {
    g_journal.head = (g_journal.head + 1) % kJournalCapacity;
    --g_journal.count;
  }
  if (sequence > g_journal.acknowledged_sequence) {
    g_journal.acknowledged_sequence = sequence;
    persist_journal(hooks);
  }
}

int format_event(const JournalEntry& entry, const char* node_id, char* out, size_t size) {
  return snprintf(out, size,
      "{\"type\":\"event\",\"message_id\":\"m5tab5_journal_%lu\","
      "\"protocol_version\":{\"major\":1,\"minor\":0},"
      "\"payload\":{\"node_id\":\"%s\",\"kind\":\"local_journal\","
      "\"journal_sequence\":%lu,\"event_kind\":\"%s\",\"x\":%d,\"y\":%d}}\n",
      static_cast<unsigned long>(entry.sequence), node_id,
      static_cast<unsigned long>(entry.sequence), entry.kind, entry.x, entry.y);
}

void emit_pending(const Hooks& hooks, bool authenticated) {
  if (!authenticated) return;
  for (size_t offset = 0; offset < g_journal.count; ++offset) {
    const JournalEntry& entry = g_journal.entries[(g_journal.head + offset) % kJournalCapacity];
    char line[kEventLineMax];
    if (format_event(entry, hooks.node_id(), line, sizeof(line)) > 0) hooks.write(line);
  }
}

void clear_dropped_events(const Hooks& hooks) {
  g_journal.dropped_events = 0;
  persist_journal(hooks);
}

void install_workflow(const Hooks& hooks, uint32_t revision, uint16_t max_runs) {
  g_workflow.config_revision = revision;
  g_workflow.max_runs = max_runs;
  g_workflow.runs = 0;
  persist_workflow(hooks);
}

bool begin_workflow_run(const Hooks& hooks) {
  if (g_workflow.config_revision == 0 || g_workflow.runs >= g_workflow.max_runs) return false;
  ++g_workflow.runs;
  persist_workflow(hooks);
  return true;
}

const JournalState& journal() { return g_journal; }
const WorkflowState& workflow() { return g_workflow; }

}  // namespace orcsdr::session_journal
