/*
 * LilyGO T-Display-P4 V1.0, 4.1" AMOLED assembly: RM69A10 panel on a 2-lane MIPI-DSI link.
 *
 * Unlike the Hosyond kit there is no bridge chip: the panel is driven straight from the P4's DPI
 * controller. Its reset line is on the XL9535 expander (IO2), so the order is:
 *   expander (3V3 rail on = IO0 low, panel reset pulse) -> DPHY LDO -> DSI bus -> DBI IO -> init commands in
 *   low-power mode -> DPI panel start (video runs) -> brightness.
 *
 * The expander is only touched with read-modify-write on the pins named here; every other line keeps
 * whatever level it had (audio power, C6 enable, SD power, radio reset ...).
 *
 * Facts used (written here from the board's published numbers, not copied code): timings and the pin map
 * are in boards/board_lilygo_t_display_p4_amoled.c; the panel's command list is the 12-command DCS
 * sequence LilyGO's Apache-2.0 example (T-Halow-P4 examples/rm69a10_lvgl, rm69a10_driver.cpp) sends:
 * page switch 0xFE/0x80, address windows 0x2A/0x2B, partial window 0x30/0x31, NORON 0x12, TEON 0x35,
 * brightness 0x51, sleep-out 0x11 with 120 ms, display-on 0x29.
 */
#include <stdio.h>
#include <string.h>
#include "esp_cache.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_idf_version.h"
#include "hosyond_board.h"
#include "orcsdr_board_profile.h"
#include "lilygo_amoled.h"

static const char *TAG = "LILYGO_AMOLED";

#define DSI_PHY_LDO_CHAN 3
#define DSI_PHY_LDO_MV 2500
#define PANEL_LDO_CHAN 4
#define PANEL_LDO_MV 3300

#define PANEL_W 568
#define PANEL_H 1232
#define DPI_MHZ 48 /* 60 MHz gave whole-screen flashes under load in LakeShark; 48 MHz is ~40 Hz */
#define LANE_MBPS 1000
#define LANE_COUNT 2
#define H_SW 50
#define H_BP 150
#define H_FP 50
#define V_SW 40
#define V_BP 120
#define V_FP 80

/* XL9535 (TCA9535-compatible register map): 0x00/0x01 input, 0x02/0x03 output, 0x06/0x07 direction (1 = input). */
#define XL_IN0 0x00
#define XL_OUT0 0x02
#define XL_CFG0 0x06
#define XL_PIN_POWER_3V3 0
#define XL_PIN_SCREEN_RST 2

static esp_ldo_channel_handle_t s_ldo;
static esp_ldo_channel_handle_t s_panel_ldo;
static esp_lcd_dsi_bus_handle_t s_bus;
static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_panel_handle_t s_panel;

typedef struct {
    uint8_t cmd;
    uint8_t data[4];
    uint8_t len;
    uint16_t delay_ms;
} dcs_cmd_t;

static const dcs_cmd_t k_init[] = {
    {0xFE, {0xFD}, 1, 0},
    {0x80, {0xFC}, 1, 0},
    {0xFE, {0x00}, 1, 0},
    {0x2A, {0x00, 0x00, 0x02, 0x37}, 4, 0}, /* columns 0..567 */
    {0x2B, {0x00, 0x00, 0x04, 0xCF}, 4, 0}, /* rows 0..1231 */
    {0x31, {0x00, 0x03, 0x02, 0x34}, 4, 0},
    {0x30, {0x00, 0x00, 0x04, 0xCF}, 4, 0},
    {0x12, {0}, 0, 0},                      /* normal display mode */
    {0x35, {0x00}, 1, 0},                   /* tearing effect line on */
    {0x51, {0xFE}, 1, 0},                   /* brightness */
    {0x11, {0}, 0, 120},                    /* sleep out */
    {0x29, {0}, 0, 0},                      /* display on */
};

/* ---- expander ---- */

static esp_err_t xl_read(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(dev, &reg, 1, val, 1, 50);
}

static esp_err_t xl_write(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val)
{
    const uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(dev, buf, sizeof(buf), 50);
}

/* Drive one port-0 pin as an output at `level`, leaving the others alone. */
static esp_err_t xl_drive(i2c_master_dev_handle_t dev, int pin, int level)
{
    uint8_t out = 0, cfg = 0;
    esp_err_t err = xl_read(dev, XL_OUT0, &out);
    err |= xl_read(dev, XL_CFG0, &cfg);
    if (err != ESP_OK) {
        return err;
    }
    out = level ? (uint8_t)(out | (1u << pin)) : (uint8_t)(out & ~(1u << pin));
    err = xl_write(dev, XL_OUT0, out); /* level first, so the pin never glitches when it becomes an output */
    err |= xl_write(dev, XL_CFG0, (uint8_t)(cfg & ~(1u << pin)));
    return err;
}

