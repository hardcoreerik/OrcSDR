#include <esp_err.h>
#include <esp_event.h>
#include <esp_hosted.h>
#include <esp_hosted_ota.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>

extern const uint8_t esp_hosted_tab5_c6_bin_start[]
    asm("_binary_esp_hosted_tab5_c6_bin_start");
extern const uint8_t esp_hosted_tab5_c6_bin_end[]
    asm("_binary_esp_hosted_tab5_c6_bin_end");

static const char* TAG = "OrcSDR-bridge";

static bool fail(const char *stage, esp_err_t err) {
  ESP_LOGE(TAG, "C6_BRIDGE_FAIL stage=%s err=%s", stage, esp_err_to_name(err));
  return false;
}

void app_main(void) {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    fail("nvs_preserve", err);
    return;
  }
  if (err != ESP_OK) { fail("nvs_init", err); return; }
  if ((err = esp_event_loop_create_default()) != ESP_OK) { fail("event_loop", err); return; }

  if ((err = esp_hosted_init()) != ESP_OK) { fail("hosted_init", err); return; }
  if ((err = esp_hosted_connect_to_slave()) != ESP_OK) { fail("hosted_connect", err); return; }
  esp_hosted_coprocessor_fwver_t before = {0};
  if ((err = esp_hosted_get_coprocessor_fwversion(&before)) != ESP_OK) { fail("version_before", err); return; }
  ESP_LOGI(TAG, "C6 before OTA: %u.%u.%u", before.major1, before.minor1, before.patch1);
  if (before.major1 == 3 && before.minor1 == 0 && before.patch1 == 6) {
    ESP_LOGI(TAG, "C6 after OTA: %s (already current)", C6_HOSTED_VERSION);
    ESP_LOGI(TAG, "RTL_WIFI_HOSTED host=%s coprocessor=%u.%u.%u match=1",
             C6_HOSTED_VERSION, before.major1, before.minor1, before.patch1);
    return;
  }
  const size_t size = esp_hosted_tab5_c6_bin_end - esp_hosted_tab5_c6_bin_start;
  if ((err = esp_hosted_slave_ota_begin()) != ESP_OK) { fail("ota_begin", err); return; }
  for (size_t offset = 0; offset < size; ) {
    const size_t chunk = size - offset > 1024 ? 1024 : size - offset;
    if ((err = esp_hosted_slave_ota_write(esp_hosted_tab5_c6_bin_start + offset, chunk)) != ESP_OK) { fail("ota_write", err); return; }
    offset += chunk;
  }
  if ((err = esp_hosted_slave_ota_end()) != ESP_OK) { fail("ota_end", err); return; }
  if ((err = esp_hosted_slave_ota_activate()) != ESP_OK) { fail("ota_activate", err); return; }
  ESP_LOGI(TAG, "C6 OTA complete: %u bytes; restarting to verify %s", (unsigned)size, C6_HOSTED_VERSION);
  esp_restart();
}
