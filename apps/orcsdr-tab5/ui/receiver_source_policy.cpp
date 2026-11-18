#include "receiver_source_policy.hpp"

#include <cstring>

namespace orcsdr::source {
namespace {

bool usable(Active source, const Inputs& in) {
  switch (source) {
    case Active::local_usb: return in.usb_ready && in.mode != Mode::remote_tcp;
    case Active::remote_tcp:
      return in.remote_configured && in.network_up && in.mode != Mode::local_usb;
    default: return false;
  }
}

}  // namespace

Active choose(const Inputs& in) {
  if (in.active != Active::none && usable(in.active, in)) return in.active;
  switch (in.mode) {
    case Mode::local_usb: return in.usb_ready ? Active::local_usb : Active::none;
    case Mode::remote_tcp:
      return in.remote_configured && in.network_up ? Active::remote_tcp : Active::none;
    case Mode::automatic:
      if (in.usb_ready) return Active::local_usb;
      if (in.remote_configured && in.network_up && in.ms_since_boot >= in.grace_ms)
        return Active::remote_tcp;
      return Active::none;
  }
  return Active::none;
}

const char* mode_name(Mode mode) {
  switch (mode) {
    case Mode::local_usb: return "usb";
    case Mode::remote_tcp: return "remote";
    default: return "auto";
  }
}

bool parse_mode(const char* text, Mode* out) {
  if (text == nullptr || out == nullptr) return false;
  if (!strcmp(text, "auto") || !strcmp(text, "automatic")) *out = Mode::automatic;
  else if (!strcmp(text, "usb") || !strcmp(text, "local")) *out = Mode::local_usb;
  else if (!strcmp(text, "remote") || !strcmp(text, "tcp")) *out = Mode::remote_tcp;
  else return false;
  return true;
}

}  // namespace orcsdr::source
