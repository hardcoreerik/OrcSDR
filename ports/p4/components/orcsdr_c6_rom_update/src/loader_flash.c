#include "orcsdr_c6_rom_update.h"
#include "esp_loader.h"
#include "loader_write_policy.h"
#include "mbedtls/sha256.h"

#include <stdlib.h>

#define FLASH_BLOCK_SIZE 1024u
#define READ_CHUNK_SIZE 4096u

orc_c6_rom_result_t orc_c6_rom_sha256(const uint8_t *data, size_t size, uint8_t digest[32])
{
    if (!data || !size || !digest) return ORC_C6_ROM_INVALID_ARGUMENT;
    return mbedtls_sha256(data, size, digest, 0) == 0
        ? ORC_C6_ROM_OK : ORC_C6_ROM_BAD_IMAGE_HASH;
}

int orc_c6_rom_loader_sha256_callback(void *context, const uint8_t *data,
                                      size_t size, uint8_t digest[32])
{
    (void)context;
    return orc_c6_rom_sha256(data, size, digest) == ORC_C6_ROM_OK ? 0 : -1;
}

orc_c6_rom_result_t orc_c6_rom_loader_write_segment(
    esp_loader_t *loader, const orc_c6_rom_segment_t *segment)
{
    if (!loader || !segment || !segment->data || !segment->size ||
        segment->size > UINT32_MAX || segment->offset > loader->_target_flash_size ||
        segment->size > loader->_target_flash_size - segment->offset)
        return ORC_C6_ROM_INVALID_ARGUMENT;
    orc_c6_loader_guard_arm(segment->offset, (uint32_t)segment->size);
    esp_loader_flash_cfg_t cfg = {
        .offset = segment->offset,
        .image_size = (uint32_t)segment->size,
        .block_size = FLASH_BLOCK_SIZE,
        .skip_verify = false,
    };
    esp_loader_error_t err = esp_loader_flash_start(loader, &cfg);
    for (size_t offset = 0; err == ESP_LOADER_SUCCESS && offset < segment->size;) {
        uint32_t count = (uint32_t)(segment->size - offset);
        if (count > FLASH_BLOCK_SIZE) count = FLASH_BLOCK_SIZE;
        err = esp_loader_flash_write(loader, &cfg, segment->data + offset, count);
        offset += count;
    }
    if (err == ESP_LOADER_SUCCESS) err = esp_loader_flash_finish(loader, &cfg);
    /* The guard normally disarms on FLASH_END. Always disarm after failures. */
    orc_c6_loader_guard_disarm();
    return err == ESP_LOADER_SUCCESS ? ORC_C6_ROM_OK : ORC_C6_ROM_WRITE_FAILED;
}

orc_c6_rom_result_t orc_c6_rom_loader_sha256_flash(
    esp_loader_t *loader, uint32_t offset, size_t size, uint8_t digest[32])
{
    if (!loader || !size || !digest || size > UINT32_MAX ||
        size - 1u > UINT32_MAX - offset) return ORC_C6_ROM_INVALID_ARGUMENT;
    uint8_t *buffer = malloc(READ_CHUNK_SIZE);
    if (!buffer) return ORC_C6_ROM_READBACK_FAILED;
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    int result = mbedtls_sha256_starts(&sha, 0);
    for (size_t done = 0; !result && done < size;) {
        uint32_t count = (uint32_t)(size - done);
        if (count > READ_CHUNK_SIZE) count = READ_CHUNK_SIZE;
        if (esp_loader_flash_read(loader, buffer, offset + (uint32_t)done, count) != ESP_LOADER_SUCCESS) {
            result = -1;
            break;
        }
        result = mbedtls_sha256_update(&sha, buffer, count);
        done += count;
    }
    if (!result) result = mbedtls_sha256_finish(&sha, digest);
    mbedtls_sha256_free(&sha);
    free(buffer);
    return result ? ORC_C6_ROM_READBACK_FAILED : ORC_C6_ROM_OK;
}
