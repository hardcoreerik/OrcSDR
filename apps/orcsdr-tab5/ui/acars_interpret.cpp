// SPDX-License-Identifier: AGPL-3.0-only
#include "acars_interpret.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace orcsdr::acars_ui {
namespace {

#if defined(__GNUC__)
#define ACARS_PRINTF(a, b) __attribute__((format(printf, a, b)))
#else
#define ACARS_PRINTF(a, b)
#endif

bool is_digit(char c) { return c >= '0' && c <= '9'; }
bool is_upper(char c) { return c >= 'A' && c <= 'Z'; }

bool all_digits(const char* p, size_t n) {
  for (size_t i = 0; i < n; ++i)
    if (!is_digit(p[i])) return false;
  return n > 0;
}
unsigned dec(const char* p, size_t n) {
  unsigned v = 0;
  for (size_t i = 0; i < n; ++i) v = v * 10u + static_cast<unsigned>(p[i] - '0');
  return v;
}

// Bounded view of the block text; every read past the end yields NUL.
struct Text {
  const char* p;
  size_t n;
  char at(size_t i) const { return i < n ? p[i] : '\0'; }
  bool has(size_t i, const char* lit) const {
    const size_t len = std::strlen(lit);
    return i + len <= n && std::memcmp(p + i, lit, len) == 0;
  }
};

void add_line(Interpretation& r, const char* fmt, ...) ACARS_PRINTF(2, 3);
void add_line(Interpretation& r, const char* fmt, ...) {
  if (r.line_count >= kInterpLines) return;
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(r.lines[r.line_count], kInterpLineChars, fmt, ap);
  va_end(ap);
  ++r.line_count;
}
void set_headline(Interpretation& r, const char* s) { std::snprintf(r.headline, sizeof(r.headline), "%s", s); }
void set_summary(Interpretation& r, const char* fmt, ...) ACARS_PRINTF(2, 3);
void set_summary(Interpretation& r, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(r.summary, sizeof(r.summary), fmt, ap);
  va_end(ap);
}

void fmt_hms(char* out, size_t cap, const char* six) {   // "202750" -> "20:27:50Z"
  std::snprintf(out, cap, "%c%c:%c%c:%c%cZ", six[0], six[1], six[2], six[3], six[4], six[5]);
}
bool valid_date(unsigned year, unsigned month, unsigned day) {
  static const unsigned kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12 || day < 1) return false;
  const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  return day <= kDays[month - 1] + (month == 2 && leap ? 1u : 0u);
}
bool valid_hms(const char* six) {
  return dec(six, 2) < 24 && dec(six + 2, 2) < 60 && dec(six + 4, 2) < 60;
}

// Latitude / longitude as signed-hemisphere decimal degrees with three decimals, e.g. "44.662N".
void fmt_coord(char* out, size_t cap, char hemi, unsigned milli_deg) {
  std::snprintf(out, cap, "%u.%03u%c", milli_deg / 1000u, milli_deg % 1000u, hemi);
}
// Degrees plus minutes with one decimal ("44397" = 44 deg 39.7 min). `deg_digits` is 2 for latitude, 3 for longitude.
bool parse_degmin(const char* p, size_t deg_digits, unsigned* milli_deg) {
  if (!all_digits(p, deg_digits + 3)) return false;
  const unsigned deg = dec(p, deg_digits);
  const unsigned tenths = dec(p + deg_digits, 3);   // minutes * 10
  if (tenths >= 600) return false;
  *milli_deg = deg * 1000u + (tenths * 5u + 1u) / 3u;
  return *milli_deg <= (deg_digits == 2 ? 90000u : 180000u);
}
// Plain decimal degrees ("42.872", "044.6863") truncated to thousandths; sets *negative for a leading '-'.
bool parse_decimal(const char* p, size_t n, unsigned* milli_deg, bool* negative) {
  size_t i = 0;
  *negative = false;
  if (i < n && p[i] == '-') { *negative = true; ++i; }
  unsigned whole = 0;
  size_t d = 0;
  while (i < n && is_digit(p[i])) { whole = whole * 10u + static_cast<unsigned>(p[i] - '0'); ++i; ++d; }
  if (d == 0 || d > 3) return false;
  unsigned frac = 0;
  size_t fd = 0;
  if (i < n && p[i] == '.') {
    ++i;
    while (i < n && is_digit(p[i])) {
      if (fd < 3) { frac = frac * 10u + static_cast<unsigned>(p[i] - '0'); ++fd; }
      ++i;
    }
  }
  while (fd < 3) { frac *= 10u; ++fd; }
  if (i != n) return false;
  *milli_deg = whole * 1000u + frac;
  return true;
}

void fmt_alt(char* out, size_t cap, unsigned hundreds) {   // altitude in hundreds of feet
  if (hundreds >= 180u) std::snprintf(out, cap, "FL%03u", hundreds);
  else std::snprintf(out, cap, "%u ft", hundreds * 100u);
}
void fmt_alt_ft(char* out, size_t cap, unsigned feet) {
  if (feet >= 18000u) std::snprintf(out, cap, "FL%03u", (feet + 50u) / 100u);
  else std::snprintf(out, cap, "%u ft", feet);
}
// "M57" -> -57, "P12" -> 12, "-59" -> -59, "-58.0" -> -58 (tenths dropped).
bool parse_temp(const char* p, size_t n, int* celsius) {
  if (n < 2) return false;
  bool neg = false;
  size_t i = 0;
  if (p[0] == 'M' || p[0] == '-') { neg = true; i = 1; }
  else if (p[0] == 'P' || p[0] == '+') i = 1;
  size_t d = 0;
  int v = 0;
  while (i < n && is_digit(p[i])) { v = v * 10 + (p[i] - '0'); ++i; ++d; }
  if (d == 0 || d > 3) return false;
  if (i < n) {
    if (p[i] != '.') return false;
    for (++i; i < n; ++i)
      if (!is_digit(p[i])) return false;
  }
  *celsius = neg ? -v : v;
  return true;
}

// ---- tables ---------------------------------------------------------------------------------------------------
struct LabelRow { char a, b; const char* name; };
// Names come from ARINC 620 Table C-1 where the row is unambiguous, otherwise from the community label research. Our own words.
constexpr LabelRow kLabels[] = {
    {'_', '?', "General response (acknowledgement)"},
    {'_', 'd', "Link ping (demand mode)"},
    {'Q', '0', "Link test"},
    {'S', 'Q', "Ground station squitter"},
    {':', ';', "Radio autotune (frequency change)"},
    {':', '}', "Retune to VDL"},
    {'H', '1', "Message to/from an on-board unit"},
    {'5', 'Z', "Airline-defined downlink"},
    {'5', 'R', "Crew-initiated position report"},
    {'5', '7', "Crew-initiated position report"},
    {'5', 'U', "Weather request"},
    {'5', 'D', "ATIS request"},
    {'5', 'V', "VDL switch advisory"},
    {'5', 'Y', "ETA revision"},
    {'5', '1', "Ground time request"},
    {'5', '4', "Voice go-ahead / frequency uplink"},
    {'7', 'A', "Engine data / takeoff thrust report"},
    {'7', 'B', "Crew-entered miscellaneous message"},
    {'A', '0', "ATS facility notification (AFN)"},
    {'B', '0', "ATS facility notification (AFN)"},
    {'A', '1', "Oceanic clearance"},
    {'A', '3', "Departure clearance"},
    {'A', '9', "ATIS report"},
    {'B', '1', "Oceanic clearance request"},
    {'B', '2', "Oceanic clearance readback"},
    {'B', '3', "Incomplete message advisory"},
    {'B', '4', "Departure clearance readback"},
    {'B', '6', "ADS report request"},
    {'B', '7', "Free text to ATC"},
    {'B', '8', "Departure slot request"},
    {'B', 'A', "ATC communication"},
    {'B', 'B', "Terminal weather for pilots"},
    {'C', '1', "Cockpit/cabin printer message"},
    {'E', '1', "Internet e-mail message"},
    {'H', 'X', "Undelivered uplink report"},
    {'M', 'A', "Media advisory (link status)"},
    {'Q', '1', "Departure/arrival report"},
    {'Q', '2', "ETA report"},
    {'Q', '3', "Clock update advisory"},
    {'Q', 'A', "OUT report (left the gate)"},
    {'Q', 'B', "OFF report (took off)"},
    {'Q', 'C', "ON report (landed)"},
    {'Q', 'D', "IN report (at the gate)"},
    {'Q', 'E', "OUT report with destination"},
    {'Q', 'F', "OFF report with destination"},
    {'Q', 'K', "Landing report"},
    {'Q', 'L', "Arrival report"},
    {'Q', 'M', "Arrival information report"},
    {'Q', 'N', "Diversion report"},
    {'R', 'A', "Command/response uplink"},
    {'R', 'B', "Command/response downlink"},
    {'S', '1', "VHF network statistics"},
    {'S', '3', "LRU configuration report"},
    {'2', '1', "Position report"},
};

struct NameRow { char a, b; const char* name; };
// H1 sub-labels: which on-board unit a message came from or is for (ARINC 620 Table C-1).
constexpr NameRow kSublabels[] = {
    {'D', 'F', "flight data recorder unit (DFDAU)"},
    {'M', '1', "left FMC"},
    {'M', '2', "right FMC"},
    {'M', '3', "centre FMC"},
    {'M', 'D', "selected FMC"},
    {'C', 'F', "central fault display"},
    {'E', 'I', "engine indicating system"},
    {'W', 'O', "weather observation"},
    {'H', '1', "left HF data radio"},
    {'H', '2', "right HF data radio"},
    {'T', '8', "vendor-defined unit"},
};

struct AirlineRow { char a, b; const char* name; };
constexpr AirlineRow kAirlines[] = {
    {'A', 'S', "Alaska"},     {'F', '9', "Frontier"},   {'W', 'N', "Southwest"}, {'D', 'L', "Delta"},
    {'A', 'A', "American"},   {'U', 'A', "United"},     {'B', '6', "JetBlue"},   {'N', 'K', "Spirit"},
    {'H', 'A', "Hawaiian"},   {'G', '4', "Allegiant"},  {'Q', 'X', "Horizon"},   {'O', 'O', "SkyWest"},
    {'A', 'C', "Air Canada"}, {'F', 'X', "FedEx"},      {'5', 'X', "UPS"},       {'Y', 'X', "Republic"},
    {'M', 'Q', "Envoy"},      {'O', 'H', "PSA"},        {'S', 'Y', "Sun Country"},
};

const char* sublabel_name(char a, char b) {
  for (const auto& r : kSublabels)
    if (r.a == a && r.b == b) return r.name;
  return nullptr;
}

