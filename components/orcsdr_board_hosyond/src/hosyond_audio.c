/*
 * Speaker output for the Waveshare ESP32-P4-Module-DEV-KIT: I2S transmit -> ES8311 codec ->
 * NS4150B amplifier -> the onboard speaker connector.
 *
 * Pin source: Espressif's espp board support package for this kit (esp32-p4-module-dev-kit.hpp):
 * I2S MCLK 13, BCLK 12, WS/LRCK 10, DOUT 9 (P4 -> codec DSDIN), DIN 11 (codec ASDOUT -> P4),
 * amplifier enable GPIO 53 (active high), ES8311 at I2C 0x18 on the board's shared I2C bus.
 * The codec register sequence, the volume curve and the amplifier-enable pin are handled by
 * esp_codec_dev. The default volume is low on purpose.
 */
#include <string.h>
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "es8311_codec.h"
#include "esp_log.h"
#include "hosyond_board.h"
#include "orcsdr_board_profile.h"

static const char *TAG = "HOSYOND_AUDIO";

#define AUDIO_MCLK_GPIO (orcsdr_board_profile()->audio_mclk)
#define AUDIO_BCLK_GPIO (orcsdr_board_profile()->audio_bclk)
#define AUDIO_WS_GPIO (orcsdr_board_profile()->audio_ws)
#define AUDIO_DOUT_GPIO (orcsdr_board_profile()->audio_dout)
#define AUDIO_DIN_GPIO (orcsdr_board_profile()->audio_din)
#define AUDIO_PA_GPIO (orcsdr_board_profile()->audio_pa_enable)
#define AUDIO_DEFAULT_VOLUME 100 /* full codec output; the application scales the level */

static i2s_chan_handle_t s_tx;
static esp_codec_dev_handle_t s_dev;
static bool s_running;

esp_err_t hosyond_audio_start(uint32_t sample_rate_hz)
{
    if (s_running) {
        return ESP_OK;
    }
    i2c_master_bus_handle_t bus = hosyond_board_i2c();
    if (!bus) {
        return ESP_ERR_INVALID_STATE;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true; /* send silence rather than stale samples on underrun */
    esp_err_t err = i2s_new_channel(&chan_cfg, &s_tx, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2S channel failed: %s", esp_err_to_name(err));
        return err;
    }
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate_hz),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = AUDIO_MCLK_GPIO,
            .bclk = AUDIO_BCLK_GPIO,
            .ws = AUDIO_WS_GPIO,
            .dout = AUDIO_DOUT_GPIO,
            .din = AUDIO_DIN_GPIO,
            .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false},
        },
    };
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    err = i2s_channel_init_std_mode(s_tx, &std_cfg);
    if (err == ESP_OK) {
        err = i2s_channel_enable(s_tx);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2S init failed: %s", esp_err_to_name(err));
        return err;
    }

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = 1,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = bus,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    es8311_codec_cfg_t es_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = AUDIO_PA_GPIO,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = true,
        .digital_mic = false,
        .hw_gain = {.pa_voltage = 5.0f, .codec_dac_voltage = 3.3f},
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&es_cfg);
    audio_codec_i2s_cfg_t i2s_cfg = {.port = I2S_NUM_0, .rx_handle = NULL, .tx_handle = s_tx};
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    if (!ctrl_if || !gpio_if || !codec_if || !data_if) {
        ESP_LOGE(TAG, "codec interfaces failed (ctrl %p gpio %p codec %p data %p)", (const void *)ctrl_if,
                 (const void *)gpio_if, (const void *)codec_if, (const void *)data_if);
        return ESP_FAIL;
    }
    esp_codec_dev_cfg_t dev_cfg = {.dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = codec_if, .data_if = data_if};
    s_dev = esp_codec_dev_new(&dev_cfg);
    if (!s_dev) {
        ESP_LOGE(TAG, "codec device failed");
        return ESP_FAIL;
    }
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = 2,
        .channel_mask = 0,
        .sample_rate = sample_rate_hz,
        .mclk_multiple = 256,
    };
    if (esp_codec_dev_open(s_dev, &fs) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "codec open failed");
        return ESP_FAIL;
    }
    esp_codec_dev_set_out_vol(s_dev, AUDIO_DEFAULT_VOLUME);
    s_running = true;
    ESP_LOGI(TAG, "speaker output up: %u Hz stereo 16-bit, volume %d%%, amplifier GPIO %d", (unsigned)sample_rate_hz,
             AUDIO_DEFAULT_VOLUME, AUDIO_PA_GPIO);
    return ESP_OK;
}

bool hosyond_audio_running(void)
{
    return s_running;
}

int hosyond_audio_write(const int16_t *interleaved_stereo, int frames)
{
    if (!s_running || !interleaved_stereo || frames <= 0) {
        return 0;
    }
    const int bytes = frames * 2 * (int)sizeof(int16_t);
    return esp_codec_dev_write(s_dev, (void *)interleaved_stereo, bytes) == ESP_CODEC_DEV_OK ? frames : 0;
}

esp_err_t hosyond_audio_set_volume(int volume_0_100)
{
    if (!s_running) {
        return ESP_ERR_INVALID_STATE;
    }
    if (volume_0_100 < 0) {
        volume_0_100 = 0;
    }
    if (volume_0_100 > 100) {
        volume_0_100 = 100;
    }
    return esp_codec_dev_set_out_vol(s_dev, volume_0_100) == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

esp_err_t hosyond_audio_mute(bool mute)
{
    if (!s_running) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_codec_dev_set_out_mute(s_dev, mute) == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}
