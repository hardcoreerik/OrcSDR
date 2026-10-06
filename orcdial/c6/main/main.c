// SPDX-License-Identifier: Apache-2.0
// OrcDial ESP-NOW radio endpoint for the Tab5's ESP-Hosted C6.
#include <string.h>
#include <stdatomic.h>
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "eh_cp_feat_peer_data.h"

#define ORCDIAL_PACKET_SIZE 64
#define ORCDIAL_PEER_FRAME_SIZE (6 + ORCDIAL_PACKET_SIZE)
#define ORCDIAL_HOST_TO_C6 0x4f444c01u
#define ORCDIAL_C6_TO_HOST 0x4f444c02u

static const char *TAG = "orcdial_c6";
static const uint8_t broadcast[6] = {255,255,255,255,255,255};
typedef struct { uint8_t bytes[ORCDIAL_PEER_FRAME_SIZE]; } peer_frame_t;
static QueueHandle_t received;
static QueueHandle_t outbound;
static bool now_ready;
static atomic_bool host_ready;

static void receive_now(const esp_now_recv_info_t *info, const uint8_t *data, int size) {
    // Discovery can arrive while Hosted is still bringing up Wi-Fi. Do not
    // inject accessory RPC events until the P4 explicitly announces readiness.
    if (!atomic_load_explicit(&host_ready, memory_order_acquire) ||
        !received || !info || !info->src_addr || !data || size != ORCDIAL_PACKET_SIZE) return;
    peer_frame_t frame;
    memcpy(frame.bytes, info->src_addr, 6);
    memcpy(frame.bytes + 6, data, ORCDIAL_PACKET_SIZE);
    (void)xQueueSend(received, &frame, 0); // bounded; drop instead of blocking Wi-Fi callback
}

static void from_host(uint32_t id, const uint8_t *data, size_t size, void *ctx) {
    (void)id; (void)ctx;
    if (!outbound || !data || size != ORCDIAL_PEER_FRAME_SIZE) return;
    atomic_store_explicit(&host_ready, true, memory_order_release);
    peer_frame_t frame;
    memcpy(frame.bytes, data, sizeof(frame.bytes));
    (void)xQueueSend(outbound, &frame, 0); // RPC callback must not block on Wi-Fi
}

static void wifi_event(void *ctx, esp_event_base_t base, int32_t id, void *event) {
    (void)ctx; (void)base; (void)event;
    if (id == WIFI_EVENT_STA_STOP) {
        atomic_store_explicit(&host_ready, false, memory_order_release);
        if (now_ready) esp_now_deinit();
        now_ready = false;
        return;
    }
    if (id != WIFI_EVENT_STA_START || now_ready) return;
    if (esp_now_init() != ESP_OK) { ESP_LOGE(TAG, "ESP-NOW init failed"); return; }
    if (esp_now_register_recv_cb(receive_now) != ESP_OK) {
        esp_now_deinit(); return;
    }
    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, broadcast, 6);
    peer.ifidx = WIFI_IF_STA;
    (void)esp_now_add_peer(&peer);
    now_ready = true;
    ESP_LOGI(TAG, "ESP-NOW ready on hosted STA");
}

static void forward_to_host(void *ctx) {
    (void)ctx;
    peer_frame_t frame;
    while (true) {
        if (xQueueReceive(received, &frame, portMAX_DELAY) != pdTRUE) continue;
        (void)eh_cp_feat_peer_data_send(ORCDIAL_C6_TO_HOST, frame.bytes, sizeof(frame.bytes));
    }
}

static void forward_to_dial(void *ctx) {
    (void)ctx;
    peer_frame_t frame;
    while (true) {
        if (xQueueReceive(outbound, &frame, portMAX_DELAY) != pdTRUE || !now_ready) continue;
        if (!esp_now_is_peer_exist(frame.bytes)) {
            esp_now_peer_info_t peer = {0};
            memcpy(peer.peer_addr, frame.bytes, 6);
            peer.ifidx = WIFI_IF_STA;
            peer.channel = 0;
            if (esp_now_add_peer(&peer) != ESP_OK) continue;
        }
        (void)esp_now_send(frame.bytes, frame.bytes + 6, ORCDIAL_PACKET_SIZE);
    }
}

void app_main(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase()); err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    received = xQueueCreate(8, sizeof(peer_frame_t));
    outbound = xQueueCreate(8, sizeof(peer_frame_t));
    ESP_ERROR_CHECK(received && outbound ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(eh_cp_feat_peer_data_init());
    ESP_ERROR_CHECK(eh_cp_feat_peer_data_register_callback(ORCDIAL_HOST_TO_C6, from_host, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    BaseType_t started = xTaskCreate(forward_to_host, "orcdial_peer", 4096, NULL, 5, NULL);
    ESP_ERROR_CHECK(started == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    started = xTaskCreate(forward_to_dial, "orcdial_now", 4096, NULL, 5, NULL);
    ESP_ERROR_CHECK(started == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_LOGI(TAG, "OrcDial hosted relay initialized");
}