// ---- common pieces ------------------------------------------------------------------------------------------------
// ARINC 620 downlink text starts with a four character message sequence number (originator, 2 digit number, block letter)
// and a six character flight identifier.
struct Header {
  bool ok = false;
  char msn[5]{};
  char flight[7]{};
};
Header parse_header(const MessageRecord& m, const Text& t) {
  Header h;
  if (t.n < 10) return h;
  if (!is_upper(t.at(0)) || !all_digits(t.p + 1, 2) || !is_upper(t.at(3))) return h;
  for (size_t i = 4; i < 10; ++i) {
    const char c = t.at(i);
    if (!(is_upper(c) || is_digit(c) || c == ' ')) return h;
  }
  std::memcpy(h.msn, t.p, 4);
  std::memcpy(h.flight, t.p + 4, 6);
  size_t n = 6;
  while (n > 0 && h.flight[n - 1] == ' ') h.flight[--n] = '\0';
  (void)m;
  h.ok = true;
  return h;
}

void add_header_line(Interpretation& r, const Header& h) {
  if (!h.ok) return;
  const char* airline = airline_name(h.flight);
  const char part = h.msn[3];
  if (airline) add_line(r, "Message %.3s part %c   Flight %s (%s)", h.msn, part, h.flight, airline);
  else add_line(r, "Message %.3s part %c   Flight %s", h.msn, part, h.flight);
}

bool block_is_uplink(char block_id) { return block_id >= 'A' && block_id <= 'Z'; }

// ---- label parsers ------------------------------------------------------------------------------------------------
bool interpret_response(const MessageRecord& m, const Text& t, Interpretation& r) {
  const bool up = block_is_uplink(m.block_id);
  set_headline(r, up ? "GROUND ACKNOWLEDGEMENT" : "AIRCRAFT ACKNOWLEDGEMENT");
  // The acknowledgement character names the block being confirmed. NAK (and any unprintable byte, shown as '?') means none.
  const char a = m.acknowledgement;
  if (is_digit(a)) add_line(r, "Confirms downlink block %c was received.", a);
  else if (is_upper(a)) add_line(r, "Confirms uplink block %c was received.", a);
  else add_line(r, "Technical response, no block named.");
  const Header h = parse_header(m, t);
  if (h.ok) add_line(r, "Message %.4s   Flight %s", h.msn, h.flight);
  if (is_digit(a) || is_upper(a)) set_summary(r, "ACK of block %c%s%s", a, h.ok ? " " : "", h.ok ? h.flight : "");
  else set_summary(r, "Technical response%s%s", h.ok ? " " : "", h.ok ? h.flight : "");
  return true;
}

bool interpret_autotune(const Text& t, Interpretation& r) {
  if (t.n < 6 || !all_digits(t.p, 6)) return false;
  const unsigned khz = dec(t.p, 6);
  set_headline(r, "RADIO AUTOTUNE");
  add_line(r, "Tune the radio to %u.%03u MHz.", khz / 1000u, khz % 1000u);
  if (t.n >= 10 && all_digits(t.p + 6, 4)) add_line(r, "For %u seconds.", dec(t.p + 6, 4));
  set_summary(r, "Retune to %u.%03u MHz", khz / 1000u, khz % 1000u);
  return true;
}

// ARINC 620 uplink squitter: versions 0, 1 and 2.
bool interpret_squitter(const Text& t, Interpretation& r) {
  if (t.n < 4 || !all_digits(t.p, 2)) return false;
  const unsigned version = dec(t.p, 2);
  if (version > 2) return false;
  const char p0 = t.at(2), p1 = t.at(3);
  const char* provider = (p0 == 'X' && p1 == 'A') ? "ARINC" : (p0 == 'X' && p1 == 'S') ? "SITA" : nullptr;
  set_headline(r, "GROUND STATION SQUITTER");
  char who[48];
  if (provider) std::snprintf(who, sizeof(who), "%s", provider);
  else std::snprintf(who, sizeof(who), "provider %c%c", p0, p1);
  if (version == 0) {
    add_line(r, "Version 0, %s. Station not identified.", who);
    set_summary(r, "Squitter (%s)", who);
    return true;
  }
  if (t.n < 12 || !is_upper(t.at(4)) || !is_upper(t.at(5)) || !is_upper(t.at(6))) return false;
  char iata[4], icao[5];
  std::memcpy(iata, t.p + 4, 3);
  iata[3] = '\0';
  std::memcpy(icao, t.p + 7, 4);
  icao[4] = '\0';
  const char number = t.at(11);
  add_line(r, "Ground station %s (%s) number %c, %s, version %u.", iata, icao, number, who, version);
  char where[40] = "";
  size_t pos = 12;
  if (version == 2 && all_digits(t.p + pos, 4) && (t.at(pos + 4) == 'N' || t.at(pos + 4) == 'S') &&
      all_digits(t.p + pos + 5, 5) && (t.at(pos + 10) == 'E' || t.at(pos + 10) == 'W')) {
    // Latitude ddmm + N/S, longitude dddmm + E/W, whole minutes.
    const unsigned latd = dec(t.p + pos, 2), latm = dec(t.p + pos + 2, 2);
    const unsigned lond = dec(t.p + pos + 5, 3), lonm = dec(t.p + pos + 8, 2);
    const unsigned lat_milli = latd * 1000u + (latm * 1000u + 30u) / 60u, lon_milli = lond * 1000u + (lonm * 1000u + 30u) / 60u;
    if (latm < 60 && lonm < 60 && lat_milli <= 90000u && lon_milli <= 180000u) {   // a position that cannot exist is not shown
      char lat[16], lon[16];
      fmt_coord(lat, sizeof(lat), t.at(pos + 4), lat_milli);
      fmt_coord(lon, sizeof(lon), t.at(pos + 10), lon_milli);
      std::snprintf(where, sizeof(where), "%s %s", lat, lon);
      add_line(r, "Located at %s.", where);
    }
    pos += 11;
  }
  // Optional service availability: flag (V/A/B) + six digit kHz, then optional ",address" lists, "-" between services, "/" ends.
  char services[40] = "";
  while (pos + 7 <= t.n) {
    const char f = t.at(pos);
    if ((f == 'V' || f == 'A' || f == 'B') && all_digits(t.p + pos + 1, 6)) {
      const unsigned khz = dec(t.p + pos + 1, 6);
      const char* kind = f == 'V' ? "VDL2" : f == 'A' ? "VDL2 ATN" : "VDL2 AOA+ATN";
      char one[28];
      std::snprintf(one, sizeof(one), "%s%s %u.%03u", services[0] ? ", " : "", kind, khz / 1000u, khz % 1000u);
      if (std::strlen(services) + std::strlen(one) < sizeof(services)) std::strcat(services, one);
      pos += 7;
      while (pos < t.n && (t.at(pos) == ',' || t.at(pos) == '-')) {
        if (t.at(pos) == '-') { ++pos; break; }
        pos += 8;   // ",52ABCDE": comma plus a 7 character ground station address
      }
    } else {
      break;
    }
  }
  if (services[0]) add_line(r, "Offers %s MHz.", services);
  // Free text after "/".
  while (pos < t.n && t.at(pos) != '/') ++pos;
  if (pos + 1 < t.n) {
    char free_text[40];
    size_t k = 0;
    for (size_t i = pos + 1; i < t.n && k + 1 < sizeof(free_text); ++i) free_text[k++] = t.at(i) >= 0x20 && t.at(i) < 0x7f ? t.at(i) : '.';
    free_text[k] = '\0';
    add_line(r, "Note: %s", free_text);
  }
  set_summary(r, "Squitter %s %s%s%s%s", iata, who, where[0] ? " " : "", where, services[0] ? " VDL" : "");
  if (services[0]) set_summary(r, "Squitter %s %s %s", iata, where, services);
  return true;
}

// FMC position report: "POS" + latitude/longitude as hemisphere + degrees + minutes with one decimal, then comma separated:
// waypoint, time, altitude (hundreds of feet), next waypoint, its ETA, following waypoint, temperature, wind.
bool interpret_fmc_position(const Text& t, size_t at, Interpretation& r) {
  if (!t.has(at, "POS")) return false;
  size_t p = at + 3;
  const char ns = t.at(p), ew = t.at(p + 6);
  if (!((ns == 'N' || ns == 'S') && (ew == 'E' || ew == 'W'))) return false;
  unsigned lat_m = 0, lon_m = 0;
  if (!parse_degmin(t.p + p + 1, 2, &lat_m) || !parse_degmin(t.p + p + 7, 3, &lon_m)) return false;
  p += 13;
  // Split the rest on commas (at most 14 fields).
  struct F { const char* s; size_t n; } f[14];
  size_t nf = 0;
  size_t i = p;
  while (i < t.n && nf < 14) {
    if (t.at(i) != ',') break;
    ++i;
    size_t s = i;
    while (i < t.n && t.at(i) != ',' && t.at(i) != '/') ++i;
    f[nf++] = {t.p + s, i - s};
    if (i < t.n && t.at(i) == '/') break;
  }
  char lat[16], lon[16], alt[16] = "", at_wp[16], hms[12];
  fmt_coord(lat, sizeof(lat), ns, lat_m);
  fmt_coord(lon, sizeof(lon), ew, lon_m);
  set_headline(r, "FMC POSITION REPORT");
  // Fields after the position: 0 waypoint, 1 time, 2 altitude (hundreds of feet), 3 next waypoint, 4 its ETA, 5 the one after,
  // 6 temperature, 7 wind.
  if (nf >= 3 && all_digits(f[2].s, f[2].n) && f[2].n <= 3) fmt_alt(alt, sizeof(alt), dec(f[2].s, f[2].n));
  add_line(r, "Position %s %s%s%s", lat, lon, alt[0] ? "   Altitude " : "", alt);
  char over[40] = "";
  if (nf >= 2 && f[0].n >= 2 && f[0].n <= 5 && f[1].n == 6 && all_digits(f[1].s, 6) && valid_hms(f[1].s)) {
    fmt_hms(hms, sizeof(hms), f[1].s);
    std::snprintf(over, sizeof(over), "Over %.*s at %s", static_cast<int>(f[0].n), f[0].s, hms);
  } else if (nf >= 1 && f[0].n >= 2 && f[0].n <= 5) {
    std::snprintf(over, sizeof(over), "Over %.*s", static_cast<int>(f[0].n), f[0].s);
  }
  if (over[0]) {
    if (nf >= 5 && f[3].n >= 2 && f[3].n <= 5 && f[4].n == 6 && all_digits(f[4].s, 6) && valid_hms(f[4].s)) {
      fmt_hms(at_wp, sizeof(at_wp), f[4].s);
      add_line(r, "%s, next %.*s ETA %s", over, static_cast<int>(f[3].n), f[3].s, at_wp);
    } else {
      add_line(r, "%s", over);
    }
  }
  int temp = 0;
  char tbuf[16] = "", wbuf[28] = "";
  if (nf >= 7 && parse_temp(f[6].s, f[6].n, &temp)) std::snprintf(tbuf, sizeof(tbuf), "%dC", temp);
  if (nf >= 8 && f[7].n >= 5 && f[7].n <= 6 && all_digits(f[7].s, f[7].n)) {
    const unsigned dir = dec(f[7].s, 3), spd = dec(f[7].s + 3, f[7].n - 3);
    if (dir <= 360) std::snprintf(wbuf, sizeof(wbuf), "wind %03u/%u kt", dir, spd);
  }
  if (tbuf[0] || wbuf[0]) add_line(r, "Outside air %s   %s", tbuf[0] ? tbuf : "?", wbuf[0] ? wbuf : "");
  set_summary(r, "%s %s %s %s %s", lat, lon, alt, tbuf, wbuf);
  return true;
}

