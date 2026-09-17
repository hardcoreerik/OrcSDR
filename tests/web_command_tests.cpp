#include "web_command.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace orcsdr::web_console;

int main() {
  Command command{};
  assert(parse_command("sound_toggle", command));
  assert(command.kind == CommandKind::sound_toggle);
  for (const char* bad : {"", "sound_toggle_extra", "volume_up=900", "open=unknown",
                          "open=", "tune=", "tune=-1", "tune=+99100000", "tune=1e8",
                          "tune=99100000junk", "tune=23999", "tune=1766000001",
                          "tune=4294967296", "tune=99999999999999999999999999"}) {
    assert(!parse_command(bad, command));
    assert(command.kind == CommandKind::none);
  }
  assert(!parse_command(std::string_view("sound_toggle\0junk", 17), command));
  assert(parse_command("tune=24000", command) && command.value == 24000);
  assert(parse_command("tune=1766000000", command) && command.value == 1766000000);
  assert(parse_command("open=shortwave", command));
  assert(command.kind == CommandKind::open && std::strcmp(command.id, "shortwave") == 0);

  CommandSlot slot;
  assert(!slot.submit({}));
  assert(slot.submit(command));
  Command next{};
  assert(parse_command("sound_toggle", next));
  assert(!slot.submit(next)); // Must not overwrite the accepted navigation request.
  Command received{};
  assert(slot.take(received));
  assert(received.kind == CommandKind::open && std::strcmp(received.id, "shortwave") == 0);
  assert(!slot.take(received));
  assert(slot.submit(next));
  assert(slot.take(received) && received.kind == CommandKind::sound_toggle);

  assert(same_origin("http://192.168.1.75", "192.168.1.75", "192.168.1.75"));
  assert(same_origin("http://orcsdr.local", "orcsdr.local", "192.168.1.75"));
  assert(!same_origin("http://evil.example", "evil.example", "192.168.1.75"));
  assert(!same_origin("http://orcsdr.local.evil", "orcsdr.local", "192.168.1.75"));
  assert(!same_origin("null", "orcsdr.local", "192.168.1.75"));
  assert(!same_origin("https://evil.example", "192.168.1.75", "192.168.1.75"));
  assert(!same_origin("http://", "", "192.168.1.75"));
  std::puts("WEB_COMMAND_OK");
}
