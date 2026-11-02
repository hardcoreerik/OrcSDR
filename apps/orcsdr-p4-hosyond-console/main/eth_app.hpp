#pragma once
// Wired Ethernet for boards that have an RMII PHY (Waveshare ESP32-P4-ETH, -POE-ETH, the Module-DEV-KIT). Opt-in: the driver is
// compiled in only with CONFIG_ORCSDR_ETHERNET=y, and it starts only if the board profile lists Ethernet pins. The pins, PHY address and
// reset line come from the board profile (components/orcsdr_board_hosyond/src/boards); nothing here names a board.
namespace eth_app {
struct Status {
  bool compiled = false;   // this build includes the driver
  bool started = false;    // the driver is installed and started
  bool link_up = false;    // cable plugged in and the PHY reports a link
  bool got_ip = false;     // DHCP lease obtained
  int speed_mbps = 0;
  bool full_duplex = false;
  char ip[16] = {};
  char gateway[16] = {};
  char mac[18] = {};
  char phy[16] = {};       // "ip101" or "generic" (the driver used)
  char error[40] = {};     // why it did not start, empty when it did
  unsigned link_changes = 0;
};
void start();     // safe to call on any board and build; does nothing unless enabled and configured
Status status();
}  // namespace eth_app
