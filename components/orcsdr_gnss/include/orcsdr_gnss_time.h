#pragma once
/* GNSS time: turn an RMC fix into UTC microseconds and decide whether to set the system clock from it.
 * Pure functions with no RTOS or I2C dependency so the rules are tested on the host.
 *
 * Accuracy: NMEA time without the PPS line is good to roughly half a second, because the sentence is
 * written after the second it describes and then travels through a UART read. That is enough for display
 * and for the battery-backed clock, not for FT8. Network time is preferred whenever it is fresh. */
#include <stdbool.h>
#include <stdint.h>

#include "orcsdr_gnss_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ORCSDR_GNSS_TIME_MIN_UTC 1704067200LL /* 2024-01-01 */
#define ORCSDR_GNSS_TIME_MAX_UTC 4102444800LL /* 2100-01-01, exclusive */
#define ORCSDR_GNSS_TIME_CONSISTENCY_US 2000000LL      /* two sentences must agree with the local clock to this */
#define ORCSDR_GNSS_TIME_MIN_STEP_US 1000000LL         /* closer than this to the clock: leave the clock alone */
#define ORCSDR_GNSS_TIME_MAX_STEP_US (366LL * 86400LL * 1000000LL) /* farther than this from a valid clock: refuse */
#define ORCSDR_GNSS_TIME_MIN_INTERVAL_US 60000000LL    /* at most one correction a minute */

static inline bool orcsdr_gnss_time_leap(unsigned y) { return (y % 4u == 0u && y % 100u != 0u) || y % 400u == 0u; }

/* UTC microseconds of the sentence's own time stamp. False unless the fix is valid and both the time and the
 * date are present and possible, with a year from 2024 to 2099 (the two-digit year is read as 20yy). */
static inline bool orcsdr_gnss_time_from_result(const orcsdr_gnss_result_t *r, int64_t *utc_us)
{
    if (!r || !utc_us || !r->fix_valid || !r->utc_valid || !r->date_valid) return false;
    if (r->hour > 23u || r->minute > 59u || r->second > 59u || r->millisecond > 999u) return false;
    if (r->year_2digit < 24u || r->year_2digit > 99u) return false;
    const unsigned year = 2000u + r->year_2digit;
    static const uint8_t mdays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (r->month < 1u || r->month > 12u || r->day < 1u) return false;
    const unsigned dim = (r->month == 2u && orcsdr_gnss_time_leap(year)) ? 29u : mdays[r->month - 1u];
    if (r->day > dim) return false;
    int64_t days = 0;
    for (unsigned y = 1970u; y < year; y++) days += orcsdr_gnss_time_leap(y) ? 366 : 365;
    for (unsigned m = 1u; m < r->month; m++) days += (m == 2u && orcsdr_gnss_time_leap(year)) ? 29 : mdays[m - 1u];
    days += (int64_t)r->day - 1;
    const int64_t seconds = days * 86400 + (int64_t)r->hour * 3600 + (int64_t)r->minute * 60 + r->second;
    if (seconds < ORCSDR_GNSS_TIME_MIN_UTC || seconds >= ORCSDR_GNSS_TIME_MAX_UTC) return false;
    *utc_us = seconds * 1000000LL + (int64_t)r->millisecond * 1000LL;
    return true;
}

typedef enum {
    ORCSDR_GNSS_TIME_SKIP_DISABLED = 0, /* the setting is off */
    ORCSDR_GNSS_TIME_SKIP_NETWORK,      /* network time is fresh and preferred */
    ORCSDR_GNSS_TIME_SKIP_UNCONFIRMED,  /* first sentence: needs a second one that agrees */
    ORCSDR_GNSS_TIME_SKIP_INCONSISTENT, /* the two sentences disagree with the local clock's progress */
    ORCSDR_GNSS_TIME_SKIP_TOO_FAR,      /* a valid clock is more than a year away: treat the GPS date as wrong */
    ORCSDR_GNSS_TIME_SKIP_CLOSE,        /* already within a second */
    ORCSDR_GNSS_TIME_SKIP_TOO_SOON,     /* corrected less than a minute ago */
    ORCSDR_GNSS_TIME_APPLY,
} orcsdr_gnss_time_decision_t;

typedef struct {
    bool enabled;
    bool network_fresh;   /* network time was synced recently */
    bool clock_valid;     /* the system clock holds a plausible date */
    int64_t clock_us;     /* system UTC now, microseconds */
    int64_t gps_us;       /* GPS UTC now: the sentence's stamp plus the time since it arrived */
    bool have_previous;   /* an earlier sentence is available for the agreement check */
    int64_t label_step_us;   /* this sentence's stamp minus the previous sentence's stamp */
    int64_t arrival_step_us; /* local monotonic time between the two sentences */
    bool applied_before;
    int64_t since_applied_us; /* monotonic time since the last GPS correction */
} orcsdr_gnss_time_input_t;

static inline int64_t orcsdr_gnss_time_abs(int64_t v) { return v < 0 ? -v : v; }

static inline orcsdr_gnss_time_decision_t orcsdr_gnss_time_decide(const orcsdr_gnss_time_input_t *in)
{
    if (!in->enabled) return ORCSDR_GNSS_TIME_SKIP_DISABLED;
    if (in->network_fresh) return ORCSDR_GNSS_TIME_SKIP_NETWORK;
    if (!in->have_previous) return ORCSDR_GNSS_TIME_SKIP_UNCONFIRMED;
    if (in->arrival_step_us <= 0 ||
        orcsdr_gnss_time_abs(in->label_step_us - in->arrival_step_us) > ORCSDR_GNSS_TIME_CONSISTENCY_US)
        return ORCSDR_GNSS_TIME_SKIP_INCONSISTENT;
    if (!in->clock_valid) return ORCSDR_GNSS_TIME_APPLY;
    const int64_t diff = orcsdr_gnss_time_abs(in->gps_us - in->clock_us);
    if (diff > ORCSDR_GNSS_TIME_MAX_STEP_US) return ORCSDR_GNSS_TIME_SKIP_TOO_FAR;
    if (diff < ORCSDR_GNSS_TIME_MIN_STEP_US) return ORCSDR_GNSS_TIME_SKIP_CLOSE;
    if (in->applied_before && in->since_applied_us < ORCSDR_GNSS_TIME_MIN_INTERVAL_US)
        return ORCSDR_GNSS_TIME_SKIP_TOO_SOON;
    return ORCSDR_GNSS_TIME_APPLY;
}

#ifdef __cplusplus
}
#endif
