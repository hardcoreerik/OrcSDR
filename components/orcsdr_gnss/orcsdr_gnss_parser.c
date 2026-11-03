#include "orcsdr_gnss_parser.h"

#include <string.h>

#define MAX_FIELDS 24

static bool digit(char value) { return value >= '0' && value <= '9'; }

static bool parse_uint(const char* text, size_t length, unsigned* value)
{
    unsigned parsed = 0;
    if (!length || !value) return false;
    for (size_t i = 0; i < length; ++i) {
        if (!digit(text[i])) return false;
        parsed = parsed * 10u + (unsigned)(text[i] - '0');
    }
    *value = parsed;
    return true;
}

static int hex_value(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    return -1;
}

static bool parse_time(const char* text, size_t length, orcsdr_gnss_result_t* out)
{
    unsigned hour, minute, second;
    if (length < 6 || !parse_uint(text, 2, &hour) ||
        !parse_uint(text + 2, 2, &minute) || !parse_uint(text + 4, 2, &second) ||
        hour > 23 || minute > 59 || second > 59) return false;

    unsigned millisecond = 0;
    if (length > 6) {
        if (text[6] != '.' || length == 7) return false;
        unsigned scale = 100;
        for (size_t i = 7; i < length; ++i) {
            if (!digit(text[i])) return false;
            if (i < 10) {
                millisecond += (unsigned)(text[i] - '0') * scale;
                scale /= 10;
            }
        }
        // Extra fractional digits are validated but truncated to millisecond precision.
    }
    out->utc_valid = true;
    out->hour = (uint8_t)hour;
    out->minute = (uint8_t)minute;
    out->second = (uint8_t)second;
    out->millisecond = (uint16_t)millisecond;
    return true;
}

