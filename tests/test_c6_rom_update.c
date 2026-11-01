#include "orcsdr_c6_rom_update.h"
#include "loader_write_policy.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    unsigned connects, writes, reads, releases, failures;
    uint32_t chip;
    bool security;
    uint8_t flash[256];
    const uint8_t *last_data;
    size_t last_size;
} fake_t;

static int hash(void *ctx, const uint8_t *data, size_t size, uint8_t out[32])
{
    (void)ctx;
    memset(out, 0, 32);
    for (size_t i = 0; i < size; ++i) out[i % 32] = (uint8_t)(out[i % 32] * 33u + data[i]);
    return 0;
}

static int connect_rom(void *ctx, uint32_t *chip, uint32_t *flash, bool *security)
{
    fake_t *f = ctx; ++f->connects;
    *chip = f->chip; *flash = sizeof(f->flash); *security = f->security;
    return f->failures == 1 ? -1 : 0;
}

static int write_segment(void *ctx, uint32_t off, const uint8_t *data, size_t size)
{
    fake_t *f = ctx; ++f->writes;
    if (off + size > sizeof(f->flash)) return -1;
    f->last_data = data; f->last_size = size;
    memcpy(f->flash + off, data, size);
    return f->failures == 2 ? -1 : 0;
}

static int hash_flash(void *ctx, uint32_t off, size_t size, uint8_t out[32])
{
    fake_t *f = ctx; ++f->reads;
    if (off + size > sizeof(f->flash)) return -1;
    hash(NULL, f->flash + off, size, out);
    if (f->failures == 3) out[0] ^= 0xff;
    return 0;
}

static void progress(void *ctx, orc_c6_rom_phase_t phase, uint8_t percent)
{
    fake_t *f = ctx; (void)percent;
    if (phase == ORC_C6_ROM_PHASE_RELEASE_BOOT) ++f->releases;
}

static int check(int condition, const char *name)
{
    if (!condition) fprintf(stderr, "FAIL: %s\n", name);
    return condition ? 0 : 1;
}

