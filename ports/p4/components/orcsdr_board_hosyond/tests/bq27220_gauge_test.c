#include "bq27220_gauge.h"
#include "fake_i2c.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define assert(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "assertion failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

#define STATUS_DSG (1u << 0)
#define STATUS_BATTPRES (1u << 3)
#define STATUS_CHGINH (1u << 8)
#define STATUS_FC (1u << 9)
#define STATUS_FD (1u << 15)

static bq27220_gauge_t *make_gauge(void)
{
    bq27220_gauge_t *gauge = NULL;
    assert(bq27220_gauge_create((void *)0x1, 0x55, &gauge) == ESP_OK);
    assert(gauge != NULL);
    assert(fake_i2c_last_address() == 0x55);
    assert(fake_i2c_last_speed() == 100000);
    return gauge;
}

static void test_decodes_words_flags_and_signed_current(void)
{
    fake_i2c_reset();
    fake_i2c_set_word(0x0A, STATUS_DSG | STATUS_BATTPRES | STATUS_CHGINH | STATUS_FC | STATUS_FD);
    fake_i2c_set_word(0x08, 0x1234);
    fake_i2c_set_word(0x2C, 87);
    fake_i2c_set_word(0x0C, (uint16_t)-325);
    bq27220_gauge_t *gauge = make_gauge();
    bq27220_gauge_sample_t sample = {0};
    assert(bq27220_gauge_read(gauge, &sample) == ESP_OK);
    assert(sample.valid);
    assert(sample.battery_status == (STATUS_DSG | STATUS_BATTPRES | STATUS_CHGINH | STATUS_FC | STATUS_FD));
    assert(sample.voltage_mv == 0x1234);
    assert(sample.state_of_charge_percent == 87);
    assert(sample.current_ma == -325);
    assert(sample.discharging && sample.battery_present_reported);
    assert(sample.charge_inhibited && sample.full_charge && sample.full_discharge);
    assert(fake_i2c_transfer_count() == 4);
    bq27220_gauge_destroy(gauge);
    assert(fake_i2c_remove_count() == 1);
}

static void test_absent_or_failing_device_invalidates_sample(void)
{
    fake_i2c_reset();
    fake_i2c_set_word(0x2C, 50);
    bq27220_gauge_t *gauge = make_gauge();
    fake_i2c_fail_transfer(1, ESP_ERR_TIMEOUT); /* No response from absent gauge. */
    bq27220_gauge_sample_t sample = {.valid = true, .voltage_mv = 5000};
    assert(bq27220_gauge_read(gauge, &sample) == ESP_ERR_TIMEOUT);
    assert(!sample.valid && sample.voltage_mv == 0 && sample.state_of_charge_percent == 0);
    assert(fake_i2c_transfer_count() == 1);
    bq27220_gauge_destroy(gauge);
    assert(fake_i2c_remove_count() == 1);
}

static void test_partial_read_failure_invalidates_sample(void)
{
    fake_i2c_reset();
    fake_i2c_set_word(0x2C, 50);
    bq27220_gauge_t *gauge = make_gauge();
    fake_i2c_fail_transfer(3, ESP_FAIL);
    bq27220_gauge_sample_t sample = {.valid = true, .current_ma = 100};
    assert(bq27220_gauge_read(gauge, &sample) == ESP_FAIL);
    assert(!sample.valid && sample.current_ma == 0 && sample.battery_status == 0);
    assert(fake_i2c_transfer_count() == 3);
    bq27220_gauge_destroy(gauge);
}

static void test_invalid_soc_is_unavailable(void)
{
    fake_i2c_reset();
    fake_i2c_set_word(0x2C, 101);
    bq27220_gauge_t *gauge = make_gauge();
    bq27220_gauge_sample_t sample = {.valid = true, .state_of_charge_percent = 10};
    assert(bq27220_gauge_read(gauge, &sample) == ESP_ERR_INVALID_RESPONSE);
    assert(!sample.valid && sample.state_of_charge_percent == 0);
    assert(fake_i2c_transfer_count() == 3);
    bq27220_gauge_destroy(gauge);
}

static void test_argument_and_init_cleanup(void)
{
    fake_i2c_reset();
    bq27220_gauge_t *gauge = (bq27220_gauge_t *)0x1;
    assert(bq27220_gauge_create(NULL, 0x55, &gauge) == ESP_ERR_INVALID_ARG);
    assert(gauge == NULL);
    assert(bq27220_gauge_create((void *)0x1, 0x80, &gauge) == ESP_ERR_INVALID_ARG);
    assert(gauge == NULL);
    assert(bq27220_gauge_create((void *)0x1, 0x55, NULL) == ESP_ERR_INVALID_ARG);
    fake_i2c_fail_add(ESP_FAIL);
    assert(bq27220_gauge_create((void *)0x1, 0x55, &gauge) == ESP_FAIL);
    assert(gauge == NULL && fake_i2c_remove_count() == 0);
    bq27220_gauge_sample_t sample = {.valid = true};
    assert(bq27220_gauge_read(NULL, &sample) == ESP_ERR_INVALID_STATE);
    assert(!sample.valid);
    bq27220_gauge_destroy(NULL);
}

int main(void)
{
    test_decodes_words_flags_and_signed_current();
    test_absent_or_failing_device_invalidates_sample();
    test_partial_read_failure_invalidates_sample();
    test_invalid_soc_is_unavailable();
    test_argument_and_init_cleanup();
    puts("bq27220 gauge tests passed");
    return 0;
}
