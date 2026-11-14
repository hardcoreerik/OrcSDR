#include "serial_rec.hpp"

#include <cstdio>
#include <cstring>

namespace orcsdr::serial_rec {

bool handle_command(const char* command, bool authenticated, const Hooks& hooks) {
  (void)authenticated;   // none of these commands is signed-in only
  if (strcmp(command, "RTL_REC_START") == 0) {
    hooks.rec_start();
    return true;
  }
  if (strcmp(command, "RTL_REC_STOP") == 0) {
    hooks.rec_stop();
    return true;
  }
  if (strcmp(command, "RTL_REC_STATUS") == 0) {
    hooks.rec_status();
    return true;
  }
  if (strcmp(command, "RTL_REC_SAVE") == 0) {
    /* Re-export held PSRAM PCM after inserting an SD card. */
    hooks.rec_save();
    return true;
  }
  if (strncmp(command, "RTL_TOOL ", 9) == 0) {
    const char* name = command + 9;
    if (strcmp(name, "RADIO") == 0 || strcmp(name, "radio") == 0) {
      hooks.set_tool(Tool::radio);
    } else if (strcmp(name, "SCOPE") == 0 || strcmp(name, "scope") == 0) {
      hooks.set_tool(Tool::scope);
    } else if (strcmp(name, "CAPTURE") == 0 || strcmp(name, "capture") == 0) {
      hooks.set_tool(Tool::capture);
    } else {
      hooks.write_line("RTL_TOOL_INVALID use RADIO|SCOPE|CAPTURE");
    }
    return true;
  }
  if (strcmp(command, "RTL_TOOL") == 0) {
    char line[64];
    std::snprintf(line, sizeof(line), "RTL_TOOL_STATUS tool=%s\n", hooks.tool_name());
    hooks.write(line);
    return true;
  }
  return false;
}

}  // namespace orcsdr::serial_rec
