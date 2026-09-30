#pragma once

#include <cstdint>

#include "keyboard_input.hpp"

// Driver for the M5Stack Tab5 Keyboard (SKU A164): 70 keys behind an STM32F030 that speaks I2C
// at 0x6D on Ext.Port1 (SDA GPIO0, SCL GPIO1, INT GPIO50). The keyboard is run in its
// character mode, where the firmware resolves Aa/Sym/Ctrl/Alt itself and reports each key as a
// modifier byte plus a name string. Everything is polled from the UI loop; there is no task.
namespace orcsdr::tab5_keyboard {

struct Status {
  bool present = false;
  uint8_t firmware = 0;
  uint8_t mode = 0;         // 2 = character mode
  bool int_low = false;     // INT asserted (events pending)
  uint32_t keys = 0;        // decoded key events delivered
  uint32_t dropped = 0;     // decoded but the queue was full
  uint32_t undecoded = 0;   // events with an unknown name
  uint32_t bus_errors = 0;  // failed I2C transfers
  uint32_t attach_count = 0;
};

// Creates the I2C bus and probes once. Safe to call when no keyboard is connected.
void begin();

// Polls the keyboard (and re-probes at a low rate while it is absent). Call every UI loop pass.
void service(uint32_t now_ms);

// Oldest queued key, if any.
bool pop(keyboard_input::Key* key);

Status status();

}  // namespace orcsdr::tab5_keyboard
