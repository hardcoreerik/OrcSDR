// Host tests for the session journal and offline workflow counters (session_journal.cpp), moved from main.cpp.
// A fake settings store records every write, so the tests also pin WHEN the journal is stored (each append; an
// acknowledge only when it moves forward). The event line is compared with one recorded from firmware 9ce92c03.
// Build with g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -Iapps/orcsdr-tab5/ui, from the
// repository root: tests/session_journal_tests.cpp apps/orcsdr-tab5/ui/session_journal.cpp
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "session_journal.hpp"

namespace {
int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

using namespace orcsdr::session_journal;

std::map<std::string, std::string> g_store;
std::vector<std::string> g_store_calls;   // keys, in order
std::vector<std::string> g_writes;
const char* g_node_id = "m5tab5_0123456789ab";

constexpr Hooks kHooks{
    [](const char* key) -> size_t { return g_store.count(key) ? g_store[key].size() : 0; },
    [](const char* key, void* out, size_t size) { std::memcpy(out, g_store[key].data(), size); },
    [](const char* key, const void* data, size_t size) {
      g_store[key] = std::string(static_cast<const char*>(data), size);
      g_store_calls.push_back(key);
    },
    [](const char* text) { g_writes.push_back(text); },
    []() -> const char* { return g_node_id; },
};

// Boot with nothing stored (the module's records are static, so force a reset through an invalid stored record).
void fresh_boot() {
  g_store.clear();
  g_store["journal"] = std::string(sizeof(JournalState), '\x7f');
  g_store["workflow"] = std::string(sizeof(WorkflowState), '\x7f');
  load(kHooks);
  g_store_calls.clear();
  g_writes.clear();
}

size_t stores_of(const char* key) {
  size_t count = 0;
  for (const auto& call : g_store_calls) count += call == key;
  return count;
}

std::vector<uint32_t> pending_sequences() {
  std::vector<uint32_t> out;
  const JournalState& j = journal();
  for (size_t i = 0; i < j.count; ++i) out.push_back(j.entries[(j.head + i) % kJournalCapacity].sequence);
  return out;
}
}  // namespace

