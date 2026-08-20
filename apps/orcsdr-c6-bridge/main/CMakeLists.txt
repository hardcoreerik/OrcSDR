idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_hosted esp_event nvs_flash
                       PRIV_INCLUDE_DIRS
                         "../managed_components/espressif__esp_hosted/host"
                         "../managed_components/espressif__esp_hosted/host/api/include"
                       EMBED_FILES "F:/Ai/OrcSDR-esp-hosted-3x/mcu_host/components/ota_partition/slave_fw_bin/esp_hosted_3.0.6_tab5_c6.bin")
