/* SPDX-License-Identifier: GPL-3.0-or-later
 * GT9895 packet/discovery validation adapted from LakeShark v2.19.2
 * components/lakeshark/board/ls_touch.c (SAMS0N1TE/LakeShark), which credits
 * LILYGO_L cpp_bus_driver gt9895.cpp. See docs/hardware/P4_REUSE.md.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static inline uint16_t gt9895_le16(const uint8_t *p)
{ return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static inline uint32_t gt9895_le32(const uint8_t *p)
{ return (uint32_t)gt9895_le16(p) | ((uint32_t)gt9895_le16(p+2) << 16); }
static inline bool gt9895_checksum(const uint8_t *p, size_t n)
{
    if (!p || n < 2) return false;
    uint16_t sum = 0;
    for (size_t i=0; i<n-2; ++i) sum = (uint16_t)(sum+p[i]);
    return sum == gt9895_le16(p+n-2);
}
static inline bool gt9895_report_address(const uint8_t *info, size_t n, uint32_t *out)
{
    if (!out || n < 87 || n > 1024 || !gt9895_checksum(info,n) || gt9895_le16(info) != n) return false;
    size_t off = 32;
    for (unsigned i=0; i<5; ++i) {
        if (off >= n-2) return false;
        size_t bytes = (size_t)info[off++]*2;
        if (bytes > n-2-off) return false;
        off += bytes;
    }
    if (n-2-off < 48) return false;
    uint32_t addr = gt9895_le32(info+off+44);
    if (!addr || addr > UINT32_MAX-18) return false;
    *out = addr;
    return true;
}
/* Single-finger input. A header-valid release must be acknowledged even though
 * it contains no point checksum. Caller handles unsupported multitouch explicitly. */
static inline bool gt9895_decode(const uint8_t *p, size_t n, int32_t *x, int32_t *y, bool *down)
{
    if (!p || !x || !y || !down || n != 18 || !p[0] || !gt9895_checksum(p,8)) return false;
    unsigned count = p[2]&15;
    if (count > 1) return false;
    bool pressed = (p[0]&0x80) && count;
    if (pressed && !gt9895_checksum(p+8,10)) return false;
    if (pressed) { *x=gt9895_le16(p+10); *y=gt9895_le16(p+12); }
    *down=pressed;
    return true;
}
