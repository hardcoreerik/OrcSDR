#pragma once
// Test-only ESP-IDF/FreeRTOS adapter. Faults are injected into real Runtime::begin.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>
inline int fault_step=0,operation=0,live_queues=0,live_stores=0;
inline bool failure(){return ++operation==fault_step;}
using QueueHandle_t=void*;using TaskHandle_t=void*;using nvs_handle_t=unsigned;
using portMUX_TYPE=int;
constexpr int pdTRUE=1,pdFALSE=0,pdPASS=1,ESP_OK=0,NVS_READWRITE=1;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define pdMS_TO_TICKS(x) (x)
inline QueueHandle_t xQueueCreate(unsigned,unsigned){if(failure())return nullptr;++live_queues;return std::malloc(1);}
inline void vQueueDelete(QueueHandle_t q){--live_queues;std::free(q);}
inline int xQueueSend(QueueHandle_t,const void*,unsigned){return pdTRUE;}
inline int xQueueReceive(QueueHandle_t,void*,unsigned){return pdFALSE;}
inline int xQueueReset(QueueHandle_t){return pdTRUE;}
inline void vTaskDelay(unsigned){}
inline int xTaskCreate(void(*)(void*),const char*,unsigned,void*,unsigned,TaskHandle_t* task){if(failure())return pdFALSE;*task=reinterpret_cast<void*>(1);return pdPASS;}
inline int64_t esp_timer_get_time(){return 0;}
inline void esp_fill_random(void* data,size_t n){static uint32_t seed=7;auto* p=static_cast<uint8_t*>(data);while(n--){seed=1664525*seed+1013904223;*p++=uint8_t(seed>>24);}}
inline int nvs_open(const char*,int,nvs_handle_t* handle){if(failure())return -1;*handle=1;++live_stores;return ESP_OK;}
inline void nvs_close(nvs_handle_t){--live_stores;}
inline int nvs_get_blob(nvs_handle_t,const char*,void*,size_t*){return -1;}
inline int nvs_set_blob(nvs_handle_t,const char*,const void*,size_t){return failure()?-1:ESP_OK;}
inline int nvs_commit(nvs_handle_t){return failure()?-1:ESP_OK;}