// Frontier-style label 21 position report: "POSN 42.872W120.685, 102,202236,36975,26701, 50,-59,214330,KLAS".
bool interpret_label21(const Text& t, size_t at, Interpretation& r) {
  if (!t.has(at, "POS")) return false;
  size_t p = at + 3;
  const char ns = t.at(p);
  if (ns != 'N' && ns != 'S') return false;
  ++p;
  while (t.at(p) == ' ') ++p;
  size_t s = p;
  while (is_digit(t.at(p)) || t.at(p) == '.') ++p;
  unsigned lat_m = 0, lon_m = 0;
  bool neg = false;
  if (!parse_decimal(t.p + s, p - s, &lat_m, &neg)) return false;
  const char ew = t.at(p);
  if (ew != 'E' && ew != 'W') return false;
  ++p;
  s = p;
  while (is_digit(t.at(p)) || t.at(p) == '.') ++p;
  if (!parse_decimal(t.p + s, p - s, &lon_m, &neg)) return false;
  if (lat_m > 90000u || lon_m > 180000u) return false;
  struct F { const char* s; size_t n; } f[10];
  size_t nf = 0;
  while (p < t.n && t.at(p) == ',' && nf < 10) {
    ++p;
    while (t.at(p) == ' ') ++p;
    const size_t b = p;
    while (p < t.n && t.at(p) != ',') ++p;
    f[nf++] = {t.p + b, p - b};
  }
  char lat[16], lon[16], alt[16] = "", hms[12], eta[12] = "";
  fmt_coord(lat, sizeof(lat), ns, lat_m);
  fmt_coord(lon, sizeof(lon), ew, lon_m);
  set_headline(r, "POSITION REPORT");
  if (nf >= 3 && all_digits(f[2].s, f[2].n) && f[2].n >= 4 && f[2].n <= 5) fmt_alt_ft(alt, sizeof(alt), dec(f[2].s, f[2].n));
  add_line(r, "Position %s %s%s%s", lat, lon, alt[0] ? "   Altitude " : "", alt);
  if (nf >= 2 && f[1].n == 6 && all_digits(f[1].s, 6) && valid_hms(f[1].s)) {
    fmt_hms(hms, sizeof(hms), f[1].s);
    add_line(r, "Reported at %s", hms);
  }
  int temp = 0;
  char tbuf[16] = "";
  if (nf >= 6 && parse_temp(f[5].s, f[5].n, &temp)) std::snprintf(tbuf, sizeof(tbuf), "%dC", temp);
  const bool have_dest = nf >= 8 && f[7].n == 4 && is_upper(f[7].s[0]) && is_upper(f[7].s[1]) && is_upper(f[7].s[2]) && is_upper(f[7].s[3]);
  if (nf >= 7 && f[6].n == 6 && all_digits(f[6].s, 6) && valid_hms(f[6].s)) fmt_hms(eta, sizeof(eta), f[6].s);
  if (tbuf[0] || eta[0] || have_dest) {
    char line[kInterpLineChars];
    int k = 0;
    if (tbuf[0]) k += std::snprintf(line + k, sizeof(line) - static_cast<size_t>(k), "Outside air %s   ", tbuf);
    if (eta[0] || have_dest)
      k += std::snprintf(line + k, sizeof(line) - static_cast<size_t>(k), "ETA %s%s%.*s", eta[0] ? eta : "?", have_dest ? " at " : "",
                         have_dest ? 4 : 0, have_dest ? f[7].s : "");
    add_line(r, "%s", line);
  }
  set_summary(r, "%s %s %s %s", lat, lon, alt, tbuf);
  return true;
}

// Flight data recorder unit (#DF) text: cruise records "N43.0955.W122.0090.282014.#39999.-59.2.267.056.CR#" (decimal degrees,
// day+time, altitude in feet, temperature, wind), a "TRP#time#lat#lon#level#..." block, and a "/CCN" header with the route.
struct DfPos {
  bool ok = false;
  char ns = 'N', ew = 'W';
  unsigned lat_m = 0, lon_m = 0;
  unsigned day = 0;
  char hm[5]{};
  unsigned alt_ft = 0;
  int temp = 0;
  unsigned wdir = 0, wspd = 0;
  char phase[3]{};
};
bool match_df_record(const Text& t, size_t i, DfPos* out, size_t* end) {
  // N dd.dddd . E/W ddd.dddd . dddddd . # ddddd . -dd.d . ddd . ddd . XX #
  if (!(t.at(i) == 'N' || t.at(i) == 'S')) return false;
  size_t p = i + 1;
  if (!(all_digits(t.p + p, 2) && t.at(p + 2) == '.' && all_digits(t.p + p + 3, 4) && t.at(p + 7) == '.')) return false;
  unsigned lat_m = 0, lon_m = 0;
  bool neg = false;
  if (!parse_decimal(t.p + p, 7, &lat_m, &neg)) return false;
  p += 8;
  if (!(t.at(p) == 'E' || t.at(p) == 'W')) return false;
  const char ew = t.at(p);
  ++p;
  if (!(all_digits(t.p + p, 3) && t.at(p + 3) == '.' && all_digits(t.p + p + 4, 4) && t.at(p + 8) == '.')) return false;
  if (!parse_decimal(t.p + p, 8, &lon_m, &neg)) return false;
  p += 9;
  if (!(all_digits(t.p + p, 6) && t.at(p + 6) == '.' && t.at(p + 7) == '#')) return false;
  const unsigned day = dec(t.p + p, 2), hh = dec(t.p + p + 2, 2), mm = dec(t.p + p + 4, 2);
  if (day < 1 || day > 31 || hh > 23 || mm > 59) return false;
  p += 8;
  if (!all_digits(t.p + p, 5) || t.at(p + 5) != '.') return false;
  const unsigned alt = dec(t.p + p, 5);
  p += 6;
  const size_t ts = p;
  if (t.at(p) == '-') ++p;
  while (is_digit(t.at(p))) ++p;
  if (t.at(p) != '.') return false;
  ++p;
  if (!is_digit(t.at(p))) return false;
  ++p;
  int temp = 0;
  if (!parse_temp(t.p + ts, p - ts, &temp)) return false;
  if (t.at(p) != '.') return false;
  ++p;
  if (!(all_digits(t.p + p, 3) && t.at(p + 3) == '.' && all_digits(t.p + p + 4, 3) && t.at(p + 7) == '.')) return false;
  const unsigned wd = dec(t.p + p, 3), ws = dec(t.p + p + 4, 3);
  if (wd > 360) return false;
  p += 8;
  if (!(is_upper(t.at(p)) && is_upper(t.at(p + 1)) && t.at(p + 2) == '#')) return false;
  out->ok = true;
  out->ns = t.at(i);
  out->ew = ew;
  out->lat_m = lat_m;
  out->lon_m = lon_m;
  out->day = day;
  std::snprintf(out->hm, sizeof(out->hm), "%02u%02u", hh, mm);
  out->alt_ft = alt;
  out->temp = temp;
  out->wdir = wd;
  out->wspd = ws;
  out->phase[0] = t.at(p);
  out->phase[1] = t.at(p + 1);
  out->phase[2] = '\0';
  *end = p + 3;
  return true;
}

const char* phase_name(const char* ph) {
  if (std::strcmp(ph, "CR") == 0) return "cruise";
  if (std::strcmp(ph, "CL") == 0) return "climb";
  if (std::strcmp(ph, "DE") == 0 || std::strcmp(ph, "DS") == 0) return "descent";
  return nullptr;
}

// Southwest-style #DFB reports (B737, 2026-10-29, one departure from KEUG): the first block of a report carries
//   "05201,0926,B737-700,261029,WN0436,KEUG,KDEN,2341,..."  (aircraft type, yymmdd, flight, origin, destination)
// and the later blocks (one every two seconds in the climb) carry zero padding and then
//   "13.30.19,04172,217.2,.354,-3.37,..."  (time, altitude, speed, Mach)
// The altitude, speed and Mach were checked against each other with the standard atmosphere: 289.0 kt calibrated at 11886 ft gives
// Mach 0.540, the value printed; six samples agree to 0.001. The other numbers are not named.
bool dfdau_sw_header(const Text& t, size_t at, Interpretation& r) {
  struct F { const char* s; size_t n; } f[24];
  size_t nf = 0, start = at;
  for (size_t i = at; i <= t.n && nf < 24; ++i) {
    if (i == t.n || t.at(i) == ',') {
      f[nf++] = {t.p + start, i - start};
      start = i + 1;
    }
  }
  for (size_t k = 1; k + 3 < nf; ++k) {
    if (f[k].n != 6 || !all_digits(f[k].s, 6)) continue;
    const unsigned yy = dec(f[k].s, 2), mo = dec(f[k].s + 2, 2), dd = dec(f[k].s + 4, 2);
    if (mo < 1 || mo > 12 || dd < 1 || dd > 31) continue;
    if (f[k + 1].n < 3 || f[k + 1].n > 7 || !is_upper(f[k + 1].s[0])) continue;
    if (f[k + 2].n != 4 || f[k + 3].n != 4) continue;
    bool icao = true;
    for (int q = 0; q < 4; ++q) icao = icao && is_upper(f[k + 2].s[q]) && is_upper(f[k + 3].s[q]);
    if (!icao || f[k - 1].n < 3 || f[k - 1].n > 9 || !is_upper(f[k - 1].s[0])) continue;
    add_line(r, "Report header: %.*s  flight %.*s  %.4s to %.4s  20%02u-%02u-%02u", static_cast<int>(f[k - 1].n), f[k - 1].s,
             static_cast<int>(f[k + 1].n), f[k + 1].s, f[k + 2].s, f[k + 3].s, yy, mo, dd);
    set_summary(r, "%.4s to %.4s %.*s", f[k + 2].s, f[k + 3].s, static_cast<int>(f[k - 1].n), f[k - 1].s);
    return true;
  }
  return false;
}

