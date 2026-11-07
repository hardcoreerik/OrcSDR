#pragma once
/* PCF8563 real-time clock: register layout and time conversion. Written from the NXP PCF8563 datasheet
 * (register map, BCD fields, VL and century bits). No vendor driver code is used.
 *
 * Pure functions with no I2C or RTOS dependency so the rules can be tested on the host.
 * The chip only holds years 2000-2099 in this use (century bit clear) and counts whole seconds. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PCF8563_REG_CONTROL1 0x00u
#define PCF8563_REG_CONTROL2 0x01u
#define PCF8563_REG_SECONDS 0x02u /* seven time registers follow, seconds first */
#define PCF8563_TIME_LEN 7u

/* Control 1 bits that must be clear for normal running: TEST1 (7), STOP (5), TESTC (3). */
#define PCF8563_CONTROL1_NORMAL_MASK 0xA8u

#define PCF8563_VL_BIT 0x80u      /* seconds register: oscillator stopped or supply dipped, time is not trustworthy */
#define PCF8563_CENTURY_BIT 0x80u /* month register: set means 19xx, which this driver rejects */

/* Same acceptance window as the Tab5 clock service: 2024-01-01 up to but not including 2100-01-01. */
#define PCF8563_MIN_UTC 1704067200u
#define PCF8563_MAX_UTC 4102444800u

typedef struct {
    uint16_t year; /* 2000..2099 */
    uint8_t month; /* 1..12 */
    uint8_t day;   /* 1..31, valid for the month */
    uint8_t weekday; /* 0 = Sunday */
    uint8_t hour, minute, second;
    bool low_voltage; /* VL flag read from the chip; ignored when encoding */
} pcf8563_time_t;

static inline bool pcf8563_bcd_ok(uint8_t v) { return (v & 0x0Fu) <= 9u && (v >> 4) <= 9u; }
static inline uint8_t pcf8563_from_bcd(uint8_t v) { return (uint8_t)((v >> 4) * 10u + (v & 0x0Fu)); }
static inline uint8_t pcf8563_to_bcd(uint8_t v) { return (uint8_t)(((v / 10u) << 4) | (v % 10u)); }

static inline bool pcf8563_leap(unsigned year) { return (year % 4u == 0u && year % 100u != 0u) || year % 400u == 0u; }

static inline unsigned pcf8563_days_in_month(unsigned year, unsigned month)
{
    static const uint8_t days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1u || month > 12u) return 0u;
    return (month == 2u && pcf8563_leap(year)) ? 29u : days[month - 1u];
}

/* Days since 1970-01-01 for a date in 1970..2099. */
static inline uint32_t pcf8563_days_since_epoch(unsigned year, unsigned month, unsigned day)
{
    uint32_t days = 0u;
    for (unsigned y = 1970u; y < year; y++) days += pcf8563_leap(y) ? 366u : 365u;
    for (unsigned m = 1u; m < month; m++) days += pcf8563_days_in_month(year, m);
    return days + (day - 1u);
}

/* Decode the seven time registers (seconds..years). False for any field that is not valid BCD or in range,
 * for an impossible calendar date, or when the century bit is set. The VL flag does not fail the decode; it is
 * reported in low_voltage so the caller can decide to distrust the time. */
static inline bool pcf8563_decode(const uint8_t *raw, size_t n, pcf8563_time_t *out)
{
    if (!raw || !out || n != PCF8563_TIME_LEN) return false;
    const uint8_t sec = raw[0] & 0x7Fu, min = raw[1] & 0x7Fu, hour = raw[2] & 0x3Fu;
    const uint8_t day = raw[3] & 0x3Fu, wday = raw[4] & 0x07u, mon = raw[5] & 0x1Fu, yr = raw[6];
    if (raw[5] & PCF8563_CENTURY_BIT) return false;
    if (!pcf8563_bcd_ok(sec) || !pcf8563_bcd_ok(min) || !pcf8563_bcd_ok(hour) || !pcf8563_bcd_ok(day) ||
        !pcf8563_bcd_ok(mon) || !pcf8563_bcd_ok(yr))
        return false;
    pcf8563_time_t t = {.year = (uint16_t)(2000u + pcf8563_from_bcd(yr)), .month = pcf8563_from_bcd(mon),
                        .day = pcf8563_from_bcd(day), .weekday = wday, .hour = pcf8563_from_bcd(hour),
                        .minute = pcf8563_from_bcd(min), .second = pcf8563_from_bcd(sec),
                        .low_voltage = (raw[0] & PCF8563_VL_BIT) != 0u};
    if (t.second > 59u || t.minute > 59u || t.hour > 23u || t.weekday > 6u) return false;
    if (t.month < 1u || t.month > 12u || t.day < 1u || t.day > pcf8563_days_in_month(t.year, t.month)) return false;
    *out = t;
    return true;
}

/* Encode into the seven time registers. VL is written clear, which is how a valid time is established. */
static inline void pcf8563_encode(const pcf8563_time_t *t, uint8_t *raw)
{
    raw[0] = pcf8563_to_bcd(t->second) & 0x7Fu;
    raw[1] = pcf8563_to_bcd(t->minute);
    raw[2] = pcf8563_to_bcd(t->hour);
    raw[3] = pcf8563_to_bcd(t->day);
    raw[4] = (uint8_t)(t->weekday % 7u);
    raw[5] = pcf8563_to_bcd(t->month);
    raw[6] = pcf8563_to_bcd((uint8_t)(t->year % 100u));
}

/* UTC epoch seconds -> calendar. False outside the accepted window. */
static inline bool pcf8563_from_epoch(uint32_t epoch, pcf8563_time_t *out)
{
    if (!out || epoch < PCF8563_MIN_UTC || epoch >= PCF8563_MAX_UTC) return false;
    uint32_t days = epoch / 86400u;
    const uint32_t rem = epoch % 86400u;
    const uint8_t weekday = (uint8_t)((days + 4u) % 7u); /* 1970-01-01 was a Thursday */
    unsigned year = 1970u;
    for (;;) {
        const uint32_t len = pcf8563_leap(year) ? 366u : 365u;
        if (days < len) break;
        days -= len;
        year++;
    }
    unsigned month = 1u;
    while (days >= pcf8563_days_in_month(year, month)) {
        days -= pcf8563_days_in_month(year, month);
        month++;
    }
    out->year = (uint16_t)year;
    out->month = (uint8_t)month;
    out->day = (uint8_t)(days + 1u);
    out->weekday = weekday;
    out->hour = (uint8_t)(rem / 3600u);
    out->minute = (uint8_t)((rem % 3600u) / 60u);
    out->second = (uint8_t)(rem % 60u);
    out->low_voltage = false;
    return true;
}

/* Calendar -> UTC epoch seconds. False outside the accepted window. */
static inline bool pcf8563_to_epoch(const pcf8563_time_t *t, uint32_t *out)
{
    if (!t || !out || t->year < 2000u || t->year > 2099u) return false;
    const uint32_t days = pcf8563_days_since_epoch(t->year, t->month, t->day);
    const uint64_t epoch = (uint64_t)days * 86400u + (uint64_t)t->hour * 3600u + (uint64_t)t->minute * 60u + t->second;
    if (epoch < PCF8563_MIN_UTC || epoch >= PCF8563_MAX_UTC) return false;
    *out = (uint32_t)epoch;
    return true;
}

#ifdef __cplusplus
}
#endif
