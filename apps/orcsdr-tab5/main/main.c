#include <inttypes.h>
#include <stdio.h>

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void)
{
    usb_serial_jtag_driver_config_t usb_config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb_config));
    usb_serial_jtag_vfs_use_driver();
    setvbuf(stdout, NULL, _IONBF, 0);

    esp_chip_info_t chip;
    uint8_t mac[6];
    uint32_t flash_bytes = 0;
    esp_chip_info(&chip);
    esp_read_mac(mac, ESP_MAC_BASE);
    esp_flash_get_size(NULL, &flash_bytes);

    char node_id[40];
    snprintf(node_id, sizeof(node_id), "m5tab5_%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    printf("{\"type\":\"hello\",\"message_id\":\"m5tab5_hello_01\",\"protocol_version\":{\"major\":1,\"minor\":0},\"payload\":{\"adapter\":{\"id\":\"m5tab5.serial\",\"version\":\"0.1.0\"},\"features\":[\"telemetry\"],\"capability_namespaces\":[\"m5tab5\"],\"config_schema_version\":1}}\n");
    printf("{\"type\":\"node_snapshot\",\"message_id\":\"m5tab5_snapshot_01\",\"protocol_version\":{\"major\":1,\"minor\":0},\"payload\":{\"node_id\":\"%s\",\"connection_state\":\"online\",\"hardware\":{\"chip\":\"ESP32-P4\",\"revision\":%u,\"cores\":%u,\"flash_bytes\":%" PRIu32 ",\"psram_bytes\":%zu,\"free_heap_bytes\":%" PRIu32 "},\"capabilities\":[\"m5tab5.device.info\",\"m5tab5.health.read\"]}}\n",
           node_id, chip.revision, chip.cores, flash_bytes, esp_psram_get_size(), esp_get_free_heap_size());

    uint64_t sequence = 0;
    while (true) {
        ++sequence;
        printf("{\"type\":\"heartbeat\",\"message_id\":\"m5tab5_heartbeat_%" PRIu64 "\",\"protocol_version\":{\"major\":1,\"minor\":0},\"payload\":{\"node_id\":\"%s\",\"sequence\":%" PRIu64 ",\"uptime_ms\":%" PRIi64 ",\"free_heap_bytes\":%" PRIu32 "}}\n",
               sequence, node_id, sequence, esp_timer_get_time() / 1000, esp_get_free_heap_size());
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
