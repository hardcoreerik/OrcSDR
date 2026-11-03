#pragma once

// Host stand-in for ESP-IDF's esp_attr.h: the firmware places large zero-initialised FT8 buffers in PSRAM with EXT_RAM_BSS_ATTR;
// on the host there is no PSRAM, so the attribute is empty.
#define EXT_RAM_BSS_ATTR
