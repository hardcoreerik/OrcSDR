#include "esp_loader_protocol.h"
#include "protocol.h"
#include "loader_write_policy.h"

extern void orc_c6_loader_note_success(uint8_t);

static const esp_loader_protocol_ops_t *original;
static esp_loader_protocol_ops_t guarded;

static int read_only_command(uint8_t command)
{
    return command == READ_REG || command == GET_SECURITY_INFO || command == READ_FLASH_STUB;
}

static int allowed(const send_cmd_config *c)
{
    if (!c || !c->cmd || c->cmd_size < sizeof(command_common_t)) return 0;
    const command_common_t *h = c->cmd;
    if (h->direction != 0 || h->size != c->cmd_size - sizeof(*h) + c->data_size) return 0;
    if (read_only_command(h->command)) return 1;
    switch (h->command) {
    case FLASH_BEGIN: {
        if (c->cmd_size != sizeof(flash_begin_command_t) - 4 || c->data_size) return 0;
        const flash_begin_command_t *v = c->cmd;
        return orc_c6_loader_allow_begin(v->offset, v->erase_size,
                                          v->packet_count, v->packet_size);
    }
    case FLASH_DATA: {
        if (c->cmd_size != sizeof(data_command_t) || !c->data) return 0;
        const data_command_t *v = c->cmd;
        return v->data_size == c->data_size &&
            orc_c6_loader_allow_data(v->sequence_number, v->data_size);
    }
    case SPI_FLASH_MD5: {
        if (c->cmd_size != sizeof(spi_flash_md5_command_t) || c->data_size) return 0;
        const spi_flash_md5_command_t *v = c->cmd;
        return orc_c6_loader_allow_md5(v->address, v->size);
    }
    case FLASH_END:
        if (c->cmd_size != sizeof(flash_end_command_t) || c->data_size) return 0;
        return orc_c6_loader_allow_end(((const flash_end_command_t *)c->cmd)->stay_in_loader);
    default:
        return 0;
    }
}

static esp_loader_error_t guarded_send(esp_loader_t *loader, const send_cmd_config *config)
{
    if (!allowed(config)) return ESP_LOADER_ERROR_UNSUPPORTED_FUNC;
    const uint8_t command = ((const command_common_t *)config->cmd)->command;
    const esp_loader_error_t result = original->send_cmd(loader, config);
    if (result == ESP_LOADER_SUCCESS) orc_c6_loader_note_success(command);
    return result;
}

const esp_loader_protocol_ops_t *__real_esp_loader_get_sdio_ops(void);
const esp_loader_protocol_ops_t *__wrap_esp_loader_get_sdio_ops(void)
{
    original = __real_esp_loader_get_sdio_ops();
    guarded = *original;
    guarded.send_cmd = guarded_send;
    return &guarded;
}
