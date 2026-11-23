#include "shortwave_library_actions.hpp"

#include <cstdint>

namespace orcsdr::shortwave_library_actions {
namespace {

using orcsdr::action_io::copy_text;
using orcsdr::action_io::say;
using orcsdr::shortwave::ActionKind;

void save_memory(const Hooks& hooks) {
  auto& library = *hooks.state;
  hooks.ensure_loaded();
  orcsdr::shortwave::Memory memory{};
  memory.frequency_hz = hooks.ui_frequency_hz();
  memory.bandwidth_hz = hooks.filter_bandwidth_hz();
  memory.saved_utc = hooks.now().utc;
  copy_text(memory.mode, hooks.mode_name_at(memory.frequency_hz), sizeof(memory.mode));
  copy_text(memory.station, hooks.pending_memory_label(), sizeof(memory.station));
  copy_text(memory.notes, hooks.pending_memory_notes(), sizeof(memory.notes));
  const auto result = library.memories.upsert(memory);
  char error[96]{};
  if (result == orcsdr::shortwave::RecordResult::ok &&
      hooks.save_memories(library.memories, error, sizeof(error))) {
    library.status = orcsdr::shortwave::StorageStatus::ready;
    say(hooks.io, "RTL_SHORTWAVE_MEMORY saved frequency_hz=%lu label=%s\n",
        static_cast<unsigned long>(memory.frequency_hz), memory.station);
  } else if (result != orcsdr::shortwave::RecordResult::duplicate) {
    if (result == orcsdr::shortwave::RecordResult::ok) library.memories.discard_last();
    library.status = orcsdr::shortwave::StorageStatus::write_failed;
    say(hooks.io, "RTL_SHORTWAVE_MEMORY save_failed result=%u error=%s\n",
        static_cast<unsigned>(result), error);
  }
}

void change_memory(const orcsdr::shortwave::Action& action, const Hooks& hooks) {
  auto& library = *hooks.state;
  hooks.ensure_loaded();
  const size_t index = action.value < 0 ? SIZE_MAX : static_cast<size_t>(action.value);
  orcsdr::shortwave::Memory before{};
  const auto* existing = library.memories.at(index);
  const bool had_record = existing != nullptr;
  if (existing) before = *existing;
  auto result = orcsdr::shortwave::RecordResult::missing;
  if (action.kind == ActionKind::update_memory) {
    if (existing) {
      auto edited = *existing;
      copy_text(edited.station, hooks.pending_memory_label(), sizeof(edited.station));
      copy_text(edited.notes, hooks.pending_memory_notes(), sizeof(edited.notes));
      result = library.memories.replace(index, edited);
    }
  } else if (action.kind == ActionKind::favorite_memory) {
    result = library.memories.toggle_favorite(index);
  } else {
    result = library.memories.erase(index);
  }
  char error[96]{};
  if (result == orcsdr::shortwave::RecordResult::ok &&
      hooks.save_memories(library.memories, error, sizeof(error))) {
    library.status = orcsdr::shortwave::StorageStatus::ready;
    say(hooks.io, "RTL_SHORTWAVE_MEMORY changed action=%u index=%u\n",
        static_cast<unsigned>(action.kind), static_cast<unsigned>(index));
  } else {
    if (result == orcsdr::shortwave::RecordResult::ok && had_record) {
      if (action.kind == ActionKind::delete_memory)
        library.memories.insert(index, before);
      else
        library.memories.replace(index, before);
    }
    library.status = orcsdr::shortwave::StorageStatus::write_failed;
    say(hooks.io, "RTL_SHORTWAVE_MEMORY change_failed action=%u index=%u error=%s\n",
        static_cast<unsigned>(action.kind), static_cast<unsigned>(index), error);
  }
}

void save_log(const Hooks& hooks) {
  auto& library = *hooks.state;
  hooks.ensure_loaded();
  const Clock clock = hooks.now();
  if (!clock.wallclock_valid) {
    hooks.io.write_line("RTL_SHORTWAVE_LOG save_failed error=utc_clock_not_set");
    return;
  }
  orcsdr::shortwave::LogEntry entry{};
  entry.timestamp_utc = clock.utc;
  entry.frequency_hz = hooks.ui_frequency_hz();
  entry.bandwidth_hz = hooks.filter_bandwidth_hz();
  entry.signal_dbfs = hooks.signal_dbfs();
  copy_text(entry.mode, hooks.mode_name_at(entry.frequency_hz), sizeof(entry.mode));
  hooks.snapshot_device(entry.device, sizeof(entry.device));
  copy_text(entry.antenna,
            hooks.pending_log_antenna()[0] ? hooks.pending_log_antenna() : "Not recorded",
            sizeof(entry.antenna));
  copy_text(entry.notes, hooks.pending_log_notes(), sizeof(entry.notes));
  for (size_t i = 0; i < library.memories.size(); ++i) {
    const auto* memory = library.memories.at(i);
    if (memory && memory->frequency_hz == entry.frequency_hz) {
      copy_text(entry.station, memory->station, sizeof(entry.station));
      break;
    }
  }
  const auto result = library.logs.append(entry);
  char error[96]{};
  if (result == orcsdr::shortwave::RecordResult::ok &&
      hooks.save_logs(library.logs, error, sizeof(error))) {
    library.status = orcsdr::shortwave::StorageStatus::ready;
    say(hooks.io, "RTL_SHORTWAVE_LOG saved frequency_hz=%lu antenna=%s\n",
        static_cast<unsigned long>(entry.frequency_hz), entry.antenna);
  } else {
    if (result == orcsdr::shortwave::RecordResult::ok) library.logs.discard_last();
    library.status = orcsdr::shortwave::StorageStatus::write_failed;
    say(hooks.io, "RTL_SHORTWAVE_LOG save_failed result=%u error=%s\n",
        static_cast<unsigned>(result), error);
  }
}

void change_log(const orcsdr::shortwave::Action& action, const Hooks& hooks) {
  auto& library = *hooks.state;
  hooks.ensure_loaded();
  const size_t index = action.value < 0 ? SIZE_MAX : static_cast<size_t>(action.value);
  orcsdr::shortwave::LogEntry before{};
  const auto* existing = library.logs.at(index);
  const bool had_record = existing != nullptr;
  if (existing) before = *existing;
  auto result = orcsdr::shortwave::RecordResult::missing;
  if (action.kind == ActionKind::update_log) {
    if (existing) {
      auto edited = *existing;
      copy_text(edited.antenna, hooks.pending_log_antenna(), sizeof(edited.antenna));
      copy_text(edited.notes, hooks.pending_log_notes(), sizeof(edited.notes));
      result = library.logs.replace(index, edited);
    }
  } else {
    result = library.logs.erase(index);
  }
  char error[96]{};
  if (result == orcsdr::shortwave::RecordResult::ok &&
      hooks.save_logs(library.logs, error, sizeof(error))) {
    library.status = orcsdr::shortwave::StorageStatus::ready;
    say(hooks.io, "RTL_SHORTWAVE_LOG changed action=%u index=%u\n",
        static_cast<unsigned>(action.kind), static_cast<unsigned>(index));
  } else {
    if (result == orcsdr::shortwave::RecordResult::ok && had_record) {
      if (action.kind == ActionKind::delete_log)
        library.logs.insert(index, before);
      else
        library.logs.replace(index, before);
    }
    library.status = orcsdr::shortwave::StorageStatus::write_failed;
    say(hooks.io, "RTL_SHORTWAVE_LOG change_failed action=%u index=%u error=%s\n",
        static_cast<unsigned>(action.kind), static_cast<unsigned>(index), error);
  }
}

void export_log(const Hooks& hooks) {
  char error[96]{};
  if (hooks.export_logs(hooks.state->logs, error, sizeof(error)))
    say(hooks.io, "RTL_SHORTWAVE_EXPORT ok logs=%u formats=csv,adif\n",
        static_cast<unsigned>(hooks.state->logs.size()));
  else
    say(hooks.io, "RTL_SHORTWAVE_EXPORT failed error=%s\n", error);
}

}  // namespace

bool handle(const orcsdr::shortwave::Action& action, const Hooks& hooks) {
  switch (action.kind) {
    case ActionKind::save_memory: save_memory(hooks); return true;
    case ActionKind::update_memory:
    case ActionKind::favorite_memory:
    case ActionKind::delete_memory: change_memory(action, hooks); return true;
    case ActionKind::save_log: save_log(hooks); return true;
    case ActionKind::update_log:
    case ActionKind::delete_log: change_log(action, hooks); return true;
    case ActionKind::export_log: export_log(hooks); return true;
    default: return false;
  }
}

}  // namespace orcsdr::shortwave_library_actions
