#include <esp_err.h>
#include <esp_event.h>
#include <esp_hosted.h>
#include <esp_hosted_ota.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>

extern const uint8_t esp_hosted_3_0_6_tab5_c6_bin_start[]
    asm("_binary_esp_hosted_3_0_6_tab5_c6_bin_start");
extern const uint8_t esp_hosted_3_0_6_tab5_c6_bin_end[]
    asm("_binary_esp_hosted_3_0_6_tab5_c6_bin_end");

static const char* TAG = "OrcSDR-bridge";

void app_main(void) {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  ESP_ERROR_CHECK(esp_hosted_init());
  ESP_ERROR_CHECK(esp_hosted_connect_to_slave());
  esp_hosted_coprocessor_fwver_t before = {0};
  ESP_ERROR_CHECK(esp_hosted_get_coprocessor_fwversion(&before));
  ESP_LOGI(TAG, "C6 before OTA: %u.%u.%u", before.major1, before.minor1, before.patch1);
  const size_t size = esp_hosted_3_0_6_tab5_c6_bin_end - esp_hosted_3_0_6_tab5_c6_bin_start;
  ESP_ERROR_CHECK(esp_hosted_slave_ota_begin());
  for (size_t offset = 0; offset < size; ) {
    const size_t chunk = size - offset > 1024 ? 1024 : size - offset;
    ESP_ERROR_CHECK(esp_hosted_slave_ota_write(esp_hosted_3_0_6_tab5_c6_bin_start + offset, chunk));
    offset += chunk;
  }
  ESP_ERROR_CHECK(esp_hosted_slave_ota_end());
  ESP_ERROR_CHECK(esp_hosted_slave_ota_activate());
  ESP_LOGI(TAG, "C6 OTA complete: %u bytes; restarting P4 for the matching 3.0.6 app", (unsigned)size);
  esp_restart();
}
