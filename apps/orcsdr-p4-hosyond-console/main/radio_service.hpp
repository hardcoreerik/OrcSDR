#pragma once
// Receiver service: owns the esp-rtl-sdr driver and publishes bounded snapshots. It never
// draws and never waits for the UI; the UI timer pulls snapshots. Derived from the radio
// task of the Waveshare 4B app (recovered from closed PR #197, b7660c58) with the board
// code removed.
#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "home_v3_ui.hpp"

namespace orcsdr::radio {
bool start(QueueHandle_t commands);
waveshare4b::home_v3::Snapshot snapshot();
// Copies the latest 256-bin relative-intensity FFT; returns true if it is new since the last call.
bool take_bins(uint8_t out[256]);
}
