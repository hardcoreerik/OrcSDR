#include "serial_ft8.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace orcsdr::serial_ft8 {

void handle(const char* args, bool authenticated, const Hooks& hooks) {
  while (*args == ' ') ++args;
  char verb[16]{};
  size_t v = 0;
  while (args[v] != '\0' && args[v] != ' ' && v + 1 < sizeof(verb)) {
    verb[v] = static_cast<char>(toupper(static_cast<unsigned char>(args[v])));
    ++v;
  }
  const char* rest = args + v;
  while (*rest == ' ') ++rest;

  if (verb[0] == '\0' || strcmp(verb, "HELP") == 0) {
    hooks.io.write_line("ORC_FT8_HELP queries: STATUS | DECODES [n] | BANDS | DUMP | TIME | CLOCK | HUNTSTATUS | HELP");
    hooks.io.write_line("ORC_FT8_HELP control (authenticated): OPEN | BAND <index|label|dial_hz> | MODE <FT8|FT4> | CLEAR | RUN <0|1> | "
                        "CONFIG <k> <gate> <fine_rows 4|8> <deadline_ms> | INJECT BEGIN|RUN|<offset> <b64> | LOG [ON|OFF|FLUSH] | SNR [OFFSET <dB>|RESET] | NTP | HUNT <FAST|DECODE|STOP> | TAB <name> | SHOT <name> | SNAP | SAVE <name> | ADIF <name> | DIAL <hz> | CLOCKTEST <ms>");
    return;
  }

  if (orcsdr::serial_ft8_query::handle(verb, rest, *hooks.query)) return;
  if (orcsdr::serial_ft8_data::handle(verb, rest, authenticated, *hooks.data)) return;
  if (orcsdr::serial_ft8_wwv::handle(verb, rest, authenticated, *hooks.wwv)) return;
  // ---- everything below changes state
  if (refuse_signed_out(hooks.io, verb, authenticated)) return;
  if (orcsdr::serial_ft8_control::handle(verb, rest, authenticated, *hooks.control)) return;

  say(hooks.io, "ORC_FT8_ERROR unknown_command %s (FT8 HELP)\n", verb);
}

}  // namespace orcsdr::serial_ft8
