#include "serial_catalog.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace orcsdr::serial_catalog {

namespace {

// One formatted line, one write: what Serial.printf did for these replies (same 2048-byte buffer).
void say(const Hooks& hooks, const char* format, ...) __attribute__((format(printf, 2, 3)));
void say(const Hooks& hooks, const char* format, ...) {
  char line[2048];
  va_list args;
  va_start(args, format);
  const int length = vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  if (length > 0) hooks.write(line);
}

}  // namespace

bool handle_command(const char* command, bool authenticated, const Hooks& hooks) {
  if (strcmp(command, "RTL_CATALOG_STATUS") == 0 ||
      strcmp(command, "RTL_CATALOG_LIST") == 0) {
    const auto state = hooks.state();
    say(hooks, "RTL_CATALOG_STATUS ready=%d busy=%d operation=%u progress=%u date=%s message=\"%s\"\n",
        state.ready ? 1 : 0, state.busy ? 1 : 0,
        static_cast<unsigned>(state.operation),
        static_cast<unsigned>(state.progress_percent),
        state.catalog_date[0] ? state.catalog_date : "--", state.message);
    for (uint8_t i = 0; i < orcsdr::catalog::kPackCount; ++i) {
      const auto& pack = state.packs[i];
      if (pack.id[0] == '\0') continue;
      say(hooks, "RTL_CATALOG_PACK id=%s version=%s source_date=%s runtime_bytes=%lu archive_bytes=%lu installed=%d update=%d status=\"%s\"\n",
          pack.id, pack.version[0] ? pack.version : "--",
          pack.source_date[0] ? pack.source_date : "--",
          static_cast<unsigned long>(pack.runtime_bytes),
          static_cast<unsigned long>(pack.archive_bytes),
          pack.installed ? 1 : 0, pack.update_available ? 1 : 0, pack.status);
    }
    return true;
  }
  if (strcmp(command, "RTL_CATALOG_CHECK") == 0 ||
      strcmp(command, "RTL_CATALOG_FETCH") == 0) {
    if (!authenticated) { hooks.write_line("RTL_CATALOG_ERROR auth_required"); return true; }
    const bool fetch = strcmp(command, "RTL_CATALOG_FETCH") == 0;
    hooks.check();
    say(hooks, "RTL_CATALOG_%s_%s\n", fetch ? "FETCH" : "CHECK",
        hooks.state().busy ? "QUEUED" : "REJECTED");
    return true;
  }
  if (strncmp(command, "RTL_CATALOG_INSTALL ", 20) == 0 ||
      strncmp(command, "RTL_CATALOG_REMOVE ", 19) == 0) {
    if (!authenticated) { hooks.write_line("RTL_CATALOG_ERROR auth_required"); return true; }
    const bool remove = strncmp(command, "RTL_CATALOG_REMOVE ", 19) == 0;
    const char* argument = command + (remove ? 19 : 20);
    char id[20]{};
    char confirm[12]{};
    char trailing = 0;
    const int fields = sscanf(argument, "%19s %11s %c", id, confirm, &trailing);
    if ((remove && (fields != 2 || strcmp(confirm, "CONFIRM") != 0)) ||
        (!remove && fields != 1)) {
      hooks.write_line(remove ? "RTL_CATALOG_REMOVE_ERROR usage: RTL_CATALOG_REMOVE <id> CONFIRM"
                              : "RTL_CATALOG_INSTALL_ERROR usage: RTL_CATALOG_INSTALL <id>");
      return true;
    }
    const auto state = hooks.state();
    int index = -1;
    for (uint8_t i = 0; i < orcsdr::catalog::kPackCount; ++i)
      if (strcmp(state.packs[i].id, id) == 0) { index = i; break; }
    if (index < 0) {
      hooks.write_line("RTL_CATALOG_ERROR unknown_or_unchecked_pack");
      return true;
    }
    if (remove) hooks.remove(index); else hooks.install(index);
    say(hooks, "RTL_CATALOG_%s_%s\n", remove ? "REMOVE" : "INSTALL",
        hooks.state().busy ? "QUEUED" : "REJECTED");
    return true;
  }
  return false;
}

}  // namespace orcsdr::serial_catalog
