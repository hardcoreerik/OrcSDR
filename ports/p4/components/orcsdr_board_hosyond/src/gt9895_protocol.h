/* SPDX-License-Identifier: MIT
 *
 * GT9895 register map and packet validation.
 *
 * Derived from SensorLib's TouchDrvGT9895 (https://github.com/lewisxhe/SensorLib,
 * src/touch/TouchDrvGT9895.cpp/.hpp), Copyright (c) 2024 Lewis He, MIT License.
 * The register addresses, packet layout and checksum rules below follow that
 * file; the C code is OrcSDR's own. License text: ports/p4/licenses/SensorLib-MIT.txt.
 * Provenance and review record: docs/hardware/P4_REUSE.md.
 *
 * Pure functions with no hardware access so the host test can compile this file.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GT9895_ADDR_FW_VERSION 0x010014u /* 28-byte firmware/ID block */
#define GT9895_ADDR_COMMAND    0x010174u
#define GT9895_ADDR_POINT      0x010308u /* 8-byte event header, then 8 bytes per contact, then 2-byte sum */

#define GT9895_FW_BLOCK_LEN    28u
#define GT9895_EVENT_HEAD_LEN  8u
#define GT9895_BYTES_PER_POINT 8u
#define GT9895_MAX_CONTACTS    10u
#define GT9895_SUM_LEN         2u
#define GT9895_POINT_STYLUS_HOVER 1u
#define GT9895_POINT_STYLUS       3u

/* Register addresses are sent as 4 big-endian bytes before a read or write payload. */
static inline void gt9895_address_bytes(uint32_t address, uint8_t out[4])
{
    out[0] = (uint8_t)(address >> 24);
    out[1] = (uint8_t)(address >> 16);
    out[2] = (uint8_t)(address >> 8);
    out[3] = (uint8_t)address;
}

/* The last two bytes (little endian) hold the 16-bit sum of every byte before them. */
static inline bool gt9895_sum_ok(const uint8_t *p, size_t n)
{
    if (!p || n < GT9895_SUM_LEN) return false;
    uint32_t sum = 0;
    for (size_t i = 0; i + GT9895_SUM_LEN < n; i++) sum += p[i];
    return (sum & 0xFFFFu) == (uint32_t)(p[n - 2] | ((uint32_t)p[n - 1] << 8));
}

/* Firmware block: valid sum, and the patch PID (bytes 10..13) reads "9895". */
static inline bool gt9895_firmware_ok(const uint8_t *fw, size_t n)
{
    return fw && n == GT9895_FW_BLOCK_LEN && gt9895_sum_ok(fw, n) &&
           fw[10] == '9' && fw[11] == '8' && fw[12] == '9' && fw[13] == '5';
}

typedef enum {
    GT9895_EVENT_NONE,        /* controller has no new report */
    GT9895_EVENT_RELEASE,     /* valid report with zero contacts */
    GT9895_EVENT_CONTACTS,    /* valid report; `contacts` fingers follow */
    GT9895_EVENT_IGNORE,      /* valid but not a finger report (other event type) */
    GT9895_EVENT_INVALID,     /* checksum or length failure */
} gt9895_event_t;

/* Classify the 8-byte event header. On GT9895_EVENT_CONTACTS, *contacts is 1..10. */
static inline gt9895_event_t gt9895_parse_header(const uint8_t *h, size_t n, unsigned *contacts)
{
    if (contacts) *contacts = 0;
    if (!h || !contacts || n != GT9895_EVENT_HEAD_LEN) return GT9895_EVENT_INVALID;
    if (h[0] == 0) return GT9895_EVENT_NONE;
    if (!gt9895_sum_ok(h, GT9895_EVENT_HEAD_LEN)) return GT9895_EVENT_INVALID;
    const unsigned count = h[2] & 0x0Fu;
    if (count > GT9895_MAX_CONTACTS) return GT9895_EVENT_INVALID;
    if (!(h[0] & 0x80u)) return GT9895_EVENT_IGNORE;
    if (count == 0) return GT9895_EVENT_RELEASE;
    *contacts = count;
    return GT9895_EVENT_CONTACTS;
}

/* Length of the contact block that follows the header: 8 bytes per finger plus the trailing sum. */
static inline size_t gt9895_contact_block_len(unsigned contacts)
{
    return (size_t)contacts * GT9895_BYTES_PER_POINT + GT9895_SUM_LEN;
}

/* Validate a contact block and return the first contact's raw coordinates. Stylus reports are
 * rejected: they use a different block layout that OrcSDR does not use. */
static inline bool gt9895_parse_first_contact(const uint8_t *block, size_t n, unsigned contacts,
                                              int32_t *raw_x, int32_t *raw_y)
{
    if (!block || !raw_x || !raw_y || contacts == 0 || contacts > GT9895_MAX_CONTACTS) return false;
    if (n != gt9895_contact_block_len(contacts) || !gt9895_sum_ok(block, n)) return false;
    const unsigned type = block[0] & 0x0Fu;
    if (type == GT9895_POINT_STYLUS || type == GT9895_POINT_STYLUS_HOVER) return false;
    *raw_x = (int32_t)(block[2] | ((uint32_t)block[3] << 8));
    *raw_y = (int32_t)(block[4] | ((uint32_t)block[5] << 8));
    return true;
}

/* Map raw controller coordinates (0..raw_w-1, 0..raw_h-1) to native panel pixels, clamped. */
static inline int32_t gt9895_scale(int32_t raw, int32_t raw_extent, int32_t native_extent)
{
    if (raw_extent <= 0 || native_extent <= 0) return 0;
    if (raw < 0) raw = 0;
    if (raw >= raw_extent) raw = raw_extent - 1;
    int32_t v = (int32_t)(((int64_t)raw * native_extent) / raw_extent);
    return v >= native_extent ? native_extent - 1 : v;
}
