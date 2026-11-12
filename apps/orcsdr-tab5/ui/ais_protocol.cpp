#include "ais_protocol.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace orcsdr::ais {
namespace {
constexpr TypeLength kLengths[] = {
  {0,0}, {168,168},{168,168},{168,168},{168,168},{424,424},
  {88,1008},{72,168},{56,1008},{168,168},{72,72},{168,168},
  {72,1008},{72,168},{40,1008},{88,160},{96,144},{80,816},
  {168,168},{312,312},{72,160},{272,360},{168,168},{160,160},
  {160,168},{40,168},{60,1004},{96,96},{168,168}
};
static_assert(sizeof(kLengths)/sizeof(kLengths[0]) == 29);

class Bits {
 public:
  Bits(const uint8_t* data, size_t length) : data_(data), length_(length) {}
  bool get(size_t start, unsigned width, uint32_t* out) const {
    if (!out || width > 32 || start > length_ || width > length_ - start) return false;
    uint32_t value = 0;
    for (unsigned i = 0; i < width; ++i)
      value = (value << 1) | ((data_[(start + i) / 8] >> (7 - ((start + i) % 8))) & 1u);
    *out = value;
    return true;
  }
  int32_t signed_value(size_t start, unsigned width) const {
    uint32_t value = 0;
    if (!get(start, width, &value)) return 0;
    if (width < 32 && (value & (uint32_t{1} << (width - 1))))
      value |= (~uint32_t{0} << width);
    return static_cast<int32_t>(value);
  }
  void text(size_t start, size_t chars, char* out, size_t capacity) const {
    if (!out || !capacity) return;
    size_t n = 0;
    for (size_t i = 0; i < chars && n + 1 < capacity; ++i) {
      uint32_t v = 0;
      if (!get(start + 6 * i, 6, &v)) break;
      char c = v < 32 ? static_cast<char>(v + 64) : static_cast<char>(v);
      if (c == '@') c = ' ';
      out[n++] = c;
    }
    while (n && out[n - 1] == ' ') --n;
    out[n] = '\0';
  }
 private:
  const uint8_t* data_;
  size_t length_;
};

// Raw AIS long/lat values are signed arcminute fractions: 1/10000 or 1/10 minute.
// Using integer arithmetic keeps unavailable values distinct from zero and avoids NaN in snapshots.
void position(const Bits& b, size_t lon_at, unsigned lon_bits, size_t lat_at,
              unsigned lat_bits, int divisor, Message* out) {
  const int64_t lon = b.signed_value(lon_at, lon_bits);
  const int64_t lat = b.signed_value(lat_at, lat_bits);
  if (lon < -int64_t{180} * divisor || lon > int64_t{180} * divisor ||
      lat < -int64_t{90} * divisor || lat > int64_t{90} * divisor) return;
  out->longitude_e7 = static_cast<int32_t>(lon * 10000000 / divisor);
  out->latitude_e7 = static_cast<int32_t>(lat * 10000000 / divisor);
  out->has_position = true;
}
void motion(const Bits& b, size_t sog, size_t cog, size_t heading, Message* out) {
  uint32_t value = 0;
  if (b.get(sog,10,&value) && value != 1023) {
    out->has_speed = true; out->speed_tenths_kn = static_cast<uint16_t>(value);
  }
  if (b.get(cog,12,&value) && value < 3600) {
    out->has_course = true; out->course_tenths_deg = static_cast<uint16_t>(value);
  }
  if (b.get(heading,9,&value) && value < 360) {
    out->has_heading = true; out->heading_deg = static_cast<uint16_t>(value);
  }
}
void dimensions(const Bits& b, size_t at, VesselStatic* out) {
  uint32_t bow = 0, stern = 0, port = 0, starboard = 0;
  if (!b.get(at,9,&bow) || !b.get(at+9,9,&stern) || !b.get(at+18,6,&port) || !b.get(at+24,6,&starboard)) return;
  if (!bow && !stern && !port && !starboard) return;   // all zero = not available
  out->has_dimensions = true;
  out->to_bow_m = static_cast<uint16_t>(bow); out->to_stern_m = static_cast<uint16_t>(stern);
  out->to_port_m = static_cast<uint8_t>(port); out->to_starboard_m = static_cast<uint8_t>(starboard);
}
void ship_type(const Bits& b, size_t at, VesselStatic* out) {
  uint32_t value = 0;
  if (b.get(at,8,&value) && value != 0) { out->has_ship_type = true; out->ship_type = static_cast<uint8_t>(value); }
}
} // namespace

TypeLength type_length(uint8_t type) {
  return type >= 1 && type <= 28 ? kLengths[type] : TypeLength{0,0};
}

