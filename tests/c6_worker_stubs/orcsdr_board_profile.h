#pragma once
#define CONFIG_ESP_HOSTED_HOST_SDIO_SLOT 0
struct orcsdr_board_profile_t {
  const char* name = nullptr;
  bool c6_rom_sdio_recovery = false;
  int c6_sdio_clk = 0, c6_sdio_cmd = 0, c6_sdio_d0 = 0;
  int c6_sdio_d1 = 0, c6_sdio_d2 = 0, c6_sdio_d3 = 0;
};
const orcsdr_board_profile_t* orcsdr_board_profile();
