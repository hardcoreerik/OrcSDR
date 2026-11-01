#pragma once
// microSD slot on the Waveshare board (SDMMC slot 0, 4-bit; pins as the Tab5 reference uses, which this kit shares):
// CLK 43, CMD 44, D0-D3 39-42. GPIO 45 is the card power enable (active low). The Tab5's storage module
// (apps/orcsdr-tab5/ui/orcsdr_storage.cpp, unchanged) does the mounting and file access.
#include <cstdint>

namespace sd_card {
void start();  // background mount a few seconds after boot; absence of a card is not an error
struct Status {
  bool attempted = false;
  bool mounted = false;
  uint64_t total_bytes = 0;
};
Status status();
}  // namespace sd_card
