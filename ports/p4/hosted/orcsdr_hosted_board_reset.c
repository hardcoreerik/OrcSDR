#include "orcsdr_hosted_board_reset.h"
#include "orcsdr_board_control.h"
#include "orcsdr_board_profile.h"
#include "eh_host_port_master_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

bool orcsdr_hosted_board_reset_available(void)
{
    const orcsdr_board_profile_t *b = orcsdr_board_profile();
    return b->has_hosted_c6 && b->c6_reset < 0 && b->ioexp_addr[0] != 0 &&
           b->ioexp_wifi_en.dev == 0 && b->ioexp_wifi_en.pin == 14;
}

eh_host_port_err_t orcsdr_hosted_board_reset(void)
{
    if (!orcsdr_hosted_board_reset_available()) return EH_HOST_PORT_ERR_NOT_CONFIGURED;
    /* C6 EN is XL9535 vendor IO14, released through its board pull-up. */
    esp_err_t rc = orcsdr_board_c6_enable(false);
    if (rc == ESP_OK) vTaskDelay(pdMS_TO_TICKS(20));
    /* Always attempt release, including after an I2C failure asserting EN. */
    esp_err_t release_rc = orcsdr_board_c6_enable(true);
    if (rc == ESP_OK) rc = release_rc;
    if (rc != ESP_OK) {
        ESP_LOGE("hosted_board_reset", "C6 expander reset failed: %s", esp_err_to_name(rc));
        return EH_HOST_PORT_ERR;
    }
    vTaskDelay(pdMS_TO_TICKS(EH_HOST_PORT_RESET_POST_DELAY_MS));
    ESP_LOGI("hosted_board_reset", "C6 reset via XL9535 IO14 complete");
    return EH_HOST_PORT_OK;
}