bool dfdau_sw_samples(const Text& t, size_t at, Interpretation& r) {
  unsigned count = 0;
  char hms[7] = "";
  unsigned alt = 0;
  char cas[8] = "", mach[8] = "";
  for (size_t i = at; i + 24 < t.n; ++i) {
    if (!(all_digits(t.p + i, 2) && t.at(i + 2) == '.' && all_digits(t.p + i + 3, 2) && t.at(i + 5) == '.' && all_digits(t.p + i + 6, 2) &&
          t.at(i + 8) == ',' && all_digits(t.p + i + 9, 5) && t.at(i + 14) == ','))
      continue;
    char h[7] = {t.at(i), t.at(i + 1), t.at(i + 3), t.at(i + 4), t.at(i + 6), t.at(i + 7), '\0'};
    if (!valid_hms(h)) continue;
    size_t p = i + 15;
    const size_t cs = p;
    while (is_digit(t.at(p))) ++p;
    if (p - cs < 2 || p - cs > 3 || t.at(p) != '.' || !is_digit(t.at(p + 1)) || t.at(p + 2) != ',') continue;
    const size_t cl = p + 2 - cs;
    const size_t ms = p + 3;
    if (!(t.at(ms) == '.' && all_digits(t.p + ms + 1, 3))) continue;
    std::memcpy(hms, h, 7);
    alt = dec(t.p + i + 9, 5);
    std::snprintf(cas, sizeof(cas), "%.*s", static_cast<int>(cl), t.p + cs);
    std::snprintf(mach, sizeof(mach), "%.4s", t.p + ms);
    ++count;
    i = ms + 3;
  }
  if (count == 0) return false;
  char when[12], alt_s[16];
  fmt_hms(when, sizeof(when), hms);
  fmt_alt_ft(alt_s, sizeof(alt_s), alt);
  set_headline(r, "FLIGHT DATA UNIT SAMPLES");
  add_line(r, "%u sample%s; latest %s  altitude %s  speed %s kt  Mach 0%s", count, count == 1 ? "" : "s", when, alt_s, cas, mach);
  set_summary(r, "%s %s kt M0%s %s", alt_s, cas, mach, when);
  return true;
}

// Climb/cruise block seen from Southwest (B737-8, 2026-10-29): "91513,32973,-51.8,241,076,CL,00000,0,  N4527.7,W1212x.x,..."
// A phase code (CL) is followed by a position in degrees and minutes. The second number is shown as the altitude and the third as the
// outside air temperature: 32973 ft and -51.8 C agree with the standard atmosphere at that level (-51 C), so both are marked probable.
bool dfdau_climb_block(const Text& t, size_t at, Interpretation& r) {
  struct F { const char* s; size_t n; } f[16];
  size_t nf = 0, start = at;
  for (size_t i = at; i <= t.n && nf < 16; ++i) {
    if (i == t.n || t.at(i) == ',') {
      while (start < i && t.p[start] == ' ') ++start;
      f[nf++] = {t.p + start, i - start};
      start = i + 1;
    }
  }
  for (size_t k = 3; k + 1 < nf; ++k) {
    const F& la = f[k];
    const F& lo = f[k + 1];
    if (la.n != 7 || lo.n < 8 || (la.s[0] != 'N' && la.s[0] != 'S') || (lo.s[0] != 'E' && lo.s[0] != 'W')) continue;
    if (!(all_digits(la.s + 1, 4) && la.s[5] == '.' && is_digit(la.s[6]))) continue;
    if (!(all_digits(lo.s + 1, 5) && lo.s[6] == '.' && is_digit(lo.s[7]))) continue;
    if (f[k - 3].n != 2 || !is_upper(f[k - 3].s[0]) || !is_upper(f[k - 3].s[1])) continue;
    char lat_d[6] = {la.s[1], la.s[2], la.s[3], la.s[4], la.s[6], '\0'};
    char lon_d[7] = {lo.s[1], lo.s[2], lo.s[3], lo.s[4], lo.s[5], lo.s[7], '\0'};
    unsigned lat = 0, lon = 0;
    if (!parse_degmin(lat_d, 2, &lat) || !parse_degmin(lon_d, 3, &lon) || lat > 90000u || lon > 180000u) continue;
    char lat_s[16], lon_s[16];
    fmt_coord(lat_s, sizeof(lat_s), la.s[0], lat);
    fmt_coord(lon_s, sizeof(lon_s), lo.s[0], lon);
    const char code[3] = {f[k - 3].s[0], f[k - 3].s[1], '\0'};
    const char* ph = phase_name(code);
    char line[kInterpLineChars];
    int u = std::snprintf(line, sizeof(line), "Position %s %s", lat_s, lon_s);
    if (nf > 2 && f[1].n == 5 && all_digits(f[1].s, 5)) u += std::snprintf(line + u, sizeof(line) - static_cast<size_t>(u), "   Altitude %u ft (probable)", dec(f[1].s, 5));
    int temp = 0;
    if (nf > 2 && f[2].n >= 3 && parse_temp(f[2].s, f[2].n, &temp)) std::snprintf(line + u, sizeof(line) - static_cast<size_t>(u), "   %dC", temp);
    set_headline(r, "FLIGHT DATA UNIT POSITION");
    add_line(r, "%s", line);
    if (ph) add_line(r, "Phase: %s", ph);
    set_summary(r, "%s %s %s", lat_s, lon_s, ph ? ph : "");
    return true;
  }
  return false;
}

// First block of a flight data unit report from several carriers: two airport codes written side by side in the header
// ("...KEUGKSMF...", "CRZ01134950677KSEAKSFO10292600...", "...CYVRKPSP...") and, in some, a position in the FMC style N44079W123220
// (degrees and tenths of minutes), which is the layout verified for the FMC position reports. The other numbers are not named.
bool dfdau_route_position(const Text& t, size_t at, bool have_route, Interpretation& r) {
  bool any = false;
  const size_t end = t.n < at + 90 ? t.n : at + 90;
  for (size_t i = at; i + 8 <= end && !have_route; ++i) {
    bool letters = true;
    for (size_t k = 0; k < 8 && letters; ++k) letters = is_upper(t.at(i + k));
    // The route may run straight into a position ("KLASKPDXN42400W121355"): a hemisphere letter followed by a digit is not more letters.
    const bool position_follows = (t.at(i + 8) == 'N' || t.at(i + 8) == 'S') && is_digit(t.at(i + 9));
    if (!letters || (is_upper(t.at(i + 8)) && !position_follows) || (i > at && is_upper(t.at(i - 1)))) continue;
    const char a = t.at(i), c = t.at(i + 4);
    if (!((a == 'K' || a == 'C' || a == 'P') && (c == 'K' || c == 'C' || c == 'P'))) continue;
    add_line(r, "Route %.4s to %.4s (from the report header)", t.p + i, t.p + i + 4);
    set_summary(r, "%.4s to %.4s", t.p + i, t.p + i + 4);
    any = true;
    break;
  }
  for (size_t i = at; i + 13 <= end; ++i) {
    if ((t.at(i) != 'N' && t.at(i) != 'S') || !all_digits(t.p + i + 1, 5) || (t.at(i + 6) != 'E' && t.at(i + 6) != 'W') ||
        !all_digits(t.p + i + 7, 6))
      continue;
    unsigned lat = 0, lon = 0;
    if (!parse_degmin(t.p + i + 1, 2, &lat) || !parse_degmin(t.p + i + 7, 3, &lon) || lat > 90000u || lon > 180000u) continue;
    char lat_s[16], lon_s[16];
    fmt_coord(lat_s, sizeof(lat_s), t.at(i), lat);
    fmt_coord(lon_s, sizeof(lon_s), t.at(i + 6), lon);
    add_line(r, "Position %s %s", lat_s, lon_s);
    if (r.summary[0] == '\0') set_summary(r, "Position %s %s", lat_s, lon_s);
    any = true;
    break;
  }
  return any;
}

bool interpret_dfdau(const Text& t, size_t at, Interpretation& r) {
  // `at` is the first character of the free text after "#DFB".
  size_t b = at;
  while (t.at(b) == ' ' || t.at(b) == '\r' || t.at(b) == '\n') ++b;
  bool any = false;
  set_headline(r, "FLIGHT DATA UNIT REPORT");
  // TRP block: "TRP#202803#044.6863#-122.3252#400#...".
  if (t.has(b, "TRP#")) {
    size_t p = b + 4;
    if (all_digits(t.p + p, 6) && valid_hms(t.p + p) && t.at(p + 6) == '#') {
      char hms[12];
      fmt_hms(hms, sizeof(hms), t.p + p);
      p += 7;
      size_t s = p;
      while (p < t.n && t.at(p) != '#') ++p;
      unsigned lat_m = 0, lon_m = 0;
      bool lat_neg = false, lon_neg = false;
      if (parse_decimal(t.p + s, p - s, &lat_m, &lat_neg) && t.at(p) == '#') {
        ++p;
        s = p;
        while (p < t.n && t.at(p) != '#') ++p;
        if (parse_decimal(t.p + s, p - s, &lon_m, &lon_neg)) {
          char lat[16], lon[16], alt[16] = "";
          fmt_coord(lat, sizeof(lat), lat_neg ? 'S' : 'N', lat_m);
          fmt_coord(lon, sizeof(lon), lon_neg ? 'W' : 'E', lon_m);
          if (t.at(p) == '#') {
            ++p;
            s = p;
            while (p < t.n && t.at(p) != '#') ++p;
            if (p - s >= 2 && p - s <= 3 && all_digits(t.p + s, p - s)) fmt_alt(alt, sizeof(alt), dec(t.p + s, p - s));
          }
          set_headline(r, "FLIGHT DATA UNIT POSITION");
          add_line(r, "Position %s %s%s%s", lat, lon, alt[0] ? "   Level " : "", alt);
          add_line(r, "Reported at %s", hms);
          set_summary(r, "TRP %s %s %s %s", lat, lon, alt, hms);
          any = true;
        }
      }
    }
  }
  // Route: "/CC<tail>,<date>,<time>,<from>,<to>,..." or ".KOAK.KSEA." in the first block of a report.
  char from[5] = "", to[5] = "";
  for (size_t i = at; i + 9 < t.n && !from[0]; ++i) {
    if (t.has(i, "/CC")) {
      size_t p = i + 3;
      char fld[5][12] = {};
      size_t nf = 0, k = 0;
      while (p < t.n && t.at(p) != '/' && nf < 5) {
        if (t.at(p) == ',') { fld[nf][k] = '\0'; ++nf; k = 0; }
        else if (k + 1 < sizeof(fld[0])) fld[nf][k++] = t.at(p);
        ++p;
      }
      if (nf < 5) { fld[nf][k] = '\0'; ++nf; }
      // tail, date, time, origin, destination
      if (nf >= 5 && std::strlen(fld[3]) == 4 && std::strlen(fld[4]) == 4 && is_upper(fld[3][0]) && is_upper(fld[4][0])) {
        std::memcpy(from, fld[3], 5);
        std::memcpy(to, fld[4], 5);
        if (all_digits(fld[2], 6) && fld[0][0]) add_line(r, "Report header: %s  %s  %.2s:%.2s:%.2sZ", fld[0], fld[1], fld[2], fld[2] + 2, fld[2] + 4);
      }
      break;
    }
    if (t.at(i) == '.' && is_upper(t.at(i + 1)) && is_upper(t.at(i + 2)) && is_upper(t.at(i + 3)) && is_upper(t.at(i + 4)) &&
        t.at(i + 5) == '.' && is_upper(t.at(i + 6)) && is_upper(t.at(i + 7)) && is_upper(t.at(i + 8)) && is_upper(t.at(i + 9))) {
      std::memcpy(from, t.p + i + 1, 4);
      std::memcpy(to, t.p + i + 6, 4);
      from[4] = to[4] = '\0';
    }
  }
  if (from[0]) {
    add_line(r, "Route %s to %s", from, to);
    set_summary(r, "%s to %s", from, to);
    any = true;
  }
  // Cruise records.
  DfPos last;
  unsigned count = 0;
  for (size_t i = at; i < t.n; ++i) {
    DfPos pos;
    size_t end = 0;
    if (match_df_record(t, i, &pos, &end)) {
      last = pos;
      ++count;
      i = end - 1;
    }
  }
  if (count > 0) {
    char lat[16], lon[16], alt[16];
    fmt_coord(lat, sizeof(lat), last.ns, last.lat_m);
    fmt_coord(lon, sizeof(lon), last.ew, last.lon_m);
    fmt_alt_ft(alt, sizeof(alt), last.alt_ft);
    const char* ph = phase_name(last.phase);
    set_headline(r, "FLIGHT DATA UNIT POSITIONS");
    if (r.line_count > 1) r.line_count = 1;          // the four-line cap: keep one earlier line so the latest fix lines are not dropped
    add_line(r, "%u position fix%s; latest %s %s   Altitude %s", count, count == 1 ? "" : "es", lat, lon, alt);
    add_line(r, "At day %u %.2s:%.2s   Outside air %dC   wind %03u/%u kt%s%s", last.day, last.hm, last.hm + 2, last.temp, last.wdir,
             last.wspd, ph ? "   " : "", ph ? ph : "");
    set_summary(r, "%u fix%s %s %s %s %dC", count, count == 1 ? "" : "es", lat, lon, alt, last.temp);
    any = true;
  }
  if (dfdau_climb_block(t, at, r)) any = true;
  if (dfdau_sw_header(t, at, r)) any = true;
  if (dfdau_sw_samples(t, at, r)) any = true;
  if (dfdau_route_position(t, at, from[0] != '\0', r)) any = true;
  r.decoded = any;                       // false: only the unit is known; interpret_h1 and interpret() keep that
  if (!any) {
    add_line(r, "Data block from the flight data recorder unit. Layout not decoded yet; raw text is shown above.");
    set_summary(r, "Flight data unit block");
  }
  return true;
}

