/* Host test for the GNSS time rules (orcsdr_gnss_time.h). */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "orcsdr_gnss_time.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

static orcsdr_gnss_result_t result(unsigned y, unsigned mo, unsigned d, unsigned h, unsigned mi, unsigned s, unsigned ms)
{
    orcsdr_gnss_result_t r;
    memset(&r, 0, sizeof(r));
    r.fix_valid = r.utc_valid = r.date_valid = true;
    r.year_2digit = (uint8_t)y; r.month = (uint8_t)mo; r.day = (uint8_t)d;
    r.hour = (uint8_t)h; r.minute = (uint8_t)mi; r.second = (uint8_t)s; r.millisecond = (uint16_t)ms;
    return r;
}

static int64_t reference(int y, int mo, int d, int h, int mi, int s)
{
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d; t.tm_hour = h; t.tm_min = mi; t.tm_sec = s;
#ifdef _WIN32
    return (int64_t)_mkgmtime(&t);
#else
    return (int64_t)timegm(&t);
#endif
}

static orcsdr_gnss_time_input_t base(void)
{
    orcsdr_gnss_time_input_t in;
    memset(&in, 0, sizeof(in));
    in.enabled = true;
    in.clock_valid = true;
    in.clock_us = 1794070000LL * 1000000LL;
    in.gps_us = in.clock_us + 5000000LL;     /* GPS is 5 s ahead of the clock */
    in.have_previous = true;
    in.label_step_us = 1000000;
    in.arrival_step_us = 1010000;
    return in;
}

static void conversion(void)
{
    int64_t us = 0;
    orcsdr_gnss_result_t r = result(26, 11, 7, 13, 45, 9, 250);
    CHECK(orcsdr_gnss_time_from_result(&r, &us));
    CHECK(us == reference(2026, 11, 7, 13, 45, 9) * 1000000LL + 250000LL);

    const int dates[][6] = {{2024, 1, 1, 0, 0, 0}, {2024, 2, 29, 23, 59, 59}, {2028, 12, 31, 12, 0, 1}, {2099, 12, 31, 23, 59, 59}};
    for (unsigned i = 0; i < sizeof(dates) / sizeof(dates[0]); i++) {
        r = result((unsigned)dates[i][0] - 2000u, (unsigned)dates[i][1], (unsigned)dates[i][2], (unsigned)dates[i][3],
                   (unsigned)dates[i][4], (unsigned)dates[i][5], 0);
        CHECK(orcsdr_gnss_time_from_result(&r, &us));
        CHECK(us == reference(dates[i][0], dates[i][1], dates[i][2], dates[i][3], dates[i][4], dates[i][5]) * 1000000LL);
    }

    r = result(26, 11, 7, 13, 45, 9, 0); r.fix_valid = false; CHECK(!orcsdr_gnss_time_from_result(&r, &us));
    r = result(26, 11, 7, 13, 45, 9, 0); r.utc_valid = false;  CHECK(!orcsdr_gnss_time_from_result(&r, &us));
    r = result(26, 11, 7, 13, 45, 9, 0); r.date_valid = false; CHECK(!orcsdr_gnss_time_from_result(&r, &us));
    r = result(23, 12, 31, 23, 59, 59, 0); CHECK(!orcsdr_gnss_time_from_result(&r, &us));   /* before 2024 */
    r = result(26, 2, 29, 0, 0, 0, 0);     CHECK(!orcsdr_gnss_time_from_result(&r, &us));   /* 2026 is not a leap year */
    r = result(26, 4, 31, 0, 0, 0, 0);     CHECK(!orcsdr_gnss_time_from_result(&r, &us));
    r = result(26, 13, 1, 0, 0, 0, 0);     CHECK(!orcsdr_gnss_time_from_result(&r, &us));
    r = result(26, 11, 7, 24, 0, 0, 0);    CHECK(!orcsdr_gnss_time_from_result(&r, &us));
    r = result(26, 11, 7, 13, 60, 0, 0);   CHECK(!orcsdr_gnss_time_from_result(&r, &us));
    r = result(26, 11, 7, 13, 45, 60, 0);  CHECK(!orcsdr_gnss_time_from_result(&r, &us));   /* leap second is not accepted */
    r = result(26, 11, 7, 13, 45, 9, 1000); CHECK(!orcsdr_gnss_time_from_result(&r, &us));
    CHECK(!orcsdr_gnss_time_from_result(NULL, &us));
    CHECK(!orcsdr_gnss_time_from_result(&r, NULL));
}

