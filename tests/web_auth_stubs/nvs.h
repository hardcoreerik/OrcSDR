#pragma once
#include <cstddef>
#include <cstdint>

using esp_err_t = int;
using nvs_handle_t = uint32_t;
using nvs_iterator_t = void*;
enum nvs_open_mode_t { NVS_READONLY, NVS_READWRITE };
enum nvs_type_t { NVS_TYPE_ANY };
struct nvs_entry_info_t { char key[16]{}; };

#define ESP_OK 0
#define ESP_FAIL (-1)
#define ESP_ERR_NVS_NOT_FOUND (-2)
#define NVS_DEFAULT_PART_NAME "nvs"

extern "C" esp_err_t nvs_open(const char*, nvs_open_mode_t, nvs_handle_t*);
extern "C" void nvs_close(nvs_handle_t);
extern "C" esp_err_t nvs_commit(nvs_handle_t);
extern "C" esp_err_t nvs_get_blob(nvs_handle_t, const char*, void*, size_t*);
extern "C" esp_err_t nvs_get_u8(nvs_handle_t, const char*, uint8_t*);
extern "C" esp_err_t nvs_set_blob(nvs_handle_t, const char*, const void*, size_t);
extern "C" esp_err_t nvs_set_u8(nvs_handle_t, const char*, uint8_t);
extern "C" esp_err_t nvs_entry_find(const char*, const char*, nvs_type_t, nvs_iterator_t*);
extern "C" esp_err_t nvs_entry_info(nvs_iterator_t, nvs_entry_info_t*);
extern "C" esp_err_t nvs_entry_next(nvs_iterator_t*);
