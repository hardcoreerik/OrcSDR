#include "serial_lab_vis.hpp"

#include <cstring>

namespace orcsdr::serial_lab_vis {

bool handle_command(const char* command, bool authenticated, const Hooks& hooks) {
  if (strncmp(command, "RTL_LAB", 7) == 0) {
    const bool read_only = strcmp(command, "RTL_LAB STATUS") == 0 ||
                           strcmp(command, "RTL_LAB SELF_CHECK") == 0 ||
                           strncmp(command, "RTL_LAB GET ", 12) == 0 ||
                           strcmp(command, "RTL_LAB RECIPE LIST") == 0 ||
                           strcmp(command, "RTL_LAB RECORDS LIST") == 0;
    if (!read_only && !authenticated) {
      hooks.write_line("RTL_LAB_ERROR auth_required");
      return true;
    }
    char response[512]{};
    if (!hooks.lab_command(command, response, sizeof(response))) {
      hooks.write_line("RTL_LAB_ERROR unknown_command");
      return true;
    }
    if (strcmp(response, "RTL_LAB_OPEN_REQUEST") == 0) {
      hooks.open_lab();
      hooks.write_line(hooks.lab_active() ? "RTL_LAB_OK" : "RTL_LAB_ERROR init_failed");
    } else {
      hooks.write_line(response);
    }
    return true;
  }

  if (strncmp(command, "RTL_VIS", 7) == 0) {
    const bool read_only = strcmp(command, "RTL_VIS STATUS") == 0 ||
                           strcmp(command, "RTL_VIS SELF_CHECK") == 0 ||
                           strncmp(command, "RTL_VIS GET ", 12) == 0 ||
                           strcmp(command, "RTL_VIS PRESET LIST") == 0;
    if (!read_only && !authenticated) {
      hooks.write_line("RTL_VIS_ERROR auth_required");
      return true;
    }
    char response[320]{};
    if (!hooks.vis_command(command, response, sizeof(response))) {
      hooks.write_line("RTL_VIS_ERROR unknown_command");
      return true;
    }
    if (strcmp(response, "RTL_VIS_OPEN_REQUEST") == 0) {
      hooks.open_visualizer();
      hooks.write_line(hooks.vis_active() ? "RTL_VIS_OK" : "RTL_VIS_ERROR source_unavailable");
    } else {
      hooks.write_line(response);
    }
    return true;
  }
  return false;
}

}  // namespace orcsdr::serial_lab_vis