static void decisions(void)
{
    orcsdr_gnss_time_input_t in = base();
    CHECK(orcsdr_gnss_time_decide(&in) == ORCSDR_GNSS_TIME_APPLY);

    in = base(); in.enabled = false;        CHECK(orcsdr_gnss_time_decide(&in) == ORCSDR_GNSS_TIME_SKIP_DISABLED);
    in = base(); in.network_fresh = true;   CHECK(orcsdr_gnss_time_decide(&in) == ORCSDR_GNSS_TIME_SKIP_NETWORK);
    in = base(); in.network_fresh = true; in.enabled = false;
    CHECK(orcsdr_gnss_time_decide(&in) == ORCSDR_GNSS_TIME_SKIP_DISABLED);   /* disabled wins: nothing runs */
    in = base(); in.have_previous = false;  CHECK(orcsdr_gnss_time_decide(&in) == ORCSDR_GNSS_TIME_SKIP_UNCONFIRMED);

    in = base(); in.label_step_us = 1000000; in.arrival_step_us = 3100000;      /* 2.1 s apart from the local clock */
    CHECK(orcsdr_gnss_time_decide(&in) == ORCSDR_GNSS_TIME_SKIP_INCONSISTENT);
    in = base(); in.label_step_us = 1000000; in.arrival_step_us = 3000000;      /* exactly 2 s: still agrees */
    CHECK(orcsdr_gnss_time_decide(&in) == ORCSDR_GNSS_TIME_APPLY);
    in = base(); in.label_step_us = 90000000; in.arrival_step_us = 1000000;     /* a jump in the stamps */
    CHECK(orcsdr_gnss_time_decide(&in) == ORCSDR_GNSS_TIME_SKIP_INCONSISTENT);
    in = base(); in.arrival_step_us = 0;    CHECK(orcsdr_gnss_time_decide(&in) == ORCSDR_GNSS_TIME_SKIP_INCONSISTENT);

    in = base(); in.gps_us = in.clock_us + ORCSDR_GNSS_TIME_MAX_STEP_US + 1;
    CHECK(orcsdr_gnss_time_decide(&in) == ORCSDR_GNSS_TIME_SKIP_TOO_FAR);
    in = base(); in.gps_us = in.clock_us - ORCSDR_GNSS_TIME_MAX_STEP_US - 1;
    CHECK(orcsdr_gnss_time_decide(&in) == ORCSDR_GNSS_TIME_SKIP_TOO_FAR);
    in = base(); in.gps_us = in.clock_us + ORCSDR_GNSS_TIME_MAX_STEP_US;       /* the limit itself is allowed */
    CHECK(orcsdr_gnss_time_decide(&in) == ORCSDR_GNSS_TIME_APPLY);

    in = base(); in.gps_us = in.clock_us + 999999;  CHECK(orcsdr_gnss_time_decide(&in) == ORCSDR_GNSS_TIME_SKIP_CLOSE);
    in = base(); in.gps_us = in.clock_us - 999999;  CHECK(orcsdr_gnss_time_decide(&in) == ORCSDR_GNSS_TIME_SKIP_CLOSE);
    in = base(); in.gps_us = in.clock_us + 1000000; CHECK(orcsdr_gnss_time_decide(&in) == ORCSDR_GNSS_TIME_APPLY);

    in = base(); in.applied_before = true; in.since_applied_us = 59999999;
    CHECK(orcsdr_gnss_time_decide(&in) == ORCSDR_GNSS_TIME_SKIP_TOO_SOON);
    in = base(); in.applied_before = true; in.since_applied_us = 60000000;
    CHECK(orcsdr_gnss_time_decide(&in) == ORCSDR_GNSS_TIME_APPLY);

    /* An unset clock takes the GPS time whatever the distance, once two sentences agree, and is not rate limited. */
    in = base(); in.clock_valid = false; in.clock_us = 0; in.applied_before = true; in.since_applied_us = 1;
    CHECK(orcsdr_gnss_time_decide(&in) == ORCSDR_GNSS_TIME_APPLY);
    in = base(); in.clock_valid = false; in.have_previous = false;
    CHECK(orcsdr_gnss_time_decide(&in) == ORCSDR_GNSS_TIME_SKIP_UNCONFIRMED);
}

int main(void)
{
    conversion();
    decisions();
    if (failures) { printf("p4_gnss_time_test: %d failure(s)\n", failures); return 1; }
    printf("p4_gnss_time_test: PASS\n");
    return 0;
}
