#include "orcsdr_c6_rom_update.h"

#include <string.h>

#define ESP32C6_CHIP_ID 13u

static bool digest_equal(const uint8_t *a, const uint8_t *b)
{
    uint8_t diff = 0;
    for (size_t i = 0; i < ORC_C6_ROM_SHA256_BYTES; ++i) diff |= a[i] ^ b[i];
    return diff == 0;
}

orc_c6_rom_result_t orc_c6_rom_validate_manifest(
    const orc_c6_rom_manifest_t *m, const orc_c6_rom_ops_t *ops)
{
    if (!m || !ops || !ops->sha256 || !m->segments) return ORC_C6_ROM_INVALID_ARGUMENT;
    if (!m->trusted) return ORC_C6_ROM_UNTRUSTED_MANIFEST;
    if (m->chip_id != ESP32C6_CHIP_ID) return ORC_C6_ROM_WRONG_CHIP;
    if (!m->flash_size || !m->segment_count || m->segment_count > ORC_C6_ROM_MAX_SEGMENTS)
        return ORC_C6_ROM_BAD_RANGE;

    for (uint8_t i = 0; i < m->segment_count; ++i) {
        const orc_c6_rom_segment_t *s = &m->segments[i];
        if (!s->data || !s->size || s->offset >= m->flash_size ||
            s->size > (size_t)(m->flash_size - s->offset)) return ORC_C6_ROM_BAD_RANGE;
        uint8_t digest[ORC_C6_ROM_SHA256_BYTES];
        if (ops->sha256(ops->context, s->data, s->size, digest) != 0)
            return ORC_C6_ROM_BAD_IMAGE_HASH;
        if (!digest_equal(digest, s->sha256)) return ORC_C6_ROM_BAD_IMAGE_HASH;

        const uint64_t start = s->offset;
        const uint64_t end = start + s->size;
        for (uint8_t j = 0; j < i; ++j) {
            const orc_c6_rom_segment_t *p = &m->segments[j];
            const uint64_t p_start = p->offset;
            const uint64_t p_end = p_start + p->size;
            if (start < p_end && p_start < end) return ORC_C6_ROM_BAD_RANGE;
        }
    }
    return ORC_C6_ROM_OK;
}

static orc_c6_rom_result_t fail(orc_c6_rom_update_t *u, orc_c6_rom_result_t r)
{
    u->state = ORC_C6_ROM_STATE_FAILED;
    if (u->ops && u->ops->progress)
        u->ops->progress(u->ops->context, ORC_C6_ROM_PHASE_FAILED, 0);
    return r;
}

orc_c6_rom_result_t orc_c6_rom_prepare(orc_c6_rom_update_t *u,
                                      const orc_c6_rom_manifest_t *m,
                                      const orc_c6_rom_ops_t *ops)
{
    if (!u || !m || !ops || !ops->connect_c6_rom || !ops->write_segment ||
        !ops->sha256_flash || !ops->sha256) return ORC_C6_ROM_INVALID_ARGUMENT;
    if (u->state == ORC_C6_ROM_STATE_READY) return ORC_C6_ROM_BAD_STATE;
    u->manifest = NULL;
    u->ops = ops;
    u->state = ORC_C6_ROM_STATE_IDLE;

    orc_c6_rom_result_t r = orc_c6_rom_validate_manifest(m, ops);
    if (r != ORC_C6_ROM_OK) return fail(u, r);

    if (ops->progress) ops->progress(ops->context, ORC_C6_ROM_PHASE_CONNECTING, 0);
    uint32_t chip = 0, flash_size = 0;
    bool security_supported = false;
    if (ops->connect_c6_rom(ops->context, &chip, &flash_size, &security_supported) != 0)
        return fail(u, ORC_C6_ROM_CONNECT_FAILED);
    if (chip != ESP32C6_CHIP_ID) return fail(u, ORC_C6_ROM_WRONG_CHIP);
    if (!security_supported) return fail(u, ORC_C6_ROM_UNSUPPORTED_SECURITY);
    if (!flash_size || flash_size < m->flash_size) return fail(u, ORC_C6_ROM_BAD_RANGE);

    u->manifest = m;
    u->state = ORC_C6_ROM_STATE_READY;
    if (ops->progress) ops->progress(ops->context, ORC_C6_ROM_PHASE_RELEASE_BOOT, 0);
    return ORC_C6_ROM_OK;
}

orc_c6_rom_result_t orc_c6_rom_flash(orc_c6_rom_update_t *u)
{
    if (!u || u->state != ORC_C6_ROM_STATE_READY || !u->manifest || !u->ops)
        return ORC_C6_ROM_BAD_STATE;
    const orc_c6_rom_manifest_t *m = u->manifest;
    const orc_c6_rom_ops_t *ops = u->ops;
    uint64_t total = 0, completed = 0;
    for (uint8_t i = 0; i < m->segment_count; ++i) total += m->segments[i].size;

    for (uint8_t i = 0; i < m->segment_count; ++i) {
        const orc_c6_rom_segment_t *s = &m->segments[i];
        uint8_t digest[ORC_C6_ROM_SHA256_BYTES];
        /* Recheck just before the irreversible call in case source memory
         * changed after prepare. */
        if (ops->sha256(ops->context, s->data, s->size, digest) != 0 ||
            !digest_equal(digest, s->sha256)) return fail(u, ORC_C6_ROM_BAD_IMAGE_HASH);
        if (ops->progress) ops->progress(ops->context, ORC_C6_ROM_PHASE_WRITING,
            (uint8_t)((uint64_t)completed * 100u / total));
        if (ops->write_segment(ops->context, s->offset, s->data, s->size) != 0)
            return fail(u, ORC_C6_ROM_WRITE_FAILED);
        completed += s->size;
        if (ops->progress) ops->progress(ops->context, ORC_C6_ROM_PHASE_WRITING,
            (uint8_t)((uint64_t)completed * 100u / total));
        if (ops->progress) ops->progress(ops->context, ORC_C6_ROM_PHASE_VERIFYING,
            (uint8_t)((i + 1u) * 100u / m->segment_count));
        if (ops->sha256_flash(ops->context, s->offset, s->size, digest) != 0)
            return fail(u, ORC_C6_ROM_READBACK_FAILED);
        if (!digest_equal(digest, s->sha256)) return fail(u, ORC_C6_ROM_BAD_READBACK_HASH);
    }
    u->state = ORC_C6_ROM_STATE_DONE;
    if (ops->progress) ops->progress(ops->context, ORC_C6_ROM_PHASE_DONE, 100);
    return ORC_C6_ROM_OK;
}
