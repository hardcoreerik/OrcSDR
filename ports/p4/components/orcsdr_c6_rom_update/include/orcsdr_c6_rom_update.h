#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ORC_C6_ROM_MAX_SEGMENTS 8u
#define ORC_C6_ROM_SHA256_BYTES 32u

typedef enum {
    ORC_C6_ROM_OK = 0,
    ORC_C6_ROM_INVALID_ARGUMENT,
    ORC_C6_ROM_UNTRUSTED_MANIFEST,
    ORC_C6_ROM_UNSUPPORTED_SECURITY,
    ORC_C6_ROM_WRONG_CHIP,
    ORC_C6_ROM_BAD_RANGE,
    ORC_C6_ROM_BAD_IMAGE_HASH,
    ORC_C6_ROM_CONNECT_FAILED,
    ORC_C6_ROM_WRITE_FAILED,
    ORC_C6_ROM_READBACK_FAILED,
    ORC_C6_ROM_BAD_READBACK_HASH,
    ORC_C6_ROM_BAD_STATE
} orc_c6_rom_result_t;

typedef enum {
    ORC_C6_ROM_PHASE_CONNECTING,
    ORC_C6_ROM_PHASE_RELEASE_BOOT,
    ORC_C6_ROM_PHASE_WRITING,
    ORC_C6_ROM_PHASE_VERIFYING,
    ORC_C6_ROM_PHASE_DONE,
    ORC_C6_ROM_PHASE_FAILED
} orc_c6_rom_phase_t;

typedef struct {
    uint32_t offset;
    const uint8_t *data;
    size_t size;
    uint8_t sha256[ORC_C6_ROM_SHA256_BYTES];
} orc_c6_rom_segment_t;

/* `trusted` means the caller verified the official signed/package provenance,
 * target version and manifest digest. This module independently checks all
 * image bytes and ranges; it does not define the release trust root. */
typedef struct {
    bool trusted;
    uint32_t chip_id;
    uint32_t flash_size;
    uint8_t segment_count;
    const orc_c6_rom_segment_t *segments;
} orc_c6_rom_manifest_t;

typedef struct {
    void *context;
    /* Board supplies SDIO pins/configuration and C6 EN reset. Caller owns the
     * bus and must stop Hosted before calling prepare. No retries are made. */
    int (*connect_c6_rom)(void *context, uint32_t *chip_id,
                          uint32_t *flash_size, bool *security_supported);
    int (*sha256)(void *context, const uint8_t *data, size_t size,
                  uint8_t digest[ORC_C6_ROM_SHA256_BYTES]);
    /* Must write exactly this complete segment range. Implementations must
     * guard every ROM flash command against the active manifest range. */
    int (*write_segment)(void *context, uint32_t offset,
                         const uint8_t *data, size_t size);
    /* Must hash the complete flash range by reading it back from the C6. */
    int (*sha256_flash)(void *context, uint32_t offset, size_t size,
                        uint8_t digest[ORC_C6_ROM_SHA256_BYTES]);
    void (*progress)(void *context, orc_c6_rom_phase_t phase,
                     uint8_t percent);
} orc_c6_rom_ops_t;

typedef enum {
    ORC_C6_ROM_STATE_IDLE,
    ORC_C6_ROM_STATE_READY,
    ORC_C6_ROM_STATE_DONE,
    ORC_C6_ROM_STATE_FAILED
} orc_c6_rom_state_t;

typedef struct {
    const orc_c6_rom_manifest_t *manifest;
    const orc_c6_rom_ops_t *ops;
    orc_c6_rom_state_t state;
} orc_c6_rom_update_t;

#define ORC_C6_ROM_UPDATE_INITIALIZER { NULL, NULL, ORC_C6_ROM_STATE_IDLE }

/* Pinned esp-serial-flasher v2.1.0 adapter. These functions are valid only
 * while the caller owns SDIO and has an initialized C6 loader session. */
struct esp_loader;
typedef struct {
    int slot;
    uint32_t max_freq_khz;
    int clk_pin, cmd_pin, d0_pin, d1_pin, d2_pin, d3_pin;
    uint32_t known_flash_size;
    void *reset_context;
    int (*reset_c6)(void *context);
} orc_c6_rom_board_t;

typedef struct {
    struct esp_loader *loader_storage;
    void *port_storage;
    void *ops_storage;
    orc_c6_rom_board_t board;
    bool connected;
    bool reset_failed;
} orc_c6_rom_loader_session_t;

void orc_c6_rom_loader_session_init(orc_c6_rom_loader_session_t *session,
                                    struct esp_loader *loader_storage,
                                    void *port_storage, void *ops_storage,
                                    const orc_c6_rom_board_t *board);
orc_c6_rom_result_t orc_c6_rom_loader_connect(
    orc_c6_rom_loader_session_t *session,
    struct esp_loader *loader_storage,
    void *port_storage, void *ops_storage,
    const orc_c6_rom_board_t *board);
void orc_c6_rom_loader_disconnect(orc_c6_rom_loader_session_t *session);
int orc_c6_rom_loader_connect_callback(void *context, uint32_t *chip_id,
                                       uint32_t *flash_size, bool *security_supported);
int orc_c6_rom_loader_write_callback(void *context, uint32_t offset,
                                     const uint8_t *data, size_t size);
int orc_c6_rom_loader_sha256_callback(void *context, const uint8_t *data,
                                      size_t size, uint8_t digest[ORC_C6_ROM_SHA256_BYTES]);
int orc_c6_rom_loader_sha256_flash_callback(void *context, uint32_t offset,
                                            size_t size, uint8_t digest[ORC_C6_ROM_SHA256_BYTES]);
orc_c6_rom_result_t orc_c6_rom_loader_write_segment(
    struct esp_loader *loader, const orc_c6_rom_segment_t *segment);
orc_c6_rom_result_t orc_c6_rom_loader_sha256_flash(
    struct esp_loader *loader, uint32_t offset, size_t size,
    uint8_t digest[ORC_C6_ROM_SHA256_BYTES]);
orc_c6_rom_result_t orc_c6_rom_sha256(
    const uint8_t *data, size_t size,
    uint8_t digest[ORC_C6_ROM_SHA256_BYTES]);

/* Validates and hashes the complete manifest before connecting. On success
 * emits RELEASE_BOOT and returns READY. Caller must wait for physical BOOT
 * release before invoking flash; the module cannot sense the button. */
orc_c6_rom_result_t orc_c6_rom_prepare(orc_c6_rom_update_t *update,
                                      const orc_c6_rom_manifest_t *manifest,
                                      const orc_c6_rom_ops_t *ops);

/* Single shot. Never retries or erases outside manifest ranges. Success means
 * every byte in every declared range matches the manifest SHA-256 on readback. */
orc_c6_rom_result_t orc_c6_rom_flash(orc_c6_rom_update_t *update);

orc_c6_rom_result_t orc_c6_rom_validate_manifest(
    const orc_c6_rom_manifest_t *manifest, const orc_c6_rom_ops_t *ops);

#ifdef __cplusplus
}
#endif
