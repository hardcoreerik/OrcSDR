#include "serial_sd.hpp"

#include <cstring>

namespace orcsdr::serial_sd {

bool handle_command(char* command, bool authenticated, const Hooks& hooks) {
  (void)authenticated;
  if (strcmp(command, "SD_LIST") == 0) {
    hooks.list();
    return true;
  }
  if (strncmp(command, "SD_GET_BEGIN ", 13) == 0) {
    hooks.get_begin(command + 13);
    return true;
  }
  if (strcmp(command, "SD_GET_CHUNK") == 0) {
    hooks.get_chunk();
    return true;
  }
  if (strcmp(command, "SD_GET_ABORT") == 0) {
    if (hooks.get_active()) hooks.get_abort("host_abort");
    else hooks.write_line("SD_GET_ABORTED");
    return true;
  }
  if (strncmp(command, "SD_REMOVE ", 10) == 0) {
    hooks.remove(command + 10);
    return true;
  }
  if (strncmp(command, "SD_PUT_BEGIN ", 13) == 0) {
    hooks.put_begin(command + 13);
    return true;
  }
  if (strncmp(command, "SD_PUT_CHUNK ", 13) == 0) {
    hooks.put_chunk(command + 13);
    return true;
  }
  if (strcmp(command, "SD_PUT_ABORT") == 0) {
    if (hooks.put_active()) {
      hooks.put_abort("host_abort");
    } else {
      hooks.write_line("SD_PUT_ABORTED");
    }
    return true;
  }
  return false;
}

}  // namespace orcsdr::serial_sd
