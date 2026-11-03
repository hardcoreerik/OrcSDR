#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ORCSDR_GNSS_SENTENCE_CAPACITY 128

typedef struct {
    char sentence[ORCSDR_GNSS_SENTENCE_CAPACITY];
    size_t length;
    bool collecting;
    bool dropping;
    bool saw_cr;
} orcsdr_gnss_parser_t;

typedef struct {
    bool fix_valid;
    int32_t latitude_e7;
    int32_t longitude_e7;
    bool utc_valid;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint16_t millisecond;
    bool date_valid;
    uint8_t day;
    uint8_t month;
    uint8_t year_2digit;
} orcsdr_gnss_result_t;

typedef enum {
    ORCSDR_GNSS_INCOMPLETE = 0,
    ORCSDR_GNSS_FIX,
    ORCSDR_GNSS_NO_FIX,
    ORCSDR_GNSS_IGNORED,
    ORCSDR_GNSS_REJECTED,
    ORCSDR_GNSS_FAILURE
} orcsdr_gnss_feed_result_t;

void orcsdr_gnss_parser_init(orcsdr_gnss_parser_t* parser);
void orcsdr_gnss_parser_clear(orcsdr_gnss_parser_t* parser);
/* Feed one byte. Results are sentence events; NO_FIX explicitly clears live fix. */
orcsdr_gnss_feed_result_t orcsdr_gnss_parser_feed(
    orcsdr_gnss_parser_t* parser, uint8_t byte, orcsdr_gnss_result_t* result);

#ifdef __cplusplus
}
#endif