ParseStatus parse(const uint8_t* bytes, size_t bit_count, Message* result) {
  if (!bytes || !result || bit_count < 38 || bit_count > kMaxMessageBits) return ParseStatus::invalid_input;
  const Bits b(bytes,bit_count);
  uint32_t type = 0;
  b.get(0,6,&type);
  if (type < 1 || type > 28) return ParseStatus::invalid_type;
  const TypeLength allowed = type_length(static_cast<uint8_t>(type));
  if (bit_count < allowed.minimum || bit_count > allowed.maximum) return ParseStatus::invalid_length;
  Message m{};
  m.type = static_cast<uint8_t>(type);
  m.bits = bit_count;
  uint32_t value = 0;
  b.get(6,2,&value); m.repeat = static_cast<uint8_t>(value);
  b.get(8,30,&m.source_id);

  switch (type) {
    case 1: case 2: case 3:
      m.coverage = Coverage::position;
      position(b,61,28,89,27,600000,&m);
      motion(b,50,116,128,&m);
      if (b.get(38,4,&value) && value != 15) { m.has_nav_status = true; m.nav_status = static_cast<uint8_t>(value); }
      break;
    case 4:   // Base station report: UTC date and time (not decoded), then accuracy, longitude, latitude.
      m.coverage = Coverage::base_station;
      position(b,79,28,107,27,600000,&m);
      break;
    case 5:   // Static and voyage related data.
      m.coverage = Coverage::static_voyage;
      b.get(40,30,&m.vessel.imo);
      b.text(70,7,m.vessel.callsign,sizeof(m.vessel.callsign));
      b.text(112,20,m.vessel.name,sizeof(m.vessel.name));
      ship_type(b,232,&m.vessel);
      dimensions(b,240,&m.vessel);
      b.text(302,20,m.vessel.destination,sizeof(m.vessel.destination));
      m.vessel.has_callsign = m.vessel.callsign[0] != 0;
      m.vessel.has_name = m.vessel.name[0] != 0;
      m.vessel.has_destination = m.vessel.destination[0] != 0;
      break;
    case 9:   // Standard SAR aircraft position report. Speed is in whole knots here (1023 = not available).
      m.coverage = Coverage::sar_aircraft;
      position(b,61,28,89,27,600000,&m);
      if (b.get(50,10,&value) && value != 1023) {
        m.has_speed = true; m.speed_tenths_kn = static_cast<uint16_t>(value * 10);
      }
      if (b.get(116,12,&value) && value < 3600) {
        m.has_course = true; m.course_tenths_deg = static_cast<uint16_t>(value);
      }
      break;
    case 18: case 19:
      m.coverage = Coverage::position;
      position(b,57,28,85,27,600000,&m);
      motion(b,46,112,124,&m);
      if (type == 19) {   // Extended Class B report also carries name, ship type and dimensions.
        b.text(143,20,m.vessel.name,sizeof(m.vessel.name));
        m.vessel.has_name = m.vessel.name[0] != 0;
        ship_type(b,263,&m.vessel);
        dimensions(b,271,&m.vessel);
      }
      break;
    case 24: {   // Static data report in two parts.
      uint32_t part = 0;
      b.get(38,2,&part);
      if (part == 0 && bit_count >= 160) {
        m.coverage = Coverage::static_b;
        m.vessel.part = 0;
        b.text(40,20,m.vessel.name,sizeof(m.vessel.name));
        m.vessel.has_name = m.vessel.name[0] != 0;
      } else if (part == 1 && bit_count >= 168) {
        m.coverage = Coverage::static_b;
        m.vessel.part = 1;
        ship_type(b,40,&m.vessel);
        b.text(90,7,m.vessel.callsign,sizeof(m.vessel.callsign));
        m.vessel.has_callsign = m.vessel.callsign[0] != 0;
        // An auxiliary craft (MMSI 98xxxxxxx) sends its mother ship's MMSI in these 30 bits, not dimensions.
        if (m.source_id / 10000000u != 98u) dimensions(b,132,&m.vessel);
      }
      break;   // parts 2 and 3 are not defined: header only
    }
    case 21: {
      m.coverage = Coverage::aid21;
      b.get(38,5,&value); m.aid.aid_type = static_cast<uint8_t>(value);
      b.text(43,20,m.aid.name,sizeof(m.aid.name));
      position(b,164,28,192,27,600000,&m);
      b.get(269,1,&value); m.aid.is_virtual = value != 0; m.aid.virtual_flag_valid = true;
      // Optional 6-bit name extension begins at bit 272. Pad bits (if any) are not text.
      if (bit_count > 272) {
        char extra[16]{};
        b.text(272,(bit_count-272)/6,extra,sizeof(extra));
        const size_t used = std::strlen(m.aid.name);
        const size_t room = sizeof(m.aid.name)-used-1;
        std::strncat(m.aid.name,extra,room);
      }
      break;
    }
    case 27:
      m.coverage = Coverage::long_range27;
      position(b,44,18,62,17,600,&m);
      b.get(79,6,&value); if (value != 63) { m.has_speed = true; m.speed_tenths_kn = static_cast<uint16_t>(value * 10); }
      b.get(85,9,&value); if (value != 511 && value <= 359) {
        m.has_course = true; m.course_tenths_deg = static_cast<uint16_t>(value * 10);
      }
      break;
    case 28:
      m.coverage = Coverage::aid28;
      b.get(38,6,&value); m.aid.timestamp = static_cast<uint8_t>(value);
      position(b,44,28,72,27,600000,&m);
      b.get(99,2,&value); m.aid.restricted_use = static_cast<uint8_t>(value);
      b.get(101,3,&value); m.aid.station_type = static_cast<uint8_t>(value);
      b.get(104,7,&value); m.aid.aid_type = static_cast<uint8_t>(value);
      b.get(111,17,&m.aid.iala_mrn);
      b.get(128,4,&value); m.aid.dimension_type = static_cast<uint8_t>(value);
      b.get(132,9,&value); m.aid.dimension_a = static_cast<uint16_t>(value);
      b.get(141,11,&value); m.aid.dimension_b = static_cast<uint16_t>(value);
      b.get(152,1,&value); m.aid.additional_data = value != 0;
      b.get(153,1,&value); m.aid.charted = value != 0;
      b.get(154,4,&value); m.aid.on_station = static_cast<uint8_t>(value);
      b.get(158,8,&value); m.aid.status = static_cast<uint8_t>(value);
      b.get(167,1,&value); m.aid.authenticated = value != 0;
      break;
    default: break;  // Preserved as header_only; do not advertise full field coverage.
  }
  *result = m;
  return ParseStatus::ok;
}
} // namespace orcsdr::ais