int main() {
  // load: an invalid record is reset and stored at once.
  g_store.clear();
  g_store["journal"] = std::string(sizeof(JournalState), '\x7f');
  g_store["workflow"] = std::string(sizeof(WorkflowState), '\x7f');
  g_store_calls.clear();
  load(kHooks);
  CHECK(journal().magic == kJournalMagic && journal().count == 0 && journal().next_sequence == 0);
  CHECK(workflow().magic == kWorkflowMagic && workflow().config_revision == 0 && workflow().max_runs == 0);
  CHECK(g_store_calls == (std::vector<std::string>{"journal", "workflow"}));
  CHECK(g_store["journal"].size() == 244 && g_store["workflow"].size() == 12);

  // append: sequences count up, kind is cut to 19 characters, each append stores the journal once.
  fresh_boot();
  CHECK(append(kHooks, "boot") == 1);
  CHECK(append(kHooks, "operator_touch", 640, 360) == 2);
  CHECK(append(kHooks, "a_kind_longer_than_nineteen_chars") == 3);
  CHECK(stores_of("journal") == 3 && stores_of("workflow") == 0);
  CHECK(journal().count == 3 && journal().head == 0 && journal().dropped_events == 0);
  CHECK(std::strcmp(journal().entries[0].kind, "boot") == 0 && journal().entries[0].x == -1 &&
        journal().entries[0].y == -1);
  CHECK(journal().entries[1].x == 640 && journal().entries[1].y == 360);
  CHECK(std::strcmp(journal().entries[2].kind, "a_kind_longer_than_") == 0);

  // A valid stored record survives a restart; a wrong-size or out-of-range one does not.
  const std::string saved = g_store["journal"];
  g_store["journal"] = std::string(sizeof(JournalState), '\x7f');
  load(kHooks);
  CHECK(journal().count == 0);
  g_store["journal"] = saved;
  g_store_calls.clear();
  load(kHooks);
  CHECK(journal().count == 3 && journal().next_sequence == 3 && g_store_calls.empty());
  {
    JournalState bad;
    std::memcpy(&bad, saved.data(), sizeof(bad));
    bad.head = kJournalCapacity;                       // out of range
    g_store["journal"] = std::string(reinterpret_cast<const char*>(&bad), sizeof(bad));
    load(kHooks);
    CHECK(journal().count == 0 && journal().next_sequence == 0);
    bad.head = 0;
    bad.count = kJournalCapacity + 1;
    g_store["journal"] = std::string(reinterpret_cast<const char*>(&bad), sizeof(bad));
    load(kHooks);
    CHECK(journal().count == 0);
    g_store["journal"] = saved.substr(0, saved.size() - 1);   // wrong size: not read; the reset record stays
    g_store_calls.clear();
    load(kHooks);
    CHECK(journal().count == 0 && journal().magic == kJournalMagic && g_store_calls.empty());
  }

  // The ring: 8 entries, then the oldest is dropped and counted.
  fresh_boot();
  for (int i = 0; i < 8; ++i) append(kHooks, "fill");
  CHECK(journal().count == 8 && journal().dropped_events == 0);
  CHECK(append(kHooks, "ninth") == 9);
  CHECK(journal().count == 8 && journal().head == 1 && journal().dropped_events == 1);
  CHECK(pending_sequences() == (std::vector<uint32_t>{2, 3, 4, 5, 6, 7, 8, 9}));
  for (int i = 0; i < 10; ++i) append(kHooks, "pressure_test");   // as TEST_PRESSURE does
  CHECK(journal().dropped_events == 11 && journal().next_sequence == 19);
  CHECK(pending_sequences() == (std::vector<uint32_t>{12, 13, 14, 15, 16, 17, 18, 19}));

  // emit_pending: nothing when signed out; one line per event, oldest first, one write each.
  emit_pending(kHooks, false);
  CHECK(g_writes.empty());
  emit_pending(kHooks, true);
  CHECK(g_writes.size() == 8);
  CHECK(g_writes.front().find("\"journal_sequence\":12,") != std::string::npos);
  CHECK(g_writes.back().find("\"journal_sequence\":19,\"event_kind\":\"pressure_test\",\"x\":-1,\"y\":-1}}\n") !=
        std::string::npos);
  CHECK(g_writes.back().back() == '\n');

  // acknowledge: drops up to the sequence; stores only when it moves forward.
  g_store_calls.clear();
  acknowledge(kHooks, 14);
  CHECK(pending_sequences() == (std::vector<uint32_t>{15, 16, 17, 18, 19}));
  CHECK(journal().acknowledged_sequence == 14 && stores_of("journal") == 1);
  acknowledge(kHooks, 14);                       // again: nothing to do, nothing stored
  acknowledge(kHooks, 3);                        // older: ignored
  CHECK(journal().acknowledged_sequence == 14 && journal().count == 5 && stores_of("journal") == 1);
  acknowledge(kHooks, 19);
  CHECK(journal().count == 0 && journal().acknowledged_sequence == 19 && stores_of("journal") == 2);
  g_writes.clear();
  emit_pending(kHooks, true);
  CHECK(g_writes.empty());
  // Existing behaviour, kept: the module does not check the sequence against next_sequence (the ACK command does).
  acknowledge(kHooks, 1000);
  CHECK(journal().acknowledged_sequence == 1000);
  CHECK(append(kHooks, "after") == 20 && journal().count == 1);

  // clear_dropped_events
  g_store_calls.clear();
  CHECK(journal().dropped_events == 11);
  clear_dropped_events(kHooks);
  CHECK(journal().dropped_events == 0 && stores_of("journal") == 1);

  // Workflow: no run until one is installed; runs stop at max_runs; a new revision starts the count again.
  fresh_boot();
  CHECK(!begin_workflow_run(kHooks) && g_store_calls.empty());
  install_workflow(kHooks, 3, 2);
  CHECK(workflow().config_revision == 3 && workflow().max_runs == 2 && workflow().runs == 0);
  CHECK(stores_of("workflow") == 1);
  CHECK(begin_workflow_run(kHooks) && workflow().runs == 1);
  CHECK(begin_workflow_run(kHooks) && workflow().runs == 2);
  CHECK(stores_of("workflow") == 3);
  CHECK(!begin_workflow_run(kHooks) && workflow().runs == 2 && stores_of("workflow") == 3);
  install_workflow(kHooks, 4, 1);
  CHECK(workflow().runs == 0 && begin_workflow_run(kHooks) && !begin_workflow_run(kHooks));
  {
    WorkflowState stored;
    std::memcpy(&stored, g_store["workflow"].data(), sizeof(stored));
    CHECK(stored.magic == kWorkflowMagic && stored.config_revision == 4 && stored.runs == 1 && stored.max_runs == 1);
  }

  // The event line, byte for byte, against the line firmware 9ce92c03 printed (third line of the golden file).
  {
    std::ifstream file("tests/data/session_identity_golden.txt", std::ios::binary);
    std::string hello, snapshot, event;
    std::getline(file, hello);
    std::getline(file, snapshot);
    std::getline(file, event);
    if (!event.empty() && event.back() == '\r') event.pop_back();
    CHECK(!event.empty());
    JournalEntry entry{12923, "session_degraded", -1, -1};
    char line[kEventLineMax];
    const int length = format_event(entry, g_node_id, line, sizeof(line));
    CHECK(length > 0 && std::string(line) == event + "\n");
  }
  // The longest possible line fits the buffer.
  {
    JournalEntry entry{4294967295u, "", -32768, -32768};
    std::memset(entry.kind, 'k', sizeof(entry.kind) - 1);
    char line[kEventLineMax];
    const int length = format_event(entry, "0123456789012345678901234567890", line, sizeof(line));   // 31 characters
    CHECK(length == 265 && length < static_cast<int>(kEventLineMax) && line[length - 1] == '\n');
  }

  if (failures == 0) std::printf("session_journal: all checks passed\n");
  return failures == 0 ? 0 : 1;
}
