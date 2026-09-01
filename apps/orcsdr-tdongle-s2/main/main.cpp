#include <inttypes.h>
#include <atomic>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_rtl_sdr.h"

namespace {
constexpr char kTag[] = "orcsdr_s2";
constexpr uint32_t kResultMagic = 0x4f533253;  // OS2S
constexpr int kWaitForV4Ms = 10000;
constexpr int kCaptureMs = 3000;
constexpr int kScreenWidth = 135;
constexpr int kScreenHeight = 240;
constexpr int kScreenBandHeight = 20;
constexpr gpio_num_t kLcdMosi = GPIO_NUM_35;
constexpr gpio_num_t kLcdSclk = GPIO_NUM_36;
constexpr gpio_num_t kLcdCs = GPIO_NUM_34;
constexpr gpio_num_t kLcdDc = GPIO_NUM_37;
constexpr gpio_num_t kLcdReset = GPIO_NUM_38;
constexpr gpio_num_t kLcdBacklight = GPIO_NUM_33;

enum class Result : uint32_t { None, Pass, NoDevice, StreamError, PowerUnstable };

struct SavedResult {
    uint32_t magic;
    uint32_t result;
    int32_t error;
    uint32_t blocks;
    uint32_t errors;
    uint32_t disconnects;
    uint32_t overruns;
    uint32_t drops;
    uint64_t bytes;
};

struct Runtime {
    std::atomic<uint32_t> blocks{0};
    std::atomic<uint32_t> errors{0};
    std::atomic<uint32_t> disconnects{0};
    std::atomic<uint32_t> bytes{0};
    std::atomic<esp_err_t> last_error{ESP_OK};
};

esp_lcd_panel_handle_t s_panel = nullptr;
SemaphoreHandle_t s_lcd_done = nullptr;
uint16_t s_lcd_band[kScreenWidth * kScreenBandHeight];
Runtime s_runtime;

const char *result_name(Result value) {
    switch (value) {
        case Result::Pass: return "PASS";
        case Result::NoDevice: return "NO V4";
        case Result::StreamError: return "STREAM ERR";
        case Result::PowerUnstable: return "POWER UNST";
        default: return "NO RESULT";
    }
}

void save_result(const SavedResult &result) {
    nvs_handle_t handle;
    if (nvs_open("orcsdr_s2", NVS_READWRITE, &handle) == ESP_OK) {
        (void)nvs_set_blob(handle, "result", &result, sizeof(result));
        (void)nvs_commit(handle);
        nvs_close(handle);
    }
}

bool load_result(SavedResult *result) {
    nvs_handle_t handle;
    size_t size = sizeof(*result);
    const esp_err_t open = nvs_open("orcsdr_s2", NVS_READONLY, &handle);
    if (open != ESP_OK) return false;
    const esp_err_t read = nvs_get_blob(handle, "result", result, &size);
    nvs_close(handle);
    return read == ESP_OK && size == sizeof(*result) && result->magic == kResultMagic;
}

void log_result(const SavedResult &result) {
    printf("RTL_S2_RESULT state=%s err=%" PRId32 " blocks=%" PRIu32
           " errors=%" PRIu32 " disconnects=%" PRIu32 " bytes=%" PRIu64
           " overruns=%" PRIu32 " drops=%" PRIu32 "\n",
           result_name(static_cast<Result>(result.result)), result.error, result.blocks,
           result.errors, result.disconnects, result.bytes, result.overruns, result.drops);
}

uint8_t glyph(char c, int column) {
    static const uint8_t digits[][5] = {
        {0x3e,0x51,0x49,0x45,0x3e},{0x00,0x42,0x7f,0x40,0x00},
        {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4b,0x31},
        {0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},
        {0x3c,0x4a,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
        {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1e},
    };
    static const uint8_t letters[][5] = {
        {0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},
        {0x3e,0x41,0x41,0x41,0x22},{0x7f,0x41,0x41,0x22,0x1c},
        {0x7f,0x49,0x49,0x49,0x41},{0x7f,0x09,0x09,0x09,0x01},
        {0x3e,0x41,0x49,0x49,0x7a},{0x7f,0x08,0x08,0x08,0x7f},
        {0x00,0x41,0x7f,0x41,0x00},{0x20,0x40,0x41,0x3f,0x01},
        {0x7f,0x08,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},
        {0x7f,0x02,0x0c,0x02,0x7f},{0x7f,0x04,0x08,0x10,0x7f},
        {0x3e,0x41,0x41,0x41,0x3e},{0x7f,0x09,0x09,0x09,0x06},
        {0x3e,0x41,0x51,0x21,0x5e},{0x7f,0x09,0x19,0x29,0x46},
        {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7f,0x01,0x01},
        {0x3f,0x40,0x40,0x40,0x3f},{0x1f,0x20,0x40,0x20,0x1f},
        {0x3f,0x40,0x38,0x40,0x3f},{0x63,0x14,0x08,0x14,0x63},
        {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},
    };
    if (c >= '0' && c <= '9') return digits[c - '0'][column];
    if (c >= 'A' && c <= 'Z') return letters[c - 'A'][column];
    if (c == ':') return column == 2 ? 0x24 : 0;
    if (c == '-') return column == 2 ? 0x08 : 0;
    return 0;
}

void text_band(int band_y, int x, int y, const char *value, uint16_t color) {
    for (; *value && x <= kScreenWidth - 10; ++value, x += 12) {
        for (int col = 0; col < 5; ++col) {
            const uint8_t bits = glyph(*value, col);
            for (int row = 0; row < 7; ++row) {
                if (!(bits & (1u << row))) continue;
                for (int dy = 0; dy < 2; ++dy) {
                    const int pixel_y = y + row * 2 + dy;
                    if (pixel_y < band_y || pixel_y >= band_y + kScreenBandHeight) continue;
                    for (int dx = 0; dx < 2; ++dx) {
                        const int pixel_x = x + col * 2 + dx;
                        if (pixel_x < kScreenWidth)
                            s_lcd_band[(pixel_y - band_y) * kScreenWidth + pixel_x] = color;
                    }
                }
            }
        }
    }
}

bool on_color_done(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *) {
    BaseType_t woke = pdFALSE;
    xSemaphoreGiveFromISR(s_lcd_done, &woke);
    return woke == pdTRUE;
}

void screen(const char *state, uint16_t color, esp_err_t detail = ESP_OK) {
    if (!s_panel || !s_lcd_done) return;
    char blocks[16];
    char errors[16];
    char detail_text[16];
    snprintf(blocks, sizeof(blocks), "B:%" PRIu32, s_runtime.blocks.load());
    snprintf(errors, sizeof(errors), "E:%" PRIu32, s_runtime.errors.load());
    snprintf(detail_text, sizeof(detail_text), "X:%" PRId32, static_cast<int32_t>(detail));
    for (int y = 0; y < kScreenHeight; y += kScreenBandHeight) {
        memset(s_lcd_band, 0, sizeof(s_lcd_band));
        text_band(y, 6, 16, "ORCSDR S2", 0xffff);
        text_band(y, 6, 52, state, color);
        text_band(y, 6, 88, blocks, 0x07ff);
        text_band(y, 6, 112, errors, 0xf800);
        if (detail != ESP_OK) text_band(y, 6, 136, detail_text, 0xffe0);
        while (xSemaphoreTake(s_lcd_done, 0) == pdTRUE) {}
        if (esp_lcd_panel_draw_bitmap(s_panel, 0, y, kScreenWidth, y + kScreenBandHeight, s_lcd_band) != ESP_OK ||
            xSemaphoreTake(s_lcd_done, pdMS_TO_TICKS(100)) != pdTRUE) {
            ESP_LOGE(kTag, "LCD status transfer failed");
            return;
        }
    }
}

esp_err_t init_screen() {
    spi_bus_config_t bus = {};
    bus.sclk_io_num = kLcdSclk;
    bus.mosi_io_num = kLcdMosi;
    bus.miso_io_num = -1;
    bus.max_transfer_sz = kScreenWidth * kScreenBandHeight * sizeof(uint16_t);
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO), kTag, "spi");
    esp_lcd_panel_io_spi_config_t io = {};
    io.dc_gpio_num = kLcdDc;
    io.cs_gpio_num = kLcdCs;
    io.pclk_hz = 27000000;
    io.lcd_cmd_bits = 8;
    io.lcd_param_bits = 8;
    io.spi_mode = 0;
    io.trans_queue_depth = 1;
    s_lcd_done = xSemaphoreCreateBinary();
    if (!s_lcd_done) return ESP_ERR_NO_MEM;
    io.on_color_trans_done = on_color_done;
    esp_lcd_panel_io_handle_t panel_io = nullptr;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(SPI2_HOST, &io, &panel_io), kTag, "panel io");
    esp_lcd_panel_dev_config_t config = {};
    config.reset_gpio_num = kLcdReset;
    config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
    config.bits_per_pixel = 16;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(panel_io, &config, &s_panel), kTag, "st7789");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), kTag, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), kTag, "init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, true), kTag, "invert");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(s_panel, false), kTag, "swap");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, false, false), kTag, "mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(s_panel, 53, 40), kTag, "gap");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), kTag, "display on");
    gpio_set_direction(kLcdBacklight, GPIO_MODE_OUTPUT);
    gpio_set_level(kLcdBacklight, 1);
    return ESP_OK;
}

