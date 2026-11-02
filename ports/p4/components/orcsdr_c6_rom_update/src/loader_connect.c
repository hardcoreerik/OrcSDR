#include "orcsdr_c6_rom_update.h"
#include "esp32_sdio_port.h"
#include "esp_loader.h"
#include "esp_log.h"

#include <stdlib.h>
#include <string.h>

static orc_c6_rom_loader_session_t *active_session;

static int reset_target(orc_c6_rom_loader_session_t *s)
{
    const int result = s->board.reset_c6 ? s->board.reset_c6(s->board.reset_context) : -1;
    if (result != 0) s->reset_failed = true;
    return result;
}

static void port_bootloader(esp_loader_port_t *unused)
{
    (void)unused;
    if (active_session) (void)reset_target(active_session);
}

static void port_reset(esp_loader_port_t *unused)
{
    (void)unused;
    if (active_session) (void)reset_target(active_session);
}

static esp_loader_error_t port_init(esp_loader_port_t *base)
{
    esp32_sdio_port_t *port = container_of(base, esp32_sdio_port_t, port);
    port->_card_config = (sdmmc_host_t)SDMMC_HOST_DEFAULT();
    port->_card_config.slot = port->slot;
    port->_card_config.max_freq_khz = port->max_freq_khz;
    port->_card_config.flags = SDMMC_HOST_FLAG_4BIT | SDMMC_HOST_FLAG_ALLOC_ALIGNED_BUF;
    esp_err_t init = sdmmc_host_init();
    ESP_LOGI("C6_ROM_LOADER", "RTL_C6_ROM_LOADER stage=host_init slot=%d code=0x%x", port->slot, (unsigned)init);
    if (init != ESP_OK && init != ESP_ERR_INVALID_STATE) return ESP_LOADER_ERROR_FAIL;
    port->_host_driver_needs_deinit = false;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.clk = port->sdio_clk_pin; slot.cmd = port->sdio_cmd_pin;
    slot.d0 = port->sdio_d0_pin; slot.d1 = port->sdio_d1_pin;
    slot.d2 = port->sdio_d2_pin; slot.d3 = port->sdio_d3_pin;
    slot.flags = SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    const esp_err_t slot_init = sdmmc_host_init_slot(port->slot, &slot);
    ESP_LOGI("C6_ROM_LOADER", "RTL_C6_ROM_LOADER stage=slot_init slot=%d code=0x%x", port->slot, (unsigned)slot_init);
    if (slot_init != ESP_OK) {
        return ESP_LOADER_ERROR_FAIL;
    }
    port->_host_driver_needs_deinit = true; // this session owns only this slot
    return ESP_LOADER_SUCCESS;
}

static void port_deinit(esp_loader_port_t *base)
{
    esp32_sdio_port_t *port = container_of(base, esp32_sdio_port_t, port);
    free(port->_card.host.dma_aligned_buffer);
    port->_card.host.dma_aligned_buffer = NULL;
    if (port->_host_driver_needs_deinit) sdmmc_host_deinit_slot(port->slot);
    port->_host_driver_needs_deinit = false;
}

void orc_c6_rom_loader_session_init(orc_c6_rom_loader_session_t *s,
                                    esp_loader_t *loader, void *port_storage,
                                    void *ops_storage, const orc_c6_rom_board_t *board)
{
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->loader_storage = loader;
    s->port_storage = port_storage;
    s->ops_storage = ops_storage;
    if (board) s->board = *board;
}

