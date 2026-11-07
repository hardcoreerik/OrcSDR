#include "orcsdr_rtc.h"

#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pcf8563_protocol.h"

#define RTC_I2C_HZ 100000
#define RTC_I2C_TIMEOUT_MS 100
#define RTC_TICK_WAIT_MS 1200u
#define RTC_TICK_POLL_MS 5u
#define RTC_BOUNDARY_SLACK_US 20000
#define RTC_WRITE_LEAD_US 4000
#define RTC_WRITE_ATTEMPTS 3

static const char *TAG = "RTC";

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static i2c_master_dev_handle_t s_dev;
static esp_timer_handle_t s_write_timer;
static int s_write_attempt;
static bool s_started;
static orcsdr_rtc_status_t s_status = {.last_error = ESP_ERR_INVALID_STATE};

static esp_err_t read_time_regs(uint8_t raw[PCF8563_TIME_LEN])
{
    const uint8_t reg = PCF8563_REG_SECONDS;
    return i2c_master_transmit_receive(s_dev, &reg, sizeof(reg), raw, PCF8563_TIME_LEN, RTC_I2C_TIMEOUT_MS);
}

static esp_err_t write_time_regs(const uint8_t raw[PCF8563_TIME_LEN])
{
    uint8_t frame[1 + PCF8563_TIME_LEN];
    frame[0] = PCF8563_REG_SECONDS;
    memcpy(&frame[1], raw, PCF8563_TIME_LEN);
    return i2c_master_transmit(s_dev, frame, sizeof(frame), RTC_I2C_TIMEOUT_MS);
}

/* Reads and classifies the chip. ESP_OK with *epoch set only when the time decoded, VL is clear and it is in the window. */
static esp_err_t read_trusted_utc(uint32_t *epoch)
{
    uint8_t raw[PCF8563_TIME_LEN];
    esp_err_t err = read_time_regs(raw);
    pcf8563_time_t t;
    bool decoded = false, vl = false;
    uint32_t utc = 0;
    if (err == ESP_OK) {
        decoded = pcf8563_decode(raw, sizeof(raw), &t);
        vl = (raw[0] & PCF8563_VL_BIT) != 0u;
        if (!decoded) err = ESP_ERR_INVALID_RESPONSE;
        else if (vl) err = ESP_ERR_INVALID_STATE;
        else if (!pcf8563_to_epoch(&t, &utc)) err = ESP_ERR_INVALID_RESPONSE;
    }
    portENTER_CRITICAL(&s_mux);
    s_status.vl_flag = err == ESP_ERR_INVALID_STATE && vl;
    s_status.chip_time_valid = err == ESP_OK;
    if (err == ESP_OK) s_status.chip_utc = utc;
    if (err != ESP_OK) s_status.last_error = err;
    portEXIT_CRITICAL(&s_mux);
    if (err == ESP_OK) *epoch = utc;
    return err;
}

static bool system_clock_plausible(void)
{
    const time_t now = time(NULL);
    return now >= (time_t)PCF8563_MIN_UTC && now < (time_t)PCF8563_MAX_UTC;
}

/* Boot: put the system clock on the chip's time. The chip only counts whole seconds, so reading it at a random
 * moment would leave the system clock up to a second behind; wait for the seconds to tick and take the time then. */
static void restore_system_clock(void)
{
    uint32_t first = 0;
    esp_err_t err = read_trusted_utc(&first);
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "chip reports voltage-low: time not trusted (battery lost or clock never set); waiting for a network or manual set");
        return;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "chip time unusable (%s); waiting for a network or manual set", esp_err_to_name(err));
        return;
    }
    if (system_clock_plausible()) {
        ESP_LOGI(TAG, "system clock already set; chip time %lu left as is", (unsigned long)first);
        return;
    }
    uint32_t tick = first;
    const TickType_t started = xTaskGetTickCount();
    while (tick == first && (xTaskGetTickCount() - started) < pdMS_TO_TICKS(RTC_TICK_WAIT_MS)) {
        vTaskDelay(pdMS_TO_TICKS(RTC_TICK_POLL_MS));
        if (read_trusted_utc(&tick) != ESP_OK) return;
    }
    const struct timeval tv = {.tv_sec = (time_t)tick, .tv_usec = 0};
    if (settimeofday(&tv, NULL) != 0) {
        ESP_LOGW(TAG, "could not set the system clock from the chip");
        return;
    }
    portENTER_CRITICAL(&s_mux);
    s_status.restored = true;
    portEXIT_CRITICAL(&s_mux);
    ESP_LOGI(TAG, "system clock restored from PCF8563: %lu UTC%s", (unsigned long)tick, tick == first ? " (no tick seen, up to 1 s early)" : "");
}

static void record_write(esp_err_t err, uint32_t written)
{
    portENTER_CRITICAL(&s_mux);
    if (err == ESP_OK) {
        s_status.last_write_utc = written;
        s_status.writes_ok++;
        s_status.last_error = ESP_OK;
    } else {
        s_status.writes_failed++;
        s_status.last_error = err;
    }
    portEXIT_CRITICAL(&s_mux);
    if (err == ESP_OK) ESP_LOGI(TAG, "PCF8563 set from system clock: %lu UTC", (unsigned long)written);
    else ESP_LOGW(TAG, "PCF8563 write failed: %s", esp_err_to_name(err));
}

