#pragma once
/* One definition per board, each in its own file under src/boards/. Adding a board means adding a file,
 * one line here, one line in orcsdr_profiles.c and one Kconfig choice entry; nothing else is board-specific. */
#include "orcsdr_board_profile.h"

extern const orcsdr_board_profile_t g_board_waveshare_p4_module_dev_kit;
extern const orcsdr_board_profile_t g_board_waveshare_p4_wifi6_poe_eth;
extern const orcsdr_board_profile_t g_board_m5stack_tab5;
