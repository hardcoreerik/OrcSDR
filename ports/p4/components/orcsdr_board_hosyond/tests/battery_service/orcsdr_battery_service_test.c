#include "orcsdr_battery.h"
#include "fake_freertos.h"
#include "fake_i2c.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STATUS_BATTPRES (1u << 3)

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "check failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

static void prepare_gauge(uint16_t status, uint16_t voltage_mv, uint16_t soc, uint16_t current)
{
    fake_i2c_reset();
    fake_freertos_reset();
    fake_i2c_set_word(0x0A, status);
    fake_i2c_set_word(0x08, voltage_mv);
    fake_i2c_set_word(0x2C, soc);
    fake_i2c_set_word(0x0C, current);
}

static void start_service(void)
{
    CHECK(orcsdr_battery_start((void *)0x1, 0x55) == ESP_OK);
    CHECK(fake_freertos_task_create_count() == 1);
    CHECK(fake_freertos_last_priority() == tskIDLE_PRIORITY + 1u);
    CHECK(fake_freertos_last_stack_depth() > 0 && fake_freertos_last_stack_depth() <= 4096);
}

static void test_valid_snapshot(void)
{
    prepare_gauge(STATUS_BATTPRES, 3712, 64, (uint16_t)-125);
    start_service();
    orcsdr_battery_test_poll_once();
    orcsdr_battery_snapshot_t snapshot;
    CHECK(orcsdr_battery_get_snapshot(&snapshot) == ESP_OK);
    CHECK(snapshot.gauge_seen && snapshot.valid && !snapshot.stale);
    CHECK(snapshot.ui_percent == 64);
    CHECK(snapshot.gauge.valid && snapshot.gauge.voltage_mv == 3712);
    CHECK(snapshot.gauge.current_ma == -125);
    CHECK(snapshot.last_error == ESP_OK && snapshot.last_success_age_ms == 0);
}

static void test_absent_or_unreported_cell_never_becomes_zero_percent(void)
{
    prepare_gauge(0, 0, 0, 0);
    start_service();
    orcsdr_battery_test_poll_once();
    orcsdr_battery_snapshot_t snapshot;
    CHECK(orcsdr_battery_get_snapshot(&snapshot) == ESP_OK);
    CHECK(snapshot.gauge_seen && snapshot.gauge.valid);
    CHECK(!snapshot.gauge.battery_present_reported && snapshot.gauge.voltage_mv == 0);
    CHECK(!snapshot.valid && snapshot.ui_percent == -1 && snapshot.last_error == ESP_OK);
}

static void test_failed_read_clears_raw_snapshot(void)
{
    prepare_gauge(STATUS_BATTPRES, 3800, 72, 5);
    start_service();
    orcsdr_battery_test_poll_once();
    fake_i2c_fail_transfer(5, ESP_ERR_TIMEOUT);
    fake_freertos_advance_tick(1000);
    orcsdr_battery_test_poll_once();
    orcsdr_battery_snapshot_t snapshot;
    CHECK(orcsdr_battery_get_snapshot(&snapshot) == ESP_OK);
    CHECK(snapshot.gauge_seen && !snapshot.valid && snapshot.ui_percent == -1);
    CHECK(!snapshot.gauge.valid && snapshot.gauge.voltage_mv == 0);
    CHECK(snapshot.last_error == ESP_ERR_TIMEOUT);
    CHECK(snapshot.last_success_age_ms == 1000 && !snapshot.stale);
}

static void test_snapshot_expires_after_three_seconds(void)
{
    prepare_gauge(STATUS_BATTPRES, 3990, 100, 0);
    start_service();
    orcsdr_battery_test_poll_once();
    orcsdr_battery_snapshot_t snapshot;
    fake_freertos_advance_tick(2999);
    CHECK(orcsdr_battery_get_snapshot(&snapshot) == ESP_OK);
    CHECK(snapshot.valid && !snapshot.stale && snapshot.ui_percent == 100);
    fake_freertos_advance_tick(1);
    CHECK(orcsdr_battery_get_snapshot(&snapshot) == ESP_OK);
    CHECK(!snapshot.valid && snapshot.stale && snapshot.ui_percent == -1);
    CHECK(snapshot.gauge.valid && snapshot.last_success_age_ms == 3000);
}

static void test_duplicate_start_does_not_create_worker_or_device(void)
{
    prepare_gauge(STATUS_BATTPRES, 3700, 50, 0);
    start_service();
    CHECK(orcsdr_battery_start((void *)0x1, 0x55) == ESP_ERR_INVALID_STATE);
    CHECK(fake_freertos_task_create_count() == 1);
    CHECK(fake_i2c_remove_count() == 0);
}

static void test_task_create_failure_cleans_handle_and_can_retry(void)
{
    prepare_gauge(STATUS_BATTPRES, 3700, 50, 0);
    fake_freertos_fail_next_task_create();
    CHECK(orcsdr_battery_start((void *)0x1, 0x55) == ESP_FAIL);
    CHECK(fake_i2c_remove_count() == 1);
    CHECK(orcsdr_battery_start((void *)0x1, 0x55) == ESP_OK);
    CHECK(fake_freertos_task_create_count() == 2);
    CHECK(fake_i2c_remove_count() == 1);
}

static void test_bus_add_failure_does_not_create_worker(void)
{
    prepare_gauge(STATUS_BATTPRES, 3700, 50, 0);
    fake_i2c_fail_add(ESP_FAIL);
    CHECK(orcsdr_battery_start((void *)0x1, 0x55) == ESP_FAIL);
    CHECK(fake_freertos_task_create_count() == 0);
    CHECK(fake_i2c_remove_count() == 0);
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    if (strcmp(argv[1], "valid_snapshot") == 0) test_valid_snapshot();
    else if (strcmp(argv[1], "absent_cell") == 0) test_absent_or_unreported_cell_never_becomes_zero_percent();
    else if (strcmp(argv[1], "read_failure") == 0) test_failed_read_clears_raw_snapshot();
    else if (strcmp(argv[1], "stale_snapshot") == 0) test_snapshot_expires_after_three_seconds();
    else if (strcmp(argv[1], "duplicate_start") == 0) test_duplicate_start_does_not_create_worker_or_device();
    else if (strcmp(argv[1], "task_failure") == 0) test_task_create_failure_cleans_handle_and_can_retry();
    else if (strcmp(argv[1], "bus_failure") == 0) test_bus_add_failure_does_not_create_worker();
    else return 2;
    puts("battery service scenario passed");
    return 0;
}