void on_event(esp_rtl_sdr_event_t event, const void *payload, void *) {
    if (event == ESP_RTL_SDR_EVT_IQ_BLOCK && payload) {
        const auto *block = static_cast<const esp_rtl_sdr_iq_block_t *>(payload);
        s_runtime.blocks.fetch_add(1);
        s_runtime.bytes.fetch_add(block->bytes);
    } else if (event == ESP_RTL_SDR_EVT_ERROR && payload) {
        s_runtime.errors.fetch_add(1);
        s_runtime.last_error = static_cast<const esp_rtl_sdr_error_info_t *>(payload)->code;
        ESP_LOGW(kTag, "RTL_S2_EVENT error=%s", esp_err_to_name(s_runtime.last_error.load()));
    } else if (event == ESP_RTL_SDR_EVT_DISCONNECTED) {
        s_runtime.disconnects.fetch_add(1);
        ESP_LOGW(kTag, "RTL_S2_EVENT disconnected");
    }
}

bool wait_for_v4(esp_rtl_sdr_handle_t sdr) {
    ESP_LOGI(kTag, "RTL_S2_STATE waiting_for_v4");
    for (int elapsed = 0; elapsed < kWaitForV4Ms; elapsed += 250) {
        size_t count = 0;
        (void)esp_rtl_sdr_refresh_device_list(sdr);
        (void)esp_rtl_sdr_get_device_count(sdr, &count);
        if (count) {
            ESP_LOGI(kTag, "RTL_S2_STATE v4_detected count=%u", static_cast<unsigned>(count));
            return true;
        }
        screen("WAIT V4", 0xffe0);
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    return false;
}

}  // namespace

