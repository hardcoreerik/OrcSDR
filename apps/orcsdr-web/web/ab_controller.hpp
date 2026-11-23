// Glue between OrcSDR's airband runtime (which already isolates its logic behind Hooks and a LiveState) and
// the page: the hooks record what the page must do (tune, filter, gain), and LiveState is filled from the
// page's radio inputs. Mirrors the airband parts of main.cpp.
#pragma once

#include <cstdint>

namespace abweb {

void begin(uint32_t now_ms);            // the Airband screen is being shown
void leave();
void set_num(const char* key, double value);
void set_gain_step(int index, int tenth_db);
void service(uint32_t now_ms);          // scanner and squelch tick
void update(uint32_t now_ms);           // redraw dynamic fields
void touch(int x, int y, uint32_t now_ms);
bool scope_edge_hit(int x, int y);
void scope_drag(int x, uint32_t now_ms);

// Outputs for the page.
uint32_t frequency_hz();
uint32_t filter_hz();
bool audio_open();                      // carrier squelch
uint32_t scope_span_hz();
uint32_t take_tune();                   // a pending retune in Hz, or 0
bool exit_requested();                  // the dashboard asked to go Home
// Gain actions queued by the dashboard: returns false when empty. kind: 1 gain_tenth_db, 2 tuner_agc, 3 rtl_agc.
bool take_gain(int* kind, int* value);
void reload_catalog(uint32_t now_ms);

}  // namespace abweb