// Label 32 position report seen from one regional carrier (five samples, 06:23-06:33Z, a descent into Eugene):
//   "M10AOO137CP,E,2026-10-29 06:23:43,4140,65924,N 44.045 W 122.171,14097,249,045,-13,333,368,585,0635,"
// The date and time, and the position, describe themselves and were checked (the time matches the receive time to two seconds; the
// positions form a track that ends at the destination airport). The ten other numbers are not documented anywhere found, so they
// are shown as received and not named. Their likely meanings are research notes in INTERPRETER.md, not on screen.
// "N 44.045 W 122.171" (label 32 from one carrier) or "N44.230 W121.750" (the same carrier's other variant, live 2026-10-29).
bool parse_spaced_position(const char* p, size_t n, char* ns, unsigned* lat, char* ew, unsigned* lon) {
  if (n < 10 || (p[0] != 'N' && p[0] != 'S')) return false;
  size_t i = 1;
  if (p[i] == ' ') ++i;
  const size_t a = i;
  while (i < n && p[i] != ' ') ++i;
  const size_t e = i;
  if (i + 2 > n) return false;
  ++i;
  if (p[i] != 'E' && p[i] != 'W') return false;
  const char hemi = p[i];
  ++i;
  if (i < n && p[i] == ' ') ++i;
  bool neg = false;
  if (!parse_decimal(p + a, e - a, lat, &neg) || neg || *lat > 90000u) return false;
  if (!parse_decimal(p + i, n - i, lon, &neg) || neg || *lon > 180000u) return false;
  *ns = p[0];
  *ew = hemi;
  return true;
}

bool interpret_label32(const Text& t, Interpretation& r, bool altitude_known) {
  if (t.n < 40 || !is_upper(t.at(10)) || t.at(11) != ',') return false;   // "P" here, "N" in a community sample
  struct F { const char* s; size_t n; } f[20];
  size_t nf = 0, start = 12;
  for (size_t i = 12; i <= t.n && nf < 20; ++i) {
    if (i == t.n || t.at(i) == ',') {
      f[nf++] = {t.p + start, i - start};
      start = i + 1;
    }
  }
  if (nf < 6 || f[0].n != 1 || f[1].n != 19) return false;
  const char* d = f[1].s;
  if (!(all_digits(d, 4) && d[4] == '-' && all_digits(d + 5, 2) && d[7] == '-' && all_digits(d + 8, 2) && d[10] == ' ' &&
        all_digits(d + 11, 2) && d[13] == ':' && all_digits(d + 14, 2) && d[16] == ':' && all_digits(d + 17, 2)))
    return false;
  const unsigned month = dec(d + 5, 2), day = dec(d + 8, 2);
  char hms[7] = {d[11], d[12], d[14], d[15], d[17], d[18], '\0'};
  if (!valid_date(dec(d, 4), month, day) || !valid_hms(hms)) return false;
  unsigned lat = 0, lon = 0;
  char ns = 'N', ew = 'W';
  if (!parse_spaced_position(f[4].s, f[4].n, &ns, &lat, &ew, &lon)) return false;
  char lat_s[16], lon_s[16], when[12];
  fmt_coord(lat_s, sizeof(lat_s), ns, lat);
  fmt_coord(lon_s, sizeof(lon_s), ew, lon);
  fmt_hms(when, sizeof(when), hms);
  set_headline(r, "POSITION REPORT");
  add_line(r, "Position %s %s   Reported %.10s %s", lat_s, lon_s, d, when);
  // The field after the position is an altitude in feet: a community note marks it "altitude in feet?" and the five samples here fall
  // from 14097 to 3723 ft on the approach to the destination. Shown as probable; everything else is shown as received.
  const bool have_alt = altitude_known && nf > 5 && f[5].n >= 1 && f[5].n <= 5 && all_digits(f[5].s, f[5].n);
  if (have_alt) add_line(r, "Altitude %.*s ft (probable)", static_cast<int>(f[5].n), f[5].s);
  char rest[kInterpLineChars];
  size_t used = 0;
  rest[0] = '\0';
  used += static_cast<size_t>(std::snprintf(rest, sizeof(rest), "%c %.*s", t.at(10), static_cast<int>(f[0].n), f[0].s));
  for (size_t k = 2; k < nf; ++k) {
    if (k == 4 || (k == 5 && have_alt) || f[k].n == 0) continue;
    if (used + f[k].n + 2 >= sizeof(rest) - 29) break;
    used += static_cast<size_t>(std::snprintf(rest + used, sizeof(rest) - used, " %.*s", static_cast<int>(f[k].n), f[k].s));
  }
  add_line(r, "Other values, not published: %s", rest);
  set_summary(r, "Position %s %s %s", lat_s, lon_s, when);
  return true;
}

