#include "orcsdr_battery.h"

#include <limits.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BATTERY_POLL_PERIOD_MS 1000u
#define BATTERY_STALE_AFTER_MS 3000u
#define BATTERY_WORKER_STACK_WORDS 3072u
#define BATTERY_WORKER_PRIORITY (tskIDLE_PRIORITY + 1u)

typedef enum {
    SERVICE_STOPPED = 0,
    SERVICE_STARTING,
    SERVICE_RUNNING,
} service_state_t;

static portMUX_TYPE s_lifecycle_mux = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_snapshot_mux = portMUX_INITIALIZER_UNLOCKED;
static service_state_t s_state;
static bq27220_gauge_t *s_gauge;
static TaskHandle_t s_worker;
static orcsdr_battery_snapshot_t s_snapshot = {
    .last_error = ESP_ERR_INVALID_STATE,
    .stale = true,
    .last_success_age_ms = UINT32_MAX,
    .ui_percent = -1,
};
static TickType_t s_last_success_tick;
static bool s_has_success;

static uint32_t ticks_to_ms(TickType_t ticks)
{
    const uint64_t ms = ((uint64_t)ticks * 1000u) / configTICK_RATE_HZ;
    return ms > UINT32_MAX ? UINT32_MAX : (uint32_t)ms;
}

static void battery_poll_once(void)
{
    bq27220_gauge_sample_t sample;
    const esp_err_t err = bq27220_gauge_read(s_gauge, &sample);
    const TickType_t now = xTaskGetTickCount();

    portENTER_CRITICAL(&s_snapshot_mux);
    if (err == ESP_OK && sample.valid) {
        s_snapshot.gauge = sample;
        s_snapshot.gauge_seen = true;
        s_snapshot.last_error = ESP_OK;
        s_last_success_tick = now;
        s_has_success = true;
    } else {
        memset(&s_snapshot.gauge, 0, sizeof(s_snapshot.gauge));
        s_snapshot.last_error = err == ESP_OK ? ESP_ERR_INVALID_RESPONSE : err;
    }
    portEXIT_CRITICAL(&s_snapshot_mux);
}

static void battery_worker(void *arg)
{
    (void)arg;
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        battery_poll_once();
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(BATTERY_POLL_PERIOD_MS));
    }
}

esp_err_t orcsdr_battery_start(i2c_master_bus_handle_t bus, uint8_t address_7bit)
{
    if (!bus || address_7bit == 0 || address_7bit > 0x7f) return ESP_ERR_INVALID_ARG;

    portENTER_CRITICAL(&s_lifecycle_mux);
    if (s_state != SERVICE_STOPPED) {
        portEXIT_CRITICAL(&s_lifecycle_mux);
        return ESP_ERR_INVALID_STATE;
    }
    s_state = SERVICE_STARTING;
    portEXIT_CRITICAL(&s_lifecycle_mux);

    bq27220_gauge_t *gauge = NULL;
    esp_err_t err = bq27220_gauge_create(bus, address_7bit, &gauge);
    if (err != ESP_OK) {
        portENTER_CRITICAL(&s_lifecycle_mux);
        s_state = SERVICE_STOPPED;
        portEXIT_CRITICAL(&s_lifecycle_mux);
        portENTER_CRITICAL(&s_snapshot_mux);
        s_snapshot.last_error = err;
        portEXIT_CRITICAL(&s_snapshot_mux);
        return err;
    }

    s_gauge = gauge;
    portENTER_CRITICAL(&s_snapshot_mux);
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_snapshot.last_error = ESP_ERR_INVALID_STATE;
    s_snapshot.stale = true;
    s_snapshot.last_success_age_ms = UINT32_MAX;
    s_snapshot.ui_percent = -1;
    s_has_success = false;
    s_last_success_tick = 0;
    portEXIT_CRITICAL(&s_snapshot_mux);

    TaskHandle_t worker = NULL;
    if (xTaskCreate(battery_worker, "orcsdr_battery", BATTERY_WORKER_STACK_WORDS,
                    NULL, BATTERY_WORKER_PRIORITY, &worker) != pdPASS) {
        s_gauge = NULL;
        bq27220_gauge_destroy(gauge);
        portENTER_CRITICAL(&s_lifecycle_mux);
        s_state = SERVICE_STOPPED;
        portEXIT_CRITICAL(&s_lifecycle_mux);
        portENTER_CRITICAL(&s_snapshot_mux);
        s_snapshot.last_error = ESP_ERR_NO_MEM;
        portEXIT_CRITICAL(&s_snapshot_mux);
        return ESP_FAIL;
    }

    portENTER_CRITICAL(&s_lifecycle_mux);
    s_worker = worker;
    s_state = SERVICE_RUNNING;
    portEXIT_CRITICAL(&s_lifecycle_mux);
    return ESP_OK;
}

esp_err_t orcsdr_battery_get_snapshot(orcsdr_battery_snapshot_t *out_snapshot)
{
    if (!out_snapshot) return ESP_ERR_INVALID_ARG;

    portENTER_CRITICAL(&s_lifecycle_mux);
    const bool running = s_state == SERVICE_RUNNING;
    portEXIT_CRITICAL(&s_lifecycle_mux);
    if (!running) {
        memset(out_snapshot, 0, sizeof(*out_snapshot));
        out_snapshot->last_error = ESP_ERR_INVALID_STATE;
        out_snapshot->stale = true;
        out_snapshot->last_success_age_ms = UINT32_MAX;
        out_snapshot->ui_percent = -1;
        return ESP_ERR_INVALID_STATE;
    }

    portENTER_CRITICAL(&s_snapshot_mux);
    *out_snapshot = s_snapshot;
    const bool has_success = s_has_success;
    const TickType_t last_success_tick = s_last_success_tick;
    portEXIT_CRITICAL(&s_snapshot_mux);

    const TickType_t now = xTaskGetTickCount();
    const uint32_t age_ms = has_success ? ticks_to_ms((TickType_t)(now - last_success_tick)) : UINT32_MAX;
    out_snapshot->last_success_age_ms = age_ms;
    out_snapshot->stale = !has_success || age_ms >= BATTERY_STALE_AFTER_MS;
    out_snapshot->valid = !out_snapshot->stale && out_snapshot->gauge.valid &&
                          out_snapshot->gauge.battery_present_reported &&
                          out_snapshot->gauge.voltage_mv != 0;
    out_snapshot->ui_percent = out_snapshot->valid ? out_snapshot->gauge.state_of_charge_percent : -1;
    return ESP_OK;
}

#ifdef ORCSDR_BATTERY_TESTING
void orcsdr_battery_test_poll_once(void)
{
    if (s_gauge) battery_poll_once();
}
#endif