static void arm_write_timer(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    /* Fire just after the next whole second of the system clock. The chip counts whole seconds and restarts its
     * sub-second counter on a write, so writing on the boundary keeps it in step with the clock people see. */
    const int64_t delay_us = (int64_t)(1000000 - (int32_t)tv.tv_usec) + RTC_WRITE_LEAD_US;
    esp_timer_start_once(s_write_timer, (uint64_t)delay_us);
}

/* Runs in the shared esp_timer task: two short I2C transactions, no waiting. */
static void write_timer_cb(void *arg)
{
    (void)arg;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    if (!system_clock_plausible()) {
        s_write_attempt = 0;
        record_write(ESP_ERR_INVALID_STATE, 0);
        return;
    }
    if (tv.tv_usec >= RTC_BOUNDARY_SLACK_US) { /* woke late, mid-second: do not write a fractional second */
        if (++s_write_attempt < RTC_WRITE_ATTEMPTS) {
            arm_write_timer();
        } else {
            s_write_attempt = 0;
            record_write(ESP_ERR_TIMEOUT, 0);
        }
        return;
    }
    s_write_attempt = 0;
    esp_err_t err;
    uint32_t written = 0;
    pcf8563_time_t t;
    uint8_t raw[PCF8563_TIME_LEN];
    if (!pcf8563_from_epoch((uint32_t)tv.tv_sec, &t)) {
        err = ESP_ERR_INVALID_ARG;
    } else {
        pcf8563_encode(&t, raw);
        err = write_time_regs(raw);
        uint32_t back = 0;
        if (err == ESP_OK) err = read_trusted_utc(&back);
        /* The read follows the write within milliseconds, so it may already show the next second. */
        if (err == ESP_OK && (back < (uint32_t)tv.tv_sec || back > (uint32_t)tv.tv_sec + 1u)) err = ESP_ERR_INVALID_RESPONSE;
        if (err == ESP_OK) written = (uint32_t)tv.tv_sec;
    }
    record_write(err, written);
}

esp_err_t orcsdr_rtc_start(i2c_master_bus_handle_t bus, uint8_t address_7bit)
{
    if (!bus || address_7bit == 0 || address_7bit > 0x7f) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_mux);
    const bool already = s_started;
    s_started = true;
    portEXIT_CRITICAL(&s_mux);
    if (already) return ESP_ERR_INVALID_STATE;

    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address_7bit,
        .scl_speed_hz = RTC_I2C_HZ,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &s_dev);
    if (err == ESP_OK) {
        /* Control 1 must have TEST1, STOP and TESTC clear or the clock is not counting normally. */
        uint8_t reg = PCF8563_REG_CONTROL1, c1 = 0;
        err = i2c_master_transmit_receive(s_dev, &reg, 1, &c1, 1, RTC_I2C_TIMEOUT_MS);
        if (err == ESP_OK && (c1 & PCF8563_CONTROL1_NORMAL_MASK) != 0u) {
            const uint8_t fix[2] = {PCF8563_REG_CONTROL1, (uint8_t)(c1 & ~PCF8563_CONTROL1_NORMAL_MASK)};
            ESP_LOGW(TAG, "control1 was 0x%02X (stopped or in test mode); clearing", c1);
            err = i2c_master_transmit(s_dev, fix, sizeof(fix), RTC_I2C_TIMEOUT_MS);
        }
        if (err != ESP_OK) {
            i2c_master_bus_rm_device(s_dev);
            s_dev = NULL;
            err = ESP_ERR_NOT_FOUND;
        }
    }
    if (err != ESP_OK) {
        portENTER_CRITICAL(&s_mux);
        s_started = false;
        s_status.last_error = err;
        portEXIT_CRITICAL(&s_mux);
        return err;
    }
    const esp_timer_create_args_t timer_args = {.callback = write_timer_cb, .name = "orcsdr_rtc"};
    err = esp_timer_create(&timer_args, &s_write_timer);
    if (err != ESP_OK) {
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
        portENTER_CRITICAL(&s_mux);
        s_started = false;
        s_status.last_error = err;
        portEXIT_CRITICAL(&s_mux);
        return err;
    }
    portENTER_CRITICAL(&s_mux);
    s_status.running = true;
    s_status.last_error = ESP_OK;
    portEXIT_CRITICAL(&s_mux);
    ESP_LOGI(TAG, "PCF8563 at 0x%02X", address_7bit);
    restore_system_clock(); /* bounded wait for the chip's seconds to tick (at most about 1.2 s) */
    return ESP_OK;
}

esp_err_t orcsdr_rtc_request_write_from_system(void)
{
    portENTER_CRITICAL(&s_mux);
    const bool running = s_status.running;
    portEXIT_CRITICAL(&s_mux);
    if (!running) return ESP_ERR_INVALID_STATE;
    if (esp_timer_is_active(s_write_timer)) return ESP_OK; /* coalesce: the pending write reads the clock when it fires */
    s_write_attempt = 0;
    arm_write_timer();
    return ESP_OK;
}

esp_err_t orcsdr_rtc_get_status(orcsdr_rtc_status_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_mux);
    *out = s_status;
    portEXIT_CRITICAL(&s_mux);
    if (!out->running) {
        memset(out, 0, sizeof(*out));
        out->last_error = ESP_ERR_INVALID_STATE;
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}
