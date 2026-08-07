/*
 * Minimal ESP-IDF smoke for RTL-SDRv4-ESP.
 *
 * Build once EXTRA_COMPONENT_DIRS points at ../../components.
 * start() is expected to return ESP_ERR_NOT_FINISHED until Gate 2 extraction.
 */
#include <stdio.h>

#include "esp_log.h"
#include "nvs_flash.h"
#include "rtl_sdr_v4_esp.h"

static const char *TAG = "p4_serial_smoke";

static void on_event(rtl_sdr_v4_esp_event_t event, const void *payload, void *ctx)
{
    (void)payload;
    (void)ctx;
    ESP_LOGI(TAG, "event=%d", (int)event);
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());

    rtl_sdr_v4_esp_config_t cfg;
    rtl_sdr_v4_esp_config_default(&cfg);
    cfg.event_cb = on_event;

    rtl_sdr_v4_esp_handle_t sdr = NULL;
    ESP_ERROR_CHECK(rtl_sdr_v4_esp_install(&cfg, &sdr));

    rtl_sdr_v4_esp_device_info_t info;
    if (rtl_sdr_v4_esp_get_device_info(sdr, &info) == ESP_OK) {
        ESP_LOGI(TAG, "filter %04x:%04x %s %s", info.vid, info.pid, info.manufacturer,
                 info.product);
    }

    rtl_sdr_v4_esp_stream_config_t stream;
    rtl_sdr_v4_esp_stream_config_default(&stream);
    esp_err_t err = rtl_sdr_v4_esp_start(sdr, &stream);
    ESP_LOGW(TAG, "start -> %s (NOT_FINISHED is expected pre-extraction)",
             esp_err_to_name(err));

    ESP_ERROR_CHECK(rtl_sdr_v4_esp_uninstall(sdr));
    ESP_LOGI(TAG, "smoke complete");
}