static bool parse_date(const char* text, size_t length, orcsdr_gnss_result_t* out)
{
    unsigned day, month, year;
    if (length != 6 || !parse_uint(text, 2, &day) ||
        !parse_uint(text + 2, 2, &month) || !parse_uint(text + 4, 2, &year) ||
        month < 1 || month > 12 || day < 1) return false;
    static const uint8_t month_days[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (day > month_days[month - 1]) return false;
    if (month == 2 && day == 29 && year != 0 && year % 4 != 0) return false;
    out->date_valid = true;
    out->day = (uint8_t)day;
    out->month = (uint8_t)month;
    out->year_2digit = (uint8_t)year;
    return true;
}

static bool parse_coordinate(const char* text, size_t length, bool latitude,
                             char hemisphere, int32_t* coordinate)
{
    const size_t degree_digits = latitude ? 2u : 3u;
    if (!length || !coordinate || length < degree_digits + 2) return false;
    unsigned degrees;
    if (!parse_uint(text, degree_digits, &degrees) ||
        !digit(text[degree_digits]) || !digit(text[degree_digits + 1])) return false;
    unsigned minute_whole = (unsigned)(text[degree_digits] - '0') * 10u +
                            (unsigned)(text[degree_digits + 1] - '0');
    size_t pos = degree_digits + 2;
    uint64_t fraction = 0, scale = 1;
    if (pos < length) {
        if (text[pos++] != '.' || pos == length) return false;
        size_t fraction_digits = length - pos;
        if (fraction_digits > 9) return false;
        for (; pos < length; ++pos) {
            if (!digit(text[pos])) return false;
            fraction = fraction * 10u + (unsigned)(text[pos] - '0');
            scale *= 10u;
        }
    }
    const unsigned maximum = latitude ? 90u : 180u;
    if (minute_whole >= 60 || degrees > maximum ||
        (degrees == maximum && (minute_whole != 0 || fraction != 0))) return false;
    if ((latitude && hemisphere != 'N' && hemisphere != 'S') ||
        (!latitude && hemisphere != 'E' && hemisphere != 'W')) return false;

    const uint64_t minute_units = (uint64_t)minute_whole * scale + fraction;
    const uint64_t denominator = 60u * scale;
    const uint64_t minute_e7 = (minute_units * 10000000u + denominator / 2u) / denominator;
    int64_t value = (int64_t)degrees * 10000000 + (int64_t)minute_e7;
    if (hemisphere == 'S' || hemisphere == 'W') value = -value;
    *coordinate = (int32_t)value;
    return true;
}

static bool field_nonempty(const char* field) { return field[0] != '\0'; }

static bool parse_optional_time(const char* field, orcsdr_gnss_result_t* out)
{
    return !field_nonempty(field) || parse_time(field, strlen(field), out);
}

static bool parse_pair(const char* lat, const char* ns, const char* lon, const char* ew,
                       bool required, orcsdr_gnss_result_t* out)
{
    const bool any = field_nonempty(lat) || field_nonempty(ns) ||
                     field_nonempty(lon) || field_nonempty(ew);
    if (!any) return !required;
    if (!field_nonempty(lat) || !field_nonempty(ns) || !field_nonempty(lon) ||
        !field_nonempty(ew) || strlen(ns) != 1 || strlen(ew) != 1 ||
        !parse_coordinate(lat, strlen(lat), true, ns[0], &out->latitude_e7) ||
        !parse_coordinate(lon, strlen(lon), false, ew[0], &out->longitude_e7)) return false;
    return true;
}

static orcsdr_gnss_feed_result_t parse_sentence(char* sentence, size_t length,
                                                orcsdr_gnss_result_t* out)
{
    size_t star = length;
    uint8_t checksum = 0;
    for (size_t i = 0; i < length; ++i) {
        if (sentence[i] == '*') {
            if (star != length) return ORCSDR_GNSS_REJECTED;
            star = i;
        } else if (star == length) {
            checksum ^= (uint8_t)sentence[i];
        }
    }
    if (star == length || star + 3 != length) return ORCSDR_GNSS_REJECTED;
    const int high = hex_value(sentence[star + 1]), low = hex_value(sentence[star + 2]);
    if (high < 0 || low < 0 || checksum != (uint8_t)((high << 4) | low))
        return ORCSDR_GNSS_REJECTED;
    sentence[star] = '\0';

    char* fields[MAX_FIELDS];
    size_t field_count = 1;
    fields[0] = sentence;
    for (char* p = sentence; *p; ++p) {
        if (*p == ',') {
            *p = '\0';
            if (field_count == MAX_FIELDS) return ORCSDR_GNSS_REJECTED;
            fields[field_count++] = p + 1;
        }
    }
    if (strlen(fields[0]) != 5 || fields[0][0] != 'G' ||
        (fields[0][1] != 'P' && fields[0][1] != 'N' && fields[0][1] != 'L'))
        return ORCSDR_GNSS_IGNORED;
    const bool rmc = strcmp(fields[0] + 2, "RMC") == 0;
    const bool gga = strcmp(fields[0] + 2, "GGA") == 0;
    if (!rmc && !gga) return ORCSDR_GNSS_IGNORED;
    if ((rmc && field_count < 10) || (gga && field_count < 7))
        return ORCSDR_GNSS_REJECTED;

    if (!parse_optional_time(fields[1], out)) return ORCSDR_GNSS_REJECTED;
    bool fix = false;
    if (rmc) {
        if (strlen(fields[2]) != 1 || (fields[2][0] != 'A' && fields[2][0] != 'V'))
            return ORCSDR_GNSS_REJECTED;
        fix = fields[2][0] == 'A';
        // NMEA mode N/E/M/S is invalid, estimated, manual or simulated, not a GNSS fix.
        if (field_count > 12 && field_nonempty(fields[12])) {
            if (strlen(fields[12]) != 1) return ORCSDR_GNSS_REJECTED;
            const char mode=fields[12][0];
            if (mode=='N'||mode=='E'||mode=='M'||mode=='S') fix=false;
            else if (mode!='A'&&mode!='D'&&mode!='F'&&mode!='R'&&mode!='P') return ORCSDR_GNSS_REJECTED;
        }
        if (!parse_pair(fields[3], fields[4], fields[5], fields[6], fix, out))
            return ORCSDR_GNSS_REJECTED;
        if (field_nonempty(fields[9]) && !parse_date(fields[9], strlen(fields[9]), out))
            return ORCSDR_GNSS_REJECTED;
    } else {
        unsigned quality;
        if (strlen(fields[6]) != 1 || !parse_uint(fields[6], 1, &quality) || quality > 8)
            return ORCSDR_GNSS_REJECTED;
        fix = quality >= 1 && quality <= 5; // 6..8 are estimated/manual/simulated
        if (!parse_pair(fields[2], fields[3], fields[4], fields[5], fix, out))
            return ORCSDR_GNSS_REJECTED;
    }
    out->fix_valid = fix;
    if (!fix) {
        out->latitude_e7 = 0;
        out->longitude_e7 = 0;
        return ORCSDR_GNSS_NO_FIX;
    }
    return ORCSDR_GNSS_FIX;
}

void orcsdr_gnss_parser_init(orcsdr_gnss_parser_t* parser)
{
    if (parser) memset(parser, 0, sizeof(*parser));
}

void orcsdr_gnss_parser_clear(orcsdr_gnss_parser_t* parser)
{
    orcsdr_gnss_parser_init(parser);
}

orcsdr_gnss_feed_result_t orcsdr_gnss_parser_feed(
    orcsdr_gnss_parser_t* parser, uint8_t byte, orcsdr_gnss_result_t* result)
{
    if (!parser || !result) return ORCSDR_GNSS_FAILURE;
    memset(result, 0, sizeof(*result));
    if (byte == '$') {
        const bool truncated = parser->collecting && parser->length != 0;
        parser->length = 0;
        parser->collecting = true;
        parser->dropping = false;
        parser->saw_cr = false;
        return truncated ? ORCSDR_GNSS_REJECTED : ORCSDR_GNSS_INCOMPLETE;
    }
    if (parser->dropping) return ORCSDR_GNSS_INCOMPLETE;
    if (!parser->collecting) return ORCSDR_GNSS_INCOMPLETE;
    if (parser->saw_cr) {
        parser->collecting = false;
        parser->saw_cr = false;
        if (byte == '\n') {
            parser->sentence[parser->length] = '\0';
            return parse_sentence(parser->sentence, parser->length, result);
        }
        parser->dropping = byte != '$';
        return ORCSDR_GNSS_REJECTED;
    }
    if (byte == '\r') {
        parser->saw_cr = true;
        return ORCSDR_GNSS_INCOMPLETE;
    }
    if (byte == '\n' || byte < 0x20 || byte > 0x7e) {
        parser->collecting = false;
        parser->dropping = true;
        return ORCSDR_GNSS_REJECTED;
    }
    if (parser->length >= sizeof(parser->sentence) - 1) {
        parser->collecting = false;
        parser->dropping = true;
        return ORCSDR_GNSS_REJECTED;
    }
    parser->sentence[parser->length++] = (char)byte;
    return ORCSDR_GNSS_INCOMPLETE;
}