// Label 33 position report seen from one regional carrier (two samples a minute apart, climbing out of cruise, 2026-10-29):
//   "M30AQX245227,B,29OCT26,133601,+44.219,-122.470,30006,7808,KMFR,KSEA,..."
// Date, time and position describe themselves; the second fix is 7.9 nm north of the first after 59 s (about 484 kt), which fits an
// E175 at FL300 flying from KMFR to KSEA. The altitude field is shown as probable, as for label 32. The two numbers before the
// airports ("27" and "B" at the start, 7808 after the altitude) are not documented and are shown as received.
bool interpret_label33(const Text& t, Interpretation& r) {
  struct F { const char* s; size_t n; } f[16];
  size_t nf = 0, start = 10;
  for (size_t i = 10; i <= t.n && nf < 16; ++i) {
    if (i == t.n || t.at(i) == ',') {
      f[nf++] = {t.p + start, i - start};
      start = i + 1;
    }
  }
  if (nf < 8 || f[2].n != 7 || f[3].n != 6 || !all_digits(f[2].s, 2) || !all_digits(f[2].s + 5, 2) || !all_digits(f[3].s, 6)) return false;
  static const char* kMonths[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
  unsigned month = 0;
  for (unsigned m = 0; m < 12; ++m)
    if (std::memcmp(f[2].s + 2, kMonths[m], 3) == 0) month = m + 1;
  const unsigned day = dec(f[2].s, 2), yy = dec(f[2].s + 5, 2);
  if (month == 0 || !valid_date(2000u + yy, month, day) || !valid_hms(f[3].s)) return false;
  unsigned lat = 0, lon = 0;
  bool lat_neg = false, lon_neg = false;
  if (f[4].n < 5 || (f[4].s[0] != '+' && f[4].s[0] != '-') || f[5].n < 5 || (f[5].s[0] != '+' && f[5].s[0] != '-')) return false;
  if (!parse_decimal(f[4].s + 1, f[4].n - 1, &lat, &lat_neg) || lat_neg || lat > 90000u) return false;
  if (!parse_decimal(f[5].s + 1, f[5].n - 1, &lon, &lon_neg) || lon_neg || lon > 180000u) return false;
  char lat_s[16], lon_s[16], when[12], hms[7];
  std::memcpy(hms, f[3].s, 6);
  hms[6] = '\0';
  fmt_coord(lat_s, sizeof(lat_s), f[4].s[0] == '-' ? 'S' : 'N', lat);
  fmt_coord(lon_s, sizeof(lon_s), f[5].s[0] == '-' ? 'W' : 'E', lon);
  fmt_hms(when, sizeof(when), hms);
  set_headline(r, "POSITION REPORT");
  add_line(r, "Position %s %s   Reported 20%02u-%02u-%02u %s", lat_s, lon_s, yy, month, day, when);
  const bool have_alt = f[6].n >= 1 && f[6].n <= 5 && all_digits(f[6].s, f[6].n);
  const bool have_route = nf >= 10 && f[8].n == 4 && f[9].n >= 4 && is_upper(f[8].s[0]) && is_upper(f[9].s[0]);
  if (have_alt || have_route) {
    char line[kInterpLineChars];
    int k = 0;
    if (have_alt) k += std::snprintf(line, sizeof(line), "Altitude %.*s ft (probable)", static_cast<int>(f[6].n), f[6].s);
    if (have_route) std::snprintf(line + k, sizeof(line) - static_cast<size_t>(k), "%sRoute %.4s to %.4s", k ? "   " : "", f[8].s, f[9].s);
    add_line(r, "%s", line);
  }
  char rest[kInterpLineChars];
  int used = std::snprintf(rest, sizeof(rest), "%.*s %.*s", static_cast<int>(f[0].n), f[0].s, static_cast<int>(f[1].n), f[1].s);
  if (nf > 7 && f[7].n > 0 && used > 0 && static_cast<size_t>(used) + f[7].n + 2 < sizeof(rest))
    std::snprintf(rest + used, sizeof(rest) - static_cast<size_t>(used), " %.*s", static_cast<int>(f[7].n), f[7].s);
  add_line(r, "Other values, not published: %s", rest);
  set_summary(r, "Position %s %s %s", lat_s, lon_s, when);
  return true;
}

// Label 22 position report seen from one low-cost carrier (live, 2026-10-29, flights at FL300 to FL380):
//   "N 440914W1213321,-------,152006,37929, ,      , ,M 59,23777  62, 104,"
// Position as degrees, minutes and seconds (44 09 14 N, 121 33 21 W), then a time hhmmss, then a number that behaves like an altitude
// in feet (30210, 20246 and 37966 on three climbs and a cruise) and an "M 59" that behaves like a temperature (M = minus, as in ARINC
// weather groups; -59 C at 37929 ft is 3 C below the standard atmosphere). Two consecutive fixes 5 minutes apart are 33 nm apart,
// which is 400 kt. Altitude and temperature are shown as probable; the remaining numbers are not named.
bool interpret_label22(const Text& t, Interpretation& r) {
  if (t.n < 40 || (t.at(10) != 'N' && t.at(10) != 'S') || t.at(11) != ' ' || !all_digits(t.p + 12, 6) || (t.at(18) != 'E' && t.at(18) != 'W') ||
      !all_digits(t.p + 19, 7) || t.at(26) != ',')
    return false;
  const unsigned lat_d = dec(t.p + 12, 2), lat_m = dec(t.p + 14, 2), lat_s = dec(t.p + 16, 2);
  const unsigned lon_d = dec(t.p + 19, 3), lon_m = dec(t.p + 22, 2), lon_s = dec(t.p + 24, 2);
  if (lat_d > 90 || lon_d > 180 || lat_m > 59 || lon_m > 59 || lat_s > 59 || lon_s > 59) return false;
  size_t i = 27;
  while (i < t.n && t.at(i) != ',') ++i;                       // the field of dashes
  if (i >= t.n) return false;
  ++i;
  if (!all_digits(t.p + i, 6) || t.at(i + 6) != ',' || !valid_hms(t.p + i)) return false;
  char hms[7];
  std::memcpy(hms, t.p + i, 6);
  hms[6] = '\0';
  i += 7;
  const size_t alt_at = i;
  while (is_digit(t.at(i))) ++i;
  const size_t alt_n = i - alt_at;
  const bool have_alt = alt_n >= 3 && alt_n <= 5 && t.at(i) == ',';
  const unsigned lat_milli = lat_d * 1000u + ((lat_m * 60u + lat_s) * 1000u) / 3600u;
  const unsigned lon_milli = lon_d * 1000u + ((lon_m * 60u + lon_s) * 1000u) / 3600u;
  char lat_str[16], lon_str[16], when[12];
  fmt_coord(lat_str, sizeof(lat_str), t.at(10), lat_milli);
  fmt_coord(lon_str, sizeof(lon_str), t.at(18), lon_milli);
  fmt_hms(when, sizeof(when), hms);
  // Temperature: "M 59" or "P 12" a few fields after the altitude.
  int temp = 0;
  bool have_temp = false;
  for (size_t k = i, commas = 0; k + 3 < t.n && commas < 6; ++k) {
    if (t.at(k) == ',') {
      ++commas;
      if ((t.at(k + 1) == 'M' || t.at(k + 1) == 'P') && t.at(k + 2) == ' ' && all_digits(t.p + k + 3, 2)) {
        temp = static_cast<int>(dec(t.p + k + 3, 2));
        if (t.at(k + 1) == 'M') temp = -temp;
        have_temp = true;
        break;
      }
    }
  }
  set_headline(r, "POSITION REPORT");
  add_line(r, "Position %s %s   Reported %s", lat_str, lon_str, when);
  if (have_alt && have_temp) add_line(r, "Altitude %.*s ft (probable)   Outside air %dC (probable)", static_cast<int>(alt_n), t.p + alt_at, temp);
  else if (have_alt) add_line(r, "Altitude %.*s ft (probable)", static_cast<int>(alt_n), t.p + alt_at);
  set_summary(r, "Position %s %s %s", lat_str, lon_str, when);
  return true;
}

// Label 10 "LDR01" report seen from one carrier (live, 2026-10-29, cruise at FL370 to FL400):
//   "LDR01,189,D,SWA-2600-019,0,N 43.779,W122.100,40003, 11.2,KSMF,KSEA,KSEA,16R/,16L/,..."
// The position and the airport codes describe themselves and the position moves along a plausible track between consecutive reports.
// The 5 digit number after the position behaves like an altitude in feet, and is shown as probable. What "LDR01" and the other
// numbers are is not published anywhere found; they are left out.
bool interpret_label10_ldr(const Text& t, Interpretation& r) {
  if (!t.has(10, "LDR")) return false;
  struct F { const char* s; size_t n; } f[16];
  size_t nf = 0, start = 10;
  for (size_t i = 10; i <= t.n && nf < 16; ++i) {
    if (i == t.n || t.at(i) == ',') {
      f[nf++] = {t.p + start, i - start};
      start = i + 1;
    }
  }
  if (nf < 11 || f[5].n < 8 || f[6].n < 8) return false;
  // "N 43.779" and "W122.100" joined with a space for the shared parser.
  char joined[40];
  const int n = std::snprintf(joined, sizeof(joined), "%.*s %.*s", static_cast<int>(f[5].n), f[5].s, static_cast<int>(f[6].n), f[6].s);
  if (n <= 0 || static_cast<size_t>(n) >= sizeof(joined)) return false;
  char ns = 'N', ew = 'W';
  unsigned lat = 0, lon = 0;
  if (!parse_spaced_position(joined, static_cast<size_t>(n), &ns, &lat, &ew, &lon)) return false;
  char lat_s[16], lon_s[16];
  fmt_coord(lat_s, sizeof(lat_s), ns, lat);
  fmt_coord(lon_s, sizeof(lon_s), ew, lon);
  set_headline(r, "POSITION REPORT");
  const bool have_alt = f[7].n >= 4 && f[7].n <= 5 && all_digits(f[7].s, f[7].n);
  bool airports = f[9].n == 4 && f[10].n == 4;
  for (size_t k = 9; k < 11 && airports; ++k)
    for (size_t q = 0; q < 4; ++q) airports = airports && is_upper(f[k].s[q]);
  add_line(r, "Position %s %s%s%.*s%s", lat_s, lon_s, have_alt ? "   Altitude " : "", have_alt ? static_cast<int>(f[7].n) : 0, f[7].s,
           have_alt ? " ft (probable)" : "");
  if (airports) add_line(r, "Airports listed: %.4s and %.4s", f[9].s, f[10].s);
  set_summary(r, "Position %s %s", lat_s, lon_s);
  return true;
}

// Central fault display (H1 sub-label CF) event list seen from one regional carrier (three distinct messages, one resent):
//   "FDE0633591029ABZ 06315707301    0    0 06315707308    0    0 06320507310    0    0"
// Structure: group tag "FDE", report time hhmmss, "1029ABZ" (not understood), then entries of hhmmss + a five digit code + two numbers
// (zero in every sample). The codes are not published and are shown as received; INTERPRETER.md records what the samples suggest.
bool interpret_cf(const Text& t, size_t at, Interpretation& r) {
  if (!t.has(at, "FDE") || !all_digits(t.p + at + 3, 6)) return false;
  char hms[7];
  std::memcpy(hms, t.p + at + 3, 6);
  hms[6] = '\0';
  if (!valid_hms(hms)) return false;
  char tag[12];
  size_t tag_len = 0;
  size_t i = at + 9;
  auto is_ws = [](char c) { return c == ' ' || c == '\r' || c == '\n'; };
  while (i < t.n && !is_ws(t.at(i)) && tag_len + 1 < sizeof(tag)) tag[tag_len++] = t.at(i++);
  tag[tag_len] = '\0';
  struct Entry { char hms[7]; char code[6]; } e[10];
  size_t shown = 0, total = 0;
  size_t s = 0, len = 0;
  auto next = [&]() {
    while (i < t.n && is_ws(t.at(i))) ++i;
    s = i;
    while (i < t.n && !is_ws(t.at(i))) ++i;
    len = i - s;
    return len > 0;
  };
  while (next()) {
    if (len != 11 || !all_digits(t.p + s, 11)) return false;       // an entry: hhmmss + five digit code
    char entry_time[7];
    std::memcpy(entry_time, t.p + s, 6);
    entry_time[6] = '\0';
    if (!valid_hms(entry_time)) return false;
    if (shown < 10) {
      std::memcpy(e[shown].hms, entry_time, 7);
      std::memcpy(e[shown].code, t.p + s + 6, 5);
      e[shown].code[5] = '\0';
      ++shown;
    }
    ++total;
    for (int k = 0; k < 2; ++k)                                      // two trailing numbers
      if (!next() || len > 6 || !all_digits(t.p + s, len)) return false;
  }
  if (total == 0) return false;
  char when[12];
  fmt_hms(when, sizeof(when), hms);
  set_headline(r, "FLIGHT DECK EFFECTS (FAULT DISPLAY)");
  // "FDE" is Flight Deck Effect (community documentation). The four digits of the tag are month and day: "1029" is the day this was
  // captured, and a community sample "0805" sits beside a 2020-08-05 report. The three letters after them are not understood.
  char when_date[16] = "";
  if (tag_len == 7 && all_digits(tag, 4) && dec(tag, 2) >= 1 && dec(tag, 2) <= 12 && dec(tag + 2, 2) >= 1 && dec(tag + 2, 2) <= 31)
    std::snprintf(when_date, sizeof(when_date), " %c%c-%c%c", tag[0], tag[1], tag[2], tag[3]);
  const char* letters = tag_len == 7 ? tag + 4 : tag;
  if (total > shown) add_line(r, "Report %s%s, tag %s: %u entries, first %u shown. Codes as received.", when, when_date, letters, static_cast<unsigned>(total), static_cast<unsigned>(shown));
  else add_line(r, "Report %s%s, tag %s: %u entr%s. Codes as received.", when, when_date, letters, static_cast<unsigned>(total), total == 1 ? "y" : "ies");
  for (size_t first = 0; first < shown; first += 5) {
    char row[kInterpLineChars];
    size_t used = 0;
    row[0] = '\0';
    for (size_t k = first; k < shown && k < first + 5; ++k) {
      char hh[12];
      fmt_hms(hh, sizeof(hh), e[k].hms);
      hh[8] = '\0';                                                  // drop the Z: these are entry times, shown as hh:mm:ss
      used += static_cast<size_t>(std::snprintf(row + used, sizeof(row) - used, "%s%s %s", used ? "  " : "", hh, e[k].code));
    }
    add_line(r, "%s", row);
  }
  set_summary(r, "Fault display events (%u) %s", static_cast<unsigned>(total), when);
  return true;
}

// ARINC 622 message carried in an H1 block: "/BA USADCXA.AT1.N926WN6082D826032E1F" (checked live, 2026-10-29). Layout from the ARINC
// 622 specification: "/", two-letter message function ("BA" downlink, "AA" uplink), a space, a 7 character ground address, ".", the
// 3 character application code, ".", the aircraft registration, the data as hex digits, and a 4 digit hex CRC. The CRC is the CRC-16
// with polynomial 0x1021, start value 0xFFFF and final complement, computed over the application code, ".", registration and the data
// bytes. It reproduces the specification's published examples and every live block checked, so a block that fails it is not decoded.
// The data bits (CPDLC and similar) need a separate commercial standard and are shown as received.
unsigned crc16_arinc(const unsigned char* p, size_t n, unsigned crc) {
  for (size_t i = 0; i < n; ++i) {
    crc ^= static_cast<unsigned>(p[i]) << 8;
    for (int k = 0; k < 8; ++k) crc = (crc & 0x8000u) ? ((crc << 1) ^ 0x1021u) & 0xFFFFu : (crc << 1) & 0xFFFFu;
  }
  return crc;
}
bool is_hex_digit(char c) { return is_digit(c) || (c >= 'A' && c <= 'F'); }
unsigned hex_value(char c) { return is_digit(c) ? static_cast<unsigned>(c - '0') : static_cast<unsigned>(c - 'A' + 10); }

bool interpret_arinc622(const Text& t, size_t at, Interpretation& r) {
  if (t.at(at) != '/' || !is_upper(t.at(at + 1)) || !is_upper(t.at(at + 2)) || t.at(at + 3) != ' ') return false;
  const size_t ga = at + 4;
  if (t.at(ga + 7) != '.' || t.at(ga + 11) != '.') return false;
  for (size_t i = 0; i < 7; ++i)
    if (!(is_upper(t.at(ga + i)) || is_digit(t.at(ga + i)))) return false;
  const size_t imi = ga + 8;
  if (!is_upper(t.at(imi)) || !is_upper(t.at(imi + 1)) || !(is_upper(t.at(imi + 2)) || is_digit(t.at(imi + 2)))) return false;
  const size_t rest = imi + 4;
  if (t.n < rest + 5 + 4) return false;
  const size_t tail_end = t.n;
  for (size_t i = tail_end - 4; i < tail_end; ++i)
    if (!is_hex_digit(t.at(i))) return false;
  const unsigned want = (hex_value(t.at(tail_end - 4)) << 12) | (hex_value(t.at(tail_end - 3)) << 8) | (hex_value(t.at(tail_end - 2)) << 4) | hex_value(t.at(tail_end - 1));
  // The registration is 4 to 9 characters; the rest up to the CRC is the hex data. The CRC picks the split.
  for (size_t reg_len = 4; reg_len <= 9; ++reg_len) {
    const size_t data_at = rest + reg_len;
    if (data_at + 4 >= tail_end) break;
    const size_t data_len = tail_end - 4 - data_at;
    bool hex = true;
    for (size_t i = data_at; i < data_at + data_len; ++i) hex = hex && is_hex_digit(t.at(i));
    if (!hex || data_len > 64) continue;
    unsigned char buf[96];
    size_t n = 0;
    for (size_t i = 0; i < 3; ++i) buf[n++] = static_cast<unsigned char>(t.at(imi + i));
    buf[n++] = '.';
    for (size_t i = 0; i < reg_len; ++i) buf[n++] = static_cast<unsigned char>(t.at(rest + i));
    for (size_t i = 0; i < data_len; i += 2) {
      const unsigned hi = hex_value(t.at(data_at + i));
      const unsigned lo = i + 1 < data_len ? hex_value(t.at(data_at + i + 1)) : 0u;   // an odd count is padded with a zero nibble
      buf[n++] = static_cast<unsigned char>((hi << 4) | lo);
    }
    const unsigned got = crc16_arinc(buf, n, 0xFFFFu) ^ 0xFFFFu;
    if (got != want) continue;
    char app[4] = {t.at(imi), t.at(imi + 1), t.at(imi + 2), '\0'};
    const char* app_name = nullptr;
    if (app[0] == 'A' && app[1] == 'T') app_name = "ATC data";
    else if (app[0] == 'C' && app[1] == 'R') app_name = "ATC connect request";
    else if (app[0] == 'C' && app[1] == 'C') app_name = "ATC connect confirm";
    else if (app[0] == 'D' && app[1] == 'R') app_name = "ATC disconnect";
    else if (std::strcmp(app, "ADS") == 0) app_name = "surveillance report (ADS)";
    else if (std::strcmp(app, "AFN") == 0) app_name = "logon (AFN)";
    else if (std::strcmp(app, "DIS") == 0) app_name = "disconnect";
    const char dir = t.at(at + 1);
    // Short registrations are padded to 7 characters with leading dots (live: "AT1..N6509", "AT1..B-LJK"); the dots are part of the
    // checksummed text but not of the registration.
    const char* reg = t.p + rest;
    size_t reg_n = reg_len;
    while (reg_n > 1 && *reg == '.') { ++reg; --reg_n; }
    set_headline(r, "ATC DATA LINK MESSAGE (ARINC 622)");
    add_line(r, "%s   Ground address %.7s   %s", dir == 'B' ? "Downlink" : dir == 'A' ? "Uplink" : "Message", t.p + ga, app_name ? app_name : app);
    add_line(r, "Aircraft %.*s   %u data byte%s   checksum OK", static_cast<int>(reg_n), reg, static_cast<unsigned>((data_len + 1) / 2),
             (data_len + 1) / 2 == 1 ? "" : "s");
    add_line(r, "Content as received: %.*s (needs the ATC message table)", static_cast<int>(data_len), t.p + data_at);
    set_summary(r, "ATC %s %s %.*s", app, dir == 'B' ? "down" : dir == 'A' ? "up" : "", static_cast<int>(reg_n), reg);
    return true;
  }
  return false;
}

// D-ATIS request, label B9: "/KSEA.TI2/024KSEAA3BC3" after the 10 character header (ARINC 623 appendix B; 6 live samples all pass the CRC).
// After "TI2/" come: preferred characters per display line (3 digits), airport (4 letters), request type (1 letter) and a 4 digit hex
// CRC, the same CRC-16 as above over "TI2/" and the eight data characters.
bool interpret_atis_request(const Text& t, Interpretation& r) {
  // The station is 4 to 7 letters or digits ("KSEA", "ATSUAXA" live), so every later index moves by `o`.
  if (t.at(10) != '/') return false;
  size_t dot = 11;
  while (dot < 18 && (is_upper(t.at(dot)) || is_digit(t.at(dot)))) ++dot;
  if (dot < 15 || t.at(dot) != '.') return false;
  const size_t o = dot - 15;
  if (t.at(16 + o) != 'T' || t.at(17 + o) != 'I' || !is_digit(t.at(18 + o)) || t.at(19 + o) != '/') return false;
  if (t.n != 32 + o || !all_digits(t.p + 20 + o, 3)) return false;
  for (size_t i = 23 + o; i < 27 + o; ++i)
    if (!is_upper(t.at(i))) return false;
  const char kind = t.at(27 + o);
  for (size_t i = 28 + o; i < 32 + o; ++i)
    if (!is_hex_digit(t.at(i))) return false;
  const unsigned want = (hex_value(t.at(28 + o)) << 12) | (hex_value(t.at(29 + o)) << 8) | (hex_value(t.at(30 + o)) << 4) | hex_value(t.at(31 + o));
  const unsigned got = crc16_arinc(reinterpret_cast<const unsigned char*>(t.p + 16 + o), 12, 0xFFFFu) ^ 0xFFFFu;
  if (got != want) return false;
  const char* what = nullptr;
  switch (kind) {
    case 'A': what = "arrival ATIS"; break;
    case 'D': what = "departure ATIS"; break;
    case 'C': what = "arrival ATIS, automatic updates"; break;
    case 'E': what = "en-route weather broadcast"; break;
    case 'T': what = "stop automatic updates"; break;
    default: return false;
  }
  set_headline(r, "AIRPORT WEATHER (ATIS) REQUEST");
  add_line(r, "Aircraft asks station %.*s for %.4s %s", static_cast<int>(4 + o), t.p + 11, t.p + 23 + o, what);
  add_line(r, "Display width %.3s characters   checksum OK", t.p + 20 + o);
  set_summary(r, "ATIS request %.4s", t.p + 23 + o);
  return true;
}

unsigned month_from_abbrev(const char* s) {
  static const char* const kMonths[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
  for (unsigned m = 0; m < 12; ++m)
    if (std::memcmp(s, kMonths[m], 3) == 0) return m + 1;
  return 0;
}
bool valid_hhmm(const char* s) { return all_digits(s, 4) && dec(s, 2) < 24 && dec(s + 2, 2) < 60; }

// H1 sub-label T1 (live, two carriers, 2026-10-29): "163AUA1241#T1BRTE 1 29OCT26 1209 N37257 UAL1241 KEUG/KDEN BCG59-U000-08..."
// The date, time, tail, call sign and airport pair describe themselves. The three letter word ("RTE", "PLF") and the rest are shown as
// received; what the message is for is not published anywhere found.
bool interpret_t1(const Text& t, Interpretation& r) {
  struct Tok { const char* s; size_t n; } tok[8];
  size_t n = 0, i = 14;
  while (n < 8) {
    while (i < t.n && t.at(i) == ' ') ++i;
    if (i >= t.n) break;
    const size_t s = i;
    while (i < t.n && t.at(i) != ' ' && t.at(i) != '\r' && t.at(i) != '\n') ++i;
    tok[n++] = {t.p + s, i - s};
    if (i < t.n && (t.at(i) == '\r' || t.at(i) == '\n')) break;
  }
  if (n < 7 || tok[0].n != 3 || tok[2].n != 7 || tok[3].n != 4 || tok[6].n < 9) return false;
  for (size_t k = 0; k < 3; ++k)
    if (!is_upper(tok[0].s[k])) return false;
  if (!all_digits(tok[2].s, 2) || !all_digits(tok[2].s + 5, 2) || month_from_abbrev(tok[2].s + 2) == 0 || !valid_hhmm(tok[3].s)) return false;
  const char* ap = tok[6].s;
  for (size_t k = 0; k < 9; ++k)
    if (k == 4 ? ap[k] != '/' : !is_upper(ap[k])) return false;
  set_headline(r, "MESSAGE T1 (AIRLINE SYSTEM TEXT)");
  add_line(r, "Report %.3s %.*s, %.7s %.2s:%.2sZ", tok[0].s, static_cast<int>(tok[1].n), tok[1].s, tok[2].s, tok[3].s, tok[3].s + 2);
  add_line(r, "Aircraft %.*s   Flight %.*s   %.4s to %.4s", static_cast<int>(tok[4].n), tok[4].s, static_cast<int>(tok[5].n), tok[5].s, ap, ap + 5);
  add_line(r, "The word %.3s and the rest of this text are shown as received.", tok[0].s);
  set_summary(r, "%.3s %.4s to %.4s", tok[0].s, ap, ap + 5);
  return true;
}

// "In range" reports (labels 28 and 80, live from two carriers): "...3701 INRANG 1358/29 KSMF/KSEA .N253AK /ETA 1430" (1358 is the
// flight number, 29 the day of the month, and the ETA is the expected arrival time; checked against the ACARS headers of both carriers).
bool interpret_inrange(const Text& t, Interpretation& r) {
  size_t at = 0;
  for (size_t i = 10; i + 7 < t.n && i < 30; ++i)
    if (t.has(i, "INRANG ")) { at = i + 7; break; }
  if (!at) return false;
  struct Tok { const char* s; size_t n; } tok[6];
  size_t n = 0, i = at;
  while (n < 6) {
    while (i < t.n && t.at(i) == ' ') ++i;
    if (i >= t.n) break;
    const size_t s = i;
    while (i < t.n && t.at(i) != ' ') ++i;
    tok[n++] = {t.p + s, i - s};
  }
  if (n < 5 || tok[0].n != 7 || tok[1].n != 9 || tok[3].n != 4 || std::memcmp(tok[3].s, "/ETA", 4) != 0 || tok[4].n < 4) return false;
  // The first token is the flight number and the day of the month ("1358/29" on flight AS1358), not a time.
  if (!all_digits(tok[0].s, 4) || tok[0].s[4] != '/' || !all_digits(tok[0].s + 5, 2) || dec(tok[0].s + 5, 2) < 1 || dec(tok[0].s + 5, 2) > 31 ||
      !valid_hhmm(tok[4].s))
    return false;
  for (size_t k = 0; k < 9; ++k)
    if (k == 4 ? tok[1].s[k] != '/' : !is_upper(tok[1].s[k])) return false;
  const char* tail = tok[2].s;
  size_t tail_n = tok[2].n;
  if (tail_n > 1 && tail[0] == '.') { ++tail; --tail_n; }
  set_headline(r, "IN-RANGE REPORT");
  add_line(r, "Day %.2s   Route %.4s to %.4s", tok[0].s + 5, tok[1].s, tok[1].s + 5);
  add_line(r, "Aircraft %.*s   Expected arrival %.2s:%.2sZ", static_cast<int>(tail_n), tail, tok[4].s, tok[4].s + 2);
  set_summary(r, "In range %.4s to %.4s ETA %.2s:%.2sZ", tok[1].s, tok[1].s + 5, tok[4].s, tok[4].s + 2);
  return true;
}

bool interpret_h1(const MessageRecord& m, const Text& t, Interpretation& r) {
  const Header h = parse_header(m, t);
  if (!h.ok || t.at(10) != '#' || t.n < 13) return false;
  const char a = t.at(11), b = t.at(12);
  const char* unit = sublabel_name(a, b);
  const bool reserved_b = t.at(13) == 'B';   // ARINC 620: the character after the sub-label is reserved and coded 'B'
  const size_t body = reserved_b ? 14 : 13;
  bool done = false;
  if (t.at(body) == '/' && interpret_arinc622(t, body, r)) done = true;
  else if (a == 'M' && (b == '1' || b == '2' || b == '3' || b == 'D')) done = interpret_fmc_position(t, body, r);
  else if (a == 'D' && b == 'F') done = interpret_dfdau(t, body, r);
  else if (a == 'C' && b == 'F') done = interpret_cf(t, body, r);
  if (done) {
    // The first line identifies the message, then the decoded lines follow. Only the flight data unit parser says when it understood nothing.
    const bool really_decoded = (a == 'D' && b == 'F') ? r.decoded : true;
    Interpretation tmp = r;
    tmp.decoded = really_decoded;
    r.line_count = 0;
    add_header_line(r, h);
    for (size_t i = 0; i < tmp.line_count && r.line_count < kInterpLines; ++i) add_line(r, "%s", tmp.lines[i]);
    r.decoded = really_decoded;
    return true;
  }
  char head[48];
  if (unit) std::snprintf(head, sizeof(head), "MESSAGE FOR/FROM %s", unit);
  else std::snprintf(head, sizeof(head), "H1 MESSAGE, SUB-LABEL %c%c", a, b);
  for (char* c = head; *c; ++c)
    if (*c >= 'a' && *c <= 'z') *c = static_cast<char>(*c - 32);
  set_headline(r, head);
  add_header_line(r, h);
  add_line(r, "Layout of this %s message is not decoded; raw text is shown above.", unit ? unit : "sub-label");
  set_summary(r, "%s %s", unit ? unit : "H1", h.flight);
  r.decoded = false;                      // the unit is named, the text is not understood
  return true;
}

}  // namespace

const char* label_name(const char* label) {
  if (!label || !label[0] || !label[1]) return nullptr;
  for (const auto& r : kLabels)
    if (r.a == label[0] && r.b == label[1]) return r.name;
  if (label[0] == '8' && (is_digit(label[1]) || label[1] == '`')) return "Crew-addressed downlink";
  return nullptr;
}

const char* airline_name(const char* flight) {
  if (!flight || !flight[0] || !flight[1] || !is_digit(flight[2])) return nullptr;
  for (const auto& r : kAirlines)
    if (r.a == flight[0] && r.b == flight[1]) return r.name;
  return nullptr;
}

bool interpret(const MessageRecord& m, Interpretation& r) {
  r = Interpretation{};
  const Text t{m.text, m.has_text ? m.text_length : 0u};
  const char* l = m.label;
  bool ok = false;
  bool h1_decoded = true;
  if (l[0] == 'A' && l[1] == 'V' && m.mode == '2') {
    // A VDL2 data frame that is not ACARS, kept as received (acars_receiver). The label is ours, not an ACARS label.
    set_headline(r, "VDL2 DATA FRAME");
    add_line(r, "Verified link-layer frame from address %.7s.", m.tail);
    add_line(r, "ATN, CPDLC and ADS-C content is not decoded; the raw bytes are shown as hex.");
    set_summary(r, "VDL2 data frame %.7s", m.tail);
    r.recognised = true;
    return true;
  }
  if (l[0] == '_' && l[1] == '?') ok = interpret_response(m, t, r);
  else if (l[0] == ':' && l[1] == ';') ok = interpret_autotune(t, r);
  else if (l[0] == 'S' && l[1] == 'Q') ok = interpret_squitter(t, r);
  else if (l[0] == 'Q' && l[1] == '0') {
    set_headline(r, "LINK TEST");
    add_line(r, "Routine radio link test between aircraft and ground.");
    const Header h = parse_header(m, t);
    if (h.ok) add_line(r, "Message %.4s   Flight %s", h.msn, h.flight);
    set_summary(r, "Link test%s%s", h.ok ? " " : "", h.ok ? h.flight : "");
    ok = true;
    h1_decoded = h.ok;                  // without a readable header only the label's meaning is known
  } else if (l[0] == '_' && l[1] == 'd') {
    set_headline(r, "LINK PING");
    add_line(r, "Demand-mode ping used to keep or test the radio link.");
    set_summary(r, "Link ping");
    ok = true;
  } else if (l[0] == 'H' && l[1] == '1' && t.has(10, "#T1B")) {
    ok = interpret_t1(t, r);
  } else if (l[0] == 'H' && l[1] == '1') {
    ok = interpret_h1(m, t, r);
    h1_decoded = ok && r.decoded;
  } else if ((l[0] == '2' && l[1] == '8') || (l[0] == '8' && l[1] == '0')) {
    const Header h = parse_header(m, t);
    if (h.ok && interpret_inrange(t, r)) {
      Interpretation tmp = r;
      r.line_count = 0;
      add_header_line(r, h);
      for (size_t i = 0; i < tmp.line_count && r.line_count < kInterpLines; ++i) add_line(r, "%s", tmp.lines[i]);
      ok = true;
    }
  }
  else if (l[0] == '2' && l[1] == '1') {
    const Header h = parse_header(m, t);
    if (h.ok && interpret_label21(t, 10, r)) {
      Interpretation tmp = r;
      r.line_count = 0;
      add_header_line(r, h);
      for (size_t i = 0; i < tmp.line_count && r.line_count < kInterpLines; ++i) add_line(r, "%s", tmp.lines[i]);
      ok = true;
    }
  } else if ((l[0] == '2' && l[1] == '2') || (l[0] == '1' && l[1] == '0')) {
    const Header h = parse_header(m, t);
    if (h.ok && (l[0] == '2' ? interpret_label22(t, r) : interpret_label10_ldr(t, r))) {
      Interpretation tmp = r;
      r.line_count = 0;
      add_header_line(r, h);
      for (size_t i = 0; i < tmp.line_count && r.line_count < kInterpLines; ++i) add_line(r, "%s", tmp.lines[i]);
      ok = true;
    }
  } else if (l[0] == 'B' && l[1] == '9') {
    const Header h = parse_header(m, t);
    if (h.ok && interpret_atis_request(t, r)) {
      Interpretation tmp = r;
      r.line_count = 0;
      add_header_line(r, h);
      for (size_t i = 0; i < tmp.line_count && r.line_count < kInterpLines; ++i) add_line(r, "%s", tmp.lines[i]);
      ok = true;
    }
  } else if (l[0] == '3' && l[1] == '3') {
    const Header h = parse_header(m, t);
    if (h.ok && interpret_label33(t, r)) {
      Interpretation tmp = r;
      r.line_count = 0;
      add_header_line(r, h);
      for (size_t i = 0; i < tmp.line_count && r.line_count < kInterpLines; ++i) add_line(r, "%s", tmp.lines[i]);
      ok = true;
    }
  } else if ((l[0] == '3' && l[1] == '2') || (l[0] == '1' && l[1] == '7')) {
    // Label 17 from the same carrier has the same layout (live, 2026-10-29); its altitude field is not confirmed, so it stays unnamed.
    const Header h = parse_header(m, t);
    if (h.ok && interpret_label32(t, r, l[0] == '3')) {
      Interpretation tmp = r;
      r.line_count = 0;
      add_header_line(r, h);
      for (size_t i = 0; i < tmp.line_count && r.line_count < kInterpLines; ++i) add_line(r, "%s", tmp.lines[i]);
      ok = true;
    }
  }
  const bool parsed = ok;
  if (!ok) {
    // Known label, no text parser: still say what kind of message it is.
    const char* name = label_name(l);
    if (!name) { r = Interpretation{}; return false; }
    r = Interpretation{};
    std::snprintf(r.headline, sizeof(r.headline), "%s", name);
    for (char* c = r.headline; *c; ++c)
      if (*c >= 'a' && *c <= 'z') *c = static_cast<char>(*c - 32);
    const Header h = parse_header(m, t);
    add_header_line(r, h);
    add_line(r, "Label %s. The text layout is not decoded; raw text is shown above.", l);
    set_summary(r, "%s", name);
    ok = true;
  }
  r.recognised = ok;
  r.decoded = parsed && h1_decoded;
  return ok;
}

}  // namespace orcsdr::acars_ui