int main(void)
{
    const uint8_t bytes[] = {1, 2, 3, 4};
    orc_c6_rom_segment_t segment = {.offset = 32, .data = bytes, .size = sizeof(bytes)};
    fake_t fake = {0};
    fake.chip = 13; fake.security = true;
    orc_c6_rom_ops_t ops = {&fake, connect_rom, hash, write_segment, hash_flash, progress};
    hash(NULL, bytes, sizeof(bytes), segment.sha256);
    orc_c6_rom_manifest_t manifest = {true, 13, sizeof(fake.flash), 1, &segment};
    orc_c6_rom_update_t update = ORC_C6_ROM_UPDATE_INITIALIZER;
    int failures = 0;

    failures += check(orc_c6_rom_prepare(&update, &manifest, &ops) == ORC_C6_ROM_OK,
                      "prepare connects after hashes");
    failures += check(fake.releases == 1 && fake.writes == 0,
                      "release BOOT is an explicit pre-write phase");
    failures += check(orc_c6_rom_flash(&update) == ORC_C6_ROM_OK,
                      "flash and full readback hash");
    failures += check(fake.writes == 1 && fake.reads == 1 &&
                      memcmp(fake.flash + 32, bytes, sizeof(bytes)) == 0,
                      "exact declared segment written and verified");
    failures += check(orc_c6_rom_flash(&update) == ORC_C6_ROM_BAD_STATE,
                      "no automatic retry or second flash");

    orc_c6_loader_guard_arm(0x60000, 2048);
    failures += check(!orc_c6_loader_allow_begin(0x10000, 2048, 2, 1024) &&
                      !orc_c6_loader_allow_begin(0x60000, 4096, 2, 1024) &&
                      orc_c6_loader_allow_begin(0x60000, 2048, 2, 1024),
                      "loader guard binds exact segment address and size");
    orc_c6_loader_note_success(0x02);
    failures += check(orc_c6_loader_allow_data(0, 1024) &&
                      !orc_c6_loader_allow_data(1, 1024), "loader guard sequences blocks");
    orc_c6_loader_note_success(0x03);
    failures += check(orc_c6_loader_allow_data(1, 1024), "loader guard permits final block");
    orc_c6_loader_note_success(0x03);
    failures += check(orc_c6_loader_allow_md5(0x60000, 2048) &&
                      !orc_c6_loader_allow_md5(0x10000, 2048), "loader guard bounds hash");
    orc_c6_loader_note_success(0x13);
    failures += check(orc_c6_loader_allow_end(1) && !orc_c6_loader_allow_end(0),
                      "loader guard closes only verified session");
    orc_c6_loader_guard_disarm();
    failures += check(!orc_c6_loader_allow_begin(0x60000, 2048, 2, 1024),
                      "loader guard disarms after segment");

    update = (orc_c6_rom_update_t)ORC_C6_ROM_UPDATE_INITIALIZER;
    fake.chip = 5;
    failures += check(orc_c6_rom_prepare(&update, &manifest, &ops) == ORC_C6_ROM_WRONG_CHIP &&
                      fake.writes == 1, "wrong chip rejected before any write");
    fake.chip = 13; fake.security = false;
    update = (orc_c6_rom_update_t)ORC_C6_ROM_UPDATE_INITIALIZER;
    failures += check(orc_c6_rom_prepare(&update, &manifest, &ops) ==
                      ORC_C6_ROM_UNSUPPORTED_SECURITY && fake.writes == 1,
                      "unsupported security rejected before any write");
    fake.security = true;
    update = (orc_c6_rom_update_t)ORC_C6_ROM_UPDATE_INITIALIZER;
    fake.failures = 2;
    failures += check(orc_c6_rom_prepare(&update, &manifest, &ops) == ORC_C6_ROM_OK,
                      "prepare write failure case");
    failures += check(orc_c6_rom_flash(&update) == ORC_C6_ROM_WRITE_FAILED && fake.writes == 2,
                      "write failure stops without retry");
    fake.failures = 3;
    update = (orc_c6_rom_update_t)ORC_C6_ROM_UPDATE_INITIALIZER;
    failures += check(orc_c6_rom_prepare(&update, &manifest, &ops) == ORC_C6_ROM_OK,
                      "prepare readback mismatch case");
    failures += check(orc_c6_rom_flash(&update) == ORC_C6_ROM_BAD_READBACK_HASH,
                      "readback mismatch fails closed");
    fake.failures = 1;
    update = (orc_c6_rom_update_t)ORC_C6_ROM_UPDATE_INITIALIZER;
    failures += check(orc_c6_rom_prepare(&update, &manifest, &ops) ==
                      ORC_C6_ROM_CONNECT_FAILED && fake.writes == 3,
                      "connect failure causes no write or retry");
    fake.failures = 0;

    update = (orc_c6_rom_update_t)ORC_C6_ROM_UPDATE_INITIALIZER;
    manifest.trusted = false;
    const unsigned connects_before_untrusted = fake.connects;
    failures += check(orc_c6_rom_prepare(&update, &manifest, &ops) ==
                      ORC_C6_ROM_UNTRUSTED_MANIFEST &&
                      fake.connects == connects_before_untrusted,
                      "untrusted manifest rejected before connect");
    manifest.trusted = true;
    manifest.segments = &segment;
    segment.offset = 255; segment.size = 4;
    failures += check(orc_c6_rom_validate_manifest(&manifest, &ops) == ORC_C6_ROM_BAD_RANGE,
                      "range overflow rejected");
    segment.offset = 32; segment.size = sizeof(bytes);
    orc_c6_rom_segment_t overlap[2] = {segment, segment};
    manifest.segment_count = 2; manifest.segments = overlap;
    failures += check(orc_c6_rom_validate_manifest(&manifest, &ops) == ORC_C6_ROM_BAD_RANGE,
                      "overlapping segments rejected");
    manifest.segment_count = 1; manifest.segments = &segment;
    segment.sha256[0] ^= 0x80;
    failures += check(orc_c6_rom_validate_manifest(&manifest, &ops) == ORC_C6_ROM_BAD_IMAGE_HASH,
                      "corrupt image hash rejected");
    return failures ? 1 : 0;
}
