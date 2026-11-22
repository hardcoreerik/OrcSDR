#include "serial_adsb.hpp"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "adsb_dashboard.hpp"

namespace orcsdr::serial_adsb {

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
  if(strcmp(command,"RTL_ADSB_RANGE")==0) {
    const RangeStatus status = hooks.range_status();
    say(hooks, "RTL_ADSB_RANGE_STATUS range_nm=%u dashboard_range_nm=%u active=%u pending=%u\n",
        static_cast<unsigned>(status.range_nm),static_cast<unsigned>(status.dashboard_range_nm),
        static_cast<unsigned>(status.active),static_cast<unsigned>(status.pending));
    return true;
  }
  if(strncmp(command,"RTL_ADSB_RANGE ",15)==0) {
    if(!authenticated){hooks.write_line("RTL_ADSB_RANGE_ERROR auth_required");return true;}
    uint16_t range=0;
    if(!orcsdr::adsb::parse_map_range(command+15,&range)) {
      hooks.write_line("RTL_ADSB_RANGE_ERROR invalid_range");return true;
    }
    hooks.set_range(range);
    say(hooks, "RTL_ADSB_RANGE_OK range_nm=%u persistence=pending\n",static_cast<unsigned>(range));return true;
  }
  if (strncmp(command, "RTL_ADSB_LOCATION ", 18) == 0) {
    if (!authenticated) { hooks.write_line("RTL_ADSB_LOCATION_ERROR auth_required"); return true; }
    double latitude = 0, longitude = 0;
    char trailing = 0;
    if (sscanf(command + 18, "%lf %lf %c", &latitude, &longitude, &trailing) != 2 ||
        !std::isfinite(latitude) || !std::isfinite(longitude) ||
        latitude < -90.0 || latitude > 90.0 || longitude < -180.0 || longitude > 180.0) {
      hooks.write_line("RTL_ADSB_LOCATION_ERROR invalid_coordinate");
      return true;
    }
    const int32_t latitude_e7 = static_cast<int32_t>(std::llround(latitude * 10000000.0));
    const int32_t longitude_e7 = static_cast<int32_t>(std::llround(longitude * 10000000.0));
    if (!hooks.save_location(latitude_e7,longitude_e7)) {
      hooks.write_line("RTL_ADSB_LOCATION_ERROR storage"); return true;
    }
    hooks.refresh_if_active();
    hooks.write_line("RTL_ADSB_LOCATION_OK");
    return true;
  }
  if (strcmp(command, "RTL_ADSB_STOP") == 0 && hooks.adsb_band_selected()) {
    hooks.request_stop();
    hooks.write_line("RTL_ADSB_STOPPING");
    return true;
  }
  if (strcmp(command, "RTL_ADSB_START") == 0) {
    hooks.start();
    hooks.write_line("RTL_ADSB_STARTING");
    return true;
  }
  return false;
}

}  // namespace orcsdr::serial_adsb