orc_c6_rom_result_t orc_c6_rom_loader_connect(
    orc_c6_rom_loader_session_t *s, esp_loader_t *loader,
    void *port_storage, void *ops_storage, const orc_c6_rom_board_t *board)
{
    if (!s || !loader || !port_storage || !ops_storage || !board ||
        !board->reset_c6 || !board->known_flash_size || board->slot < 0 ||
        board->clk_pin < 0 || board->cmd_pin < 0 || board->d0_pin < 0 ||
        board->d1_pin < 0 || board->d2_pin < 0 || board->d3_pin < 0 || s->connected)
        return ORC_C6_ROM_INVALID_ARGUMENT;

    esp32_sdio_port_t *port = port_storage;
    esp_loader_port_ops_t *ops = ops_storage;
    *ops = esp32_sdio_ops;
    ops->init = port_init;
    ops->deinit = port_deinit;
    ops->enter_bootloader = port_bootloader;
    ops->reset_target = port_reset;
    *port = (esp32_sdio_port_t){
        .port.ops = ops,
        .slot = board->slot,
        .max_freq_khz = board->max_freq_khz,
        .sdio_clk_pin = board->clk_pin,
        .sdio_cmd_pin = board->cmd_pin,
        .sdio_d0_pin = board->d0_pin,
        .sdio_d1_pin = board->d1_pin,
        .sdio_d2_pin = board->d2_pin,
        .sdio_d3_pin = board->d3_pin,
        .bus_width = SDIO_4BIT,
    };
    memset(loader, 0, sizeof(*loader));
    s->loader_storage = loader;
    s->port_storage = port_storage;
    s->ops_storage = ops_storage;
    s->board = *board;
    s->reset_failed = false;
    active_session = s;

    esp_loader_error_t e = esp_loader_init_sdio(loader, &port->port);
    ESP_LOGI("C6_ROM_LOADER", "RTL_C6_ROM_LOADER stage=loader_init code=%d", (int)e);
    if (e == ESP_LOADER_SUCCESS) {
        esp_loader_connect_args_t args = {.sync_timeout = 1000, .trials = 1};
        e = esp_loader_connect(loader, &args);
        ESP_LOGI("C6_ROM_LOADER", "RTL_C6_ROM_LOADER stage=connect code=%d reset_failed=%d", (int)e, s->reset_failed);
    }
    if (e != ESP_LOADER_SUCCESS || s->reset_failed ||
        esp_loader_get_target(loader) != ESP32C6_CHIP) {
        const bool wrong_chip = e == ESP_LOADER_SUCCESS && !s->reset_failed &&
            esp_loader_get_target(loader) != ESP32C6_CHIP;
        if (loader->_port) esp_loader_deinit(loader);
        active_session = NULL;
        return wrong_chip ? ORC_C6_ROM_WRONG_CHIP : ORC_C6_ROM_CONNECT_FAILED;
    }

    esp_loader_target_security_info_t security = {0};
    e = esp_loader_get_security_info(loader, &security);
    if (e != ESP_LOADER_SUCCESS || security.secure_boot_enabled ||
        security.secure_download_mode_enabled || security.flash_encryption_enabled) {
        esp_loader_deinit(loader);
        active_session = NULL;
        return ORC_C6_ROM_UNSUPPORTED_SECURITY;
    }
    /* The pinned loader has no public flash-size setter and its ROM size
     * autodetection uses register writes. Use the profile's known C6 package
     * capacity, as in the proven experiment, and reject manifest ranges beyond it. */
    loader->_target_flash_size = board->known_flash_size;
    s->connected = true;
    return ORC_C6_ROM_OK;
}

void orc_c6_rom_loader_disconnect(orc_c6_rom_loader_session_t *s)
{
    if (!s || !s->connected || !s->loader_storage) return;
    esp_loader_deinit(s->loader_storage);
    s->connected = false;
    active_session = NULL;
}

int orc_c6_rom_loader_connect_callback(void *context, uint32_t *chip_id,
                                       uint32_t *flash_size, bool *security_supported)
{
    orc_c6_rom_loader_session_t *s = context;
    if (!s || !chip_id || !flash_size || !security_supported) return -1;
    const orc_c6_rom_result_t result = orc_c6_rom_loader_connect(
        s, s->loader_storage, s->port_storage, s->ops_storage, &s->board);
    if (result != ORC_C6_ROM_OK) return -1;
    *chip_id = 13u; /* ESP32-C6 image chip ID, distinct from loader enum ordinal. */
    *flash_size = s->board.known_flash_size;
    *security_supported = true;
    return 0;
}

int orc_c6_rom_loader_write_callback(void *context, uint32_t offset,
                                     const uint8_t *data, size_t size)
{
    orc_c6_rom_loader_session_t *s = context;
    if (!s || !s->connected || !data) return -1;
    const orc_c6_rom_segment_t segment = {.offset = offset, .data = data, .size = size};
    return orc_c6_rom_loader_write_segment(s->loader_storage, &segment) == ORC_C6_ROM_OK ? 0 : -1;
}

int orc_c6_rom_loader_sha256_flash_callback(void *context, uint32_t offset,
                                            size_t size, uint8_t digest[32])
{
    orc_c6_rom_loader_session_t *s = context;
    if (!s || !s->connected) return -1;
    return orc_c6_rom_loader_sha256_flash(s->loader_storage, offset, size, digest) == ORC_C6_ROM_OK
        ? 0 : -1;
}