extern "C" void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_LOGI(kTag, "RTL_S2_STATE boot");
    ESP_ERROR_CHECK(init_screen());
    ESP_LOGI(kTag, "RTL_S2_STATE display_ready");
    SavedResult previous{};
    if (load_result(&previous)) log_result(previous);
    screen("BOOT", 0xffff);

    esp_rtl_sdr_config_t config;
    esp_rtl_sdr_config_default(&config);
    config.transfer_count = 2;
    config.transfer_bytes = 4096;
    config.delivery_mode = ESP_RTL_SDR_DELIVERY_CALLBACK;
    config.event_cb = on_event;
    ESP_ERROR_CHECK(esp_rtl_sdr_config_validate(&config));

    esp_rtl_sdr_handle_t sdr = nullptr;
    const esp_err_t install = esp_rtl_sdr_install(&config, &sdr);
    if (install != ESP_OK) {
        SavedResult saved{kResultMagic, static_cast<uint32_t>(Result::StreamError),
                          static_cast<int32_t>(install), 0, 1, 0, 0, 0, 0};
        save_result(saved);
        screen("HOST ERR", 0xf800, install);
        return;
    }
    ESP_LOGI(kTag, "RTL_S2_STATE usb_host_ready");
    if (!wait_for_v4(sdr)) {
        screen("NO V4", 0xf800);
        (void)esp_rtl_sdr_uninstall(sdr);
        return;
    }

    screen("STREAM", 0x07ff);
    esp_rtl_sdr_stream_config_t stream;
    esp_rtl_sdr_stream_config_default(&stream);
    stream.preset = ESP_RTL_SDR_PRESET_KZEL_96_1;
    stream.sample_rate_sps = ESP_RTL_SDR_RATE_960K;
    const esp_err_t start = esp_rtl_sdr_start(sdr, &stream);
    ESP_LOGI(kTag, "RTL_S2_STATE stream_start=%s", esp_err_to_name(start));
    Result result = Result::StreamError;
    const char *final_state = "START ERR";
    esp_err_t final_error = start;
    if (start == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(kCaptureMs));
        const esp_err_t stop = esp_rtl_sdr_stop(sdr, 1500);
        ESP_LOGI(kTag, "RTL_S2_STATE stream_stop=%s", esp_err_to_name(stop));
        if (s_runtime.disconnects.load()) {
            result = Result::PowerUnstable;
            final_state = "POWER UNST";
        } else if (stop != ESP_OK) {
            final_state = "STOP ERR";
            final_error = stop;
        } else if (s_runtime.errors.load()) {
            final_state = "IQ ERR";
            final_error = s_runtime.last_error.load();
        } else if (!s_runtime.blocks.load()) {
            final_state = "NO IQ";
        } else {
            result = Result::Pass;
            final_state = "PASS";
            final_error = ESP_OK;
        }
    }

    esp_rtl_sdr_metrics_t metrics{};
    (void)esp_rtl_sdr_get_metrics(sdr, &metrics);
    SavedResult saved{kResultMagic, static_cast<uint32_t>(result),
                      static_cast<int32_t>(final_error),
                      s_runtime.blocks.load(), s_runtime.errors.load(), s_runtime.disconnects.load(),
                      metrics.overruns, metrics.consumer_drops, s_runtime.bytes.load()};
    save_result(saved);
    log_result(saved);
    screen(final_state, result == Result::Pass ? 0x07e0 : 0xf800, final_error);
    ESP_LOGI(kTag, "bounded stream %s", result_name(result));
    (void)esp_rtl_sdr_uninstall(sdr);
}