static esp_err_t expander_power_and_reset(i2c_master_bus_handle_t bus)
{
    i2c_master_dev_handle_t dev;
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = (uint16_t)orcsdr_board_profile()->ioexp_addr[0],
        .scl_speed_hz = 100000,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &dev);
    if (err != ESP_OK) {
        return err;
    }
    uint8_t in0 = 0xFF, in1 = 0xFF, out0 = 0xFF, cfg0 = 0xFF;
    xl_read(dev, XL_IN0, &in0);
    xl_read(dev, XL_IN0 + 1, &in1);
    xl_read(dev, XL_OUT0, &out0);
    xl_read(dev, XL_CFG0, &cfg0);
    ESP_LOGI(TAG, "XL9535 before: in=%02X%02X out0=%02X dir0=%02X", in1, in0, out0, cfg0);

    /* IO0 is the board's 3V3 rail enable and it is ACTIVE LOW: 0 = rail on (LilyGO's driver drives it to 0 and
     * warns it must stay enabled). Driving it high cuts the supply to the panel side. */
    err = xl_drive(dev, XL_PIN_POWER_3V3, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    err |= xl_drive(dev, XL_PIN_SCREEN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    err |= xl_drive(dev, XL_PIN_SCREEN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    err |= xl_drive(dev, XL_PIN_SCREEN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    xl_read(dev, XL_OUT0, &out0);
    xl_read(dev, XL_CFG0, &cfg0);
    ESP_LOGI(TAG, "XL9535 after: out0=%02X dir0=%02X (panel reset pulsed on IO%d)", out0, cfg0, XL_PIN_SCREEN_RST);
    i2c_master_bus_rm_device(dev);
    return err == ESP_OK ? ESP_OK : ESP_FAIL;
}

/* ---- panel ---- */

static volatile uint32_t s_refresh_count;

static bool on_refresh_done(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *edata, void *user_ctx)
{
    (void)panel;
    (void)edata;
    (void)user_ctx;
    s_refresh_count++;
    return false;
}

/* DCS reads: what the panel says about itself (power mode 0x0A, MADCTL 0x0B, pixel format 0x0C, image mode
 * 0x0D, signal mode 0x0E, self-diagnostic 0x0F, ID 0x04). A panel that hears us returns plausible values; one
 * that does not returns zeros or an error. */
static void log_panel_state(const char *when)
{
    static const uint8_t regs[] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
    char line[96];
    size_t n = 0;
    for (size_t i = 0; i < sizeof(regs); ++i) {
        uint8_t v = 0xEE;
        const esp_err_t e = esp_lcd_panel_io_rx_param(s_io, regs[i], &v, 1);
        n += (size_t)snprintf(line + n, sizeof(line) - n, "%02X=%s%02X ", regs[i], e == ESP_OK ? "" : "ERR:", v);
    }
    uint8_t id[3] = {0xEE, 0xEE, 0xEE};
    const esp_err_t e = esp_lcd_panel_io_rx_param(s_io, 0x04, id, sizeof(id));
    uint8_t da = 0xEE;
    esp_lcd_panel_io_rx_param(s_io, 0xDA, &da, 1);
    ESP_LOGI(TAG, "panel %s: %s| RDDID %s%02X %02X %02X, 0xDA=%02X", when, line, e == ESP_OK ? "" : "ERR:", id[0],
             id[1], id[2], da);
}

esp_err_t lilygo_amoled_start(i2c_master_bus_handle_t bus, hosyond_display_t *out)
{
    /* The AMOLED's supply comes from the P4's own LDO channel 4 (3.3 V), not from a board regulator the
     * expander switches; without it the panel is unpowered and every DCS read returns zero. The DPHY LDO is
     * channel 3. Both before the panel reset. */
    esp_ldo_channel_config_t ldo_cfg = {.chan_id = DSI_PHY_LDO_CHAN, .voltage_mv = DSI_PHY_LDO_MV};
    esp_err_t err = esp_ldo_acquire_channel(&ldo_cfg, &s_ldo);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MIPI DPHY LDO failed: %s", esp_err_to_name(err));
        return err;
    }
    ldo_cfg.chan_id = PANEL_LDO_CHAN;
    ldo_cfg.voltage_mv = PANEL_LDO_MV;
    err = esp_ldo_acquire_channel(&ldo_cfg, &s_panel_ldo);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel supply LDO failed: %s", esp_err_to_name(err));
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(20));

    err = expander_power_and_reset(bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "expander failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_lcd_dsi_bus_config_t bus_cfg = {
        .bus_id = 0,
        .num_data_lanes = LANE_COUNT,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = LANE_MBPS,
    };
    err = esp_lcd_new_dsi_bus(&bus_cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "DSI bus failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_lcd_dbi_io_config_t dbi = {.virtual_channel = 0, .lcd_cmd_bits = 8, .lcd_param_bits = 8};
    err = esp_lcd_new_panel_io_dbi(s_bus, &dbi, &s_io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "DBI IO failed: %s", esp_err_to_name(err));
        return err;
    }

    for (size_t i = 0; i < sizeof(k_init) / sizeof(k_init[0]); ++i) {
        err = esp_lcd_panel_io_tx_param(s_io, k_init[i].cmd, k_init[i].len ? k_init[i].data : NULL, k_init[i].len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "init command 0x%02X failed: %s", k_init[i].cmd, esp_err_to_name(err));
            return err;
        }
        if (k_init[i].delay_ms) {
            vTaskDelay(pdMS_TO_TICKS(k_init[i].delay_ms));
        }
    }

    log_panel_state("after init");

    esp_lcd_dpi_panel_config_t dpi = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = DPI_MHZ,
        .in_color_format = LCD_COLOR_FMT_RGB888,
        .out_color_format = LCD_COLOR_FMT_RGB888,
        .num_fbs = 1,
        .video_timing = {
            .h_size = PANEL_W,
            .v_size = PANEL_H,
            .hsync_back_porch = H_BP,
            .hsync_pulse_width = H_SW,
            .hsync_front_porch = H_FP,
            .vsync_back_porch = V_BP,
            .vsync_pulse_width = V_SW,
            .vsync_front_porch = V_FP,
        },
    };
    err = esp_lcd_new_panel_dpi(s_bus, &dpi, &s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "DPI panel failed: %s", esp_err_to_name(err));
        return err;
    }
    const esp_lcd_dpi_panel_event_callbacks_t cbs = {.on_refresh_done = on_refresh_done};
    if (esp_lcd_dpi_panel_register_event_callbacks(s_panel, &cbs, NULL) != ESP_OK) {
        ESP_LOGW(TAG, "refresh counter unavailable");
    }
    err = esp_lcd_panel_init(s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel init failed: %s", esp_err_to_name(err));
        return err;
    }
    /* Brightness after the video is running: the value sent before sleep-out may not survive it. */
    vTaskDelay(pdMS_TO_TICKS(50));
    lilygo_amoled_set_brightness(0xFE);

    void *fb = NULL;
    err = esp_lcd_dpi_panel_get_frame_buffer(s_panel, 1, &fb);
    if (err != ESP_OK || !fb) {
        ESP_LOGE(TAG, "frame buffer query failed: %s", esp_err_to_name(err));
        return err != ESP_OK ? err : ESP_FAIL;
    }
    memset(out, 0, sizeof(*out));
    out->width = PANEL_W;
    out->height = PANEL_H;
    out->bytes_per_pixel = 3;
    out->stride_bytes = (size_t)PANEL_W * 3;
    out->fb_bytes = out->stride_bytes * PANEL_H;
    out->fb_count = 1;
    out->fb[0] = (uint8_t *)fb;
    /* UI orientation belongs to shared geometry, not a board-only rotate flag. */
    if (!orcsdr_geometry_init(&out->geometry, PANEL_W, PANEL_H, 0, 72, 72, 72, 72)) return ESP_ERR_INVALID_ARG;
    /* Do not issue DCS reads after esp_lcd_panel_init(): IDF 5.5.4's
     * mipi_dsi_hal_host_gen_read_short_packet switches video mode OFF and
     * does not restore it. Refresh callbacks alone do not prove visible video.
     * LakeShark performs its ID read before starting DPI for this reason. */
    /* Bring-up aid: two seconds of solid white shows whether the panel can display anything at all,
     * before the UI takes over. */
    memset(fb, 0xFF, out->fb_bytes);
    esp_cache_msync(fb, out->fb_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    const uint32_t r0 = s_refresh_count;
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_LOGI(TAG, "video scan: %u refresh callbacks in 2 s; no post-start DCS reads",
             (unsigned)(s_refresh_count - r0));
    memset(fb, 0, out->fb_bytes);
    esp_cache_msync(fb, out->fb_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    ESP_LOGI(TAG, "display up: %dx%d native, initial view %dx%d, %u bytes frame buffer in PSRAM",
             out->width, out->height, out->geometry.width, out->geometry.height, (unsigned)out->fb_bytes);
    return ESP_OK;
}

esp_err_t lilygo_amoled_set_brightness(uint8_t level)
{
    if (!s_io) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_lcd_panel_io_tx_param(s_io, 0x51, &level, 1);
}
