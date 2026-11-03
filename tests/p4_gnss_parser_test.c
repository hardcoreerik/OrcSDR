#include "orcsdr_gnss_parser.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static orcsdr_gnss_parser_t parser;
static orcsdr_gnss_result_t result;

static size_t sentence(const char* body, char* output, size_t capacity)
{
    unsigned checksum = 0;
    for (const char* p = body; *p; ++p) checksum ^= (unsigned char)*p;
    const int count = snprintf(output, capacity, "$%s*%02X\r\n", body, checksum);
    assert(count > 0 && (size_t)count < capacity);
    return (size_t)count;
}

static orcsdr_gnss_feed_result_t feed(const char* bytes, size_t length)
{
    orcsdr_gnss_feed_result_t last = ORCSDR_GNSS_INCOMPLETE;
    for (size_t i = 0; i < length; ++i)
        last = orcsdr_gnss_parser_feed(&parser, (uint8_t)bytes[i], &result);
    return last;
}

static orcsdr_gnss_feed_result_t feed_body(const char* body)
{
    char line[256];
    return feed(line, sentence(body, line, sizeof(line)));
}

static void accepted_rmc_and_gga(void)
{
    orcsdr_gnss_parser_init(&parser);
    assert(feed_body("GPRMC,123519.250,A,4807.038,N,01131.000,E,0.0,0.0,230394,,,A") ==
           ORCSDR_GNSS_FIX);
    assert(result.fix_valid && result.latitude_e7 == 481173000);
    assert(result.longitude_e7 == 115166667);
    assert(result.utc_valid && result.hour == 12 && result.minute == 35 &&
           result.second == 19 && result.millisecond == 250);
    assert(result.date_valid && result.day == 23 && result.month == 3 &&
           result.year_2digit == 94);

    assert(feed_body("GLGGA,235959.9999,3450.000,S,05822.500,W,1,08,0.9,10.0,M,0.0,M,,") ==
           ORCSDR_GNSS_FIX);
    assert(result.fix_valid && result.latitude_e7 == -348333333);
    assert(result.longitude_e7 == -583750000);
    assert(result.utc_valid && result.hour == 23 && result.minute == 59 &&
           result.second == 59 && result.millisecond == 999);
    assert(!result.date_valid && result.day == 0 && result.year_2digit == 0);
}

static void no_fix_clears_live_result(void)
{
    orcsdr_gnss_parser_init(&parser);
    assert(feed_body("GNRMC,120000,A,3745.000,N,12225.000,W,0,0,010126,,,A") ==
           ORCSDR_GNSS_FIX);
    assert(result.fix_valid);
    assert(feed_body("GPGGA,120001,,,,,0,00,99.9,,,,,,") == ORCSDR_GNSS_NO_FIX);
    assert(!result.fix_valid && result.latitude_e7 == 0 && result.longitude_e7 == 0);
    assert(result.utc_valid && result.second == 1 && !result.date_valid);
    assert(feed_body("GPRMC,,V,,,,,,,010126,,,") == ORCSDR_GNSS_NO_FIX);
    assert(!result.fix_valid && !result.utc_valid && result.hour == 0);
    assert(result.date_valid && result.day == 1 && result.month == 1 && result.year_2digit == 26);
}

static void checksum_fields_and_ranges_rejected(void)
{
    char line[256];
    const size_t length = sentence("GPRMC,123519,A,4807.038,N,01131.000,E,0,0,230394,,,A",
                                   line, sizeof(line));
    line[length - 4] = line[length - 4] == '0' ? '1' : '0';
    orcsdr_gnss_parser_init(&parser);
    assert(feed(line, length) == ORCSDR_GNSS_REJECTED);

    assert(feed_body("GPRMC,123519,A,4860.000,N,01131.000,E,0,0,230394,,,A") ==
           ORCSDR_GNSS_REJECTED);
    assert(feed_body("GPRMC,123519,A,9000.001,N,01131.000,E,0,0,230394,,,A") ==
           ORCSDR_GNSS_REJECTED);
    assert(feed_body("GPRMC,123519,A,4807.038,X,01131.000,E,0,0,230394,,,A") ==
           ORCSDR_GNSS_REJECTED);
    assert(feed_body("GPRMC,123519,A,4807.038,N,18100.000,E,0,0,230394,,,A") ==
           ORCSDR_GNSS_REJECTED);
    assert(feed_body("GPRMC,246000,A,4807.038,N,01131.000,E,0,0,230394,,,A") ==
           ORCSDR_GNSS_REJECTED);
    assert(feed_body("GPRMC,123519,A,4807.038,N,01131.000,E,0,0,320294,,,A") ==
           ORCSDR_GNSS_REJECTED);
    assert(feed_body("GPRMC,123519,A,4807.038,N,01131.000,E,0,0,290294,,,A") ==
           ORCSDR_GNSS_REJECTED);
    assert(feed_body("GPGGA,123519,4807.038,N,01131.000,E,9,08,0.9,,,,,,") ==
           ORCSDR_GNSS_REJECTED);
}

static void boundaries_chunks_and_resynchronization(void)
{
    char line[256];
    const size_t length = sentence("GNGGA,010203,9000.000,N,18000.000,E,1,05,1.0,,,,,,",
                                   line, sizeof(line));
    orcsdr_gnss_parser_init(&parser);
    for (size_t i = 0; i < length; ++i) {
        const orcsdr_gnss_feed_result_t status =
            orcsdr_gnss_parser_feed(&parser, (uint8_t)line[i], &result);
        assert(status == (i + 1 == length ? ORCSDR_GNSS_FIX : ORCSDR_GNSS_INCOMPLETE));
    }
    assert(result.latitude_e7 == 900000000 && result.longitude_e7 == 1800000000);

    orcsdr_gnss_parser_clear(&parser);
    feed("noise and $GPRMC,broken", 23);
    assert(feed("$", 1) == ORCSDR_GNSS_REJECTED);
    assert(feed(line, length) == ORCSDR_GNSS_FIX); // a new '$' restarts an incomplete sentence

    orcsdr_gnss_parser_clear(&parser);
    (void)orcsdr_gnss_parser_feed(&parser, '$', &result);
    for (size_t i = 0; i < ORCSDR_GNSS_SENTENCE_CAPACITY + 5; ++i) {
        const orcsdr_gnss_feed_result_t status =
            orcsdr_gnss_parser_feed(&parser, 'x', &result);
        if (i == ORCSDR_GNSS_SENTENCE_CAPACITY - 1)
            assert(status == ORCSDR_GNSS_REJECTED);
    }
    assert(feed(line, length) == ORCSDR_GNSS_FIX); // oversized noise cannot poison the next line
}

int main(void)
{
    assert(orcsdr_gnss_parser_feed(NULL, '$', &result) == ORCSDR_GNSS_FAILURE);
    assert(orcsdr_gnss_parser_feed(&parser, '$', NULL) == ORCSDR_GNSS_FAILURE);
    accepted_rmc_and_gga();
    no_fix_clears_live_result();
    checksum_fields_and_ranges_rejected();
    boundaries_chunks_and_resynchronization();
    assert(feed_body("GPRMC,123519,A,4807.038,N,01131.000,E,0,0,230394,,,N")==ORCSDR_GNSS_NO_FIX);
    assert(!result.fix_valid && result.latitude_e7==0);
    assert(feed_body("GPGGA,123519,4807.038,N,01131.000,E,6,08,1.0,,,,,,")==ORCSDR_GNSS_NO_FIX);
    assert(!result.fix_valid && result.longitude_e7==0);
    puts("P4_GNSS_PARSER_TEST pass");
    return 0;
}
