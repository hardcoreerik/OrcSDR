#include "serial_ft8_query.hpp"

#include <cstdlib>
#include <cstring>

namespace orcsdr::serial_ft8_query {

using orcsdr::serial_ft8::say;
using orcsdr::serial_ft8::say_wide;

namespace {

#if defined(__GNUC__)
__attribute__((noinline))
#endif
void print_status(const Hooks& hooks) {
  Status s;
  hooks.status(&s);
  say_wide(hooks.io,
      "ORC_FT8_STATUS screen=%d runtime=%s headless=%d mode=%s band=%s dial_hz=%lu clock=%d rx_running=%d tap=%d rate_hz=%lu "
      "blocks=%llu ring=%llu avg_us=%lu max_us=%lu slots=%lu skipped=%lu last_decodes=%lu last_ms=%lu spectral_ms=%lu "
      "refine_ms=%lu gate_ms=%lu coarse=%u deadline_hit=%d slot_rms=%lu slot_peak=%lu slot_clipped=%lu dial_offset_hz=%d iq_dbfs=%.1f iq_clip_pct=%.3f gain_tenth_db=%d store=%u k=%u gate=%u fine_rows=%u deadline_ms=%lu\n",
      s.screen ? 1 : 0, s.runtime, s.headless ? 1 : 0, s.mode,
      s.band, s.dial_hz,
      s.clock ? 1 : 0,
      s.rx_running ? 1 : 0, s.tap ? 1 : 0,
      s.rate_hz, s.blocks,
      s.ring, s.avg_us,
      s.max_us, s.slots,
      s.skipped, s.last_decodes,
      s.last_ms, s.spectral_ms,
      s.refine_ms, s.gate_ms,
      s.coarse, s.deadline_hit ? 1 : 0, s.slot_rms,
      s.slot_peak, s.slot_clipped, s.dial_offset_hz,
      s.iq_dbfs, s.iq_clip_pct,
      s.gain_tenth_db, s.store,
      s.k, s.gate, s.fine_rows,
      s.deadline_ms);
}

}  // namespace

bool handle(const char* verb, const char* rest, const Hooks& hooks) {
  if (strcmp(verb, "STATUS") == 0) {
    print_status(hooks);
    return true;
  }

  if (strcmp(verb, "TIME") == 0) {
    // The wall clock the slot scheduler uses, to the millisecond, so a host can measure its error against a trusted clock.
    unsigned long long utc_ms = 0;
    bool valid = false;
    hooks.time(&utc_ms, &valid);
    say(hooks.io, "ORC_FT8_TIME utc_ms=%llu valid=%d\n", utc_ms, valid ? 1 : 0);
    return true;
  }

  if (strcmp(verb, "BANDS") == 0) {
    for (size_t i = 0; i < hooks.band_count(); ++i) {
      const char* label = "";
      unsigned long dial_hz = 0;
      if (hooks.band(i, &label, &dial_hz))
        say(hooks.io, "ORC_FT8_BAND index=%u label=%s dial_hz=%lu\n", static_cast<unsigned>(i), label, dial_hz);
    }
    hooks.io.write_line("ORC_FT8_BANDS_END");
    return true;
  }

  if (strcmp(verb, "DECODES") == 0) {
    hooks.drain_decodes();
    unsigned long limit = *rest != '\0' ? strtoul(rest, nullptr, 10) : 20ul;
    if (limit == 0) limit = 20;
    size_t printed = 0;
    for (size_t i = 0; i < hooks.decode_count() && printed < limit; ++i) {
      if (!hooks.print_decode(i)) break;
      ++printed;
    }
    say(hooks.io, "ORC_FT8_DECODES_END shown=%u total=%u\n", static_cast<unsigned>(printed),
        static_cast<unsigned>(hooks.decode_count()));
    return true;
  }

  if (strcmp(verb, "HUNTSTATUS") == 0) {
    HuntSummary summary;
    hooks.hunt_summary(&summary);
    say(hooks.io, "ORC_FT8_HUNTSTATUS mode=%s phase=%s current=%d best=%d slots_per_band=%u\n", summary.mode,
        summary.phase, summary.current, summary.best, summary.slots_per_band);
    for (size_t i = 0; i < hooks.band_count(); ++i) {
      HuntBand band;
      if (!hooks.hunt_band(i, &band)) continue;
      say(hooks.io, "ORC_FT8_HUNTBAND index=%u label=%s visited=%d evidence=%s slots=%u decodes=%u strong=%u peak_dbfs=%.1f\n",
          static_cast<unsigned>(i), band.label, band.visited ? 1 : 0, band.evidence, band.slots, band.decodes,
          band.strong, band.peak_dbfs);
    }
    hooks.io.write_line("ORC_FT8_HUNTSTATUS_END");
    return true;
  }

  if (strcmp(verb, "CLOCK") == 0) {
    // A query (no authentication): the clock's trust state and the decoded stations' view of it (median DT): positive dt_err_ms = our clock is fast.
    Clock c;
    hooks.clock(&c);
    say(hooks.io,
        "ORC_FT8_CLOCK level=%u age_s=%lu ntp_utc=%lu signal_utc=%lu wide=%d last_slot_wide=%d dt_valid=%d dt_err_ms=%d dt_stations=%u hold=%d\n",
        c.level, c.age_s, c.ntp_utc, c.signal_utc, c.wide ? 1 : 0, c.last_slot_wide ? 1 : 0, c.dt_valid ? 1 : 0,
        c.dt_err_ms, c.dt_stations, c.hold ? 1 : 0);
    return true;
  }

  return false;
}

}  // namespace orcsdr::serial_ft8_query
