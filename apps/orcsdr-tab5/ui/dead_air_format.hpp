#pragma once
// Plain-text rendering of evidence-log rows for the Dead Air screens. Pure and host-tested. Nothing here invents data:
// a row shows only what the event record holds, and an unverified wall clock is shown as time since boot.
#include <cstddef>
#include <cstdint>

#include "dead_air_model.hpp"

namespace orcsdr::dead_air {

// "22:13:20" (UTC) when the wall clock was verified (unix_time != 0), otherwise "T+02:05" (time since boot).
void format_clock(uint32_t unix_time, uint32_t monotonic_ms, char* out, size_t capacity);

// One event-log row, for example "#12 22:13:20 RF RISE -2.3 dBFS +33 dB" or "#3 T+02:05 MARK 97.289 MHz".
void format_event_row(const Event& event, char* out, size_t capacity);

// "42s", "5m 09s", "1h 05m": a duration for the session card and the control run.
void format_duration(uint32_t seconds, char* out, size_t capacity);

// Vertical range of the Overview RF strip. It fits every valid sample plus, when `lines` is set, the baseline and
// limit lines, with a little padding, and is never narrower than `min_span_db` so noise does not look like a trend.
struct StripScale { float lo = -100.0f, hi = 0.0f; };
StripScale strip_scale(const int16_t* tenths, size_t count, bool lines, float baseline_dbfs, float limit_dbfs,
                       float min_span_db = 20.0f);
// Pixel row for a level inside a strip `height` rows tall starting at `top`; clamped to the strip.
int strip_y(float dbfs, const StripScale& scale, int top, int height);

}  // namespace orcsdr::dead_air
