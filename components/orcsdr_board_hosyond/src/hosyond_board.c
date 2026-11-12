/*
 * Waveshare ESP32-P4-Module-DEV-KIT + Hosyond 7" 800x480 DSI panel.
 *
 * The start-up order below is the one verified on the bench (docs/HOSYOND_7IN_DSI_P4.md,
 * section 4); do not reorder it:
 *   ATTINY power-up -> DPHY LDO -> DSI bus -> low-power generic writes latched ->
 *   DPI panel -> non-burst video -> panel init (video runs) -> clock lane HS ->
 *   bridge reset release + wake proxy -> TC358762 registers -> touch reset -> backlight.
 *
 * The TC358762 is written only while video is running and is never read back: reads
 * return nothing and the read path restarts DSI video mode mid-frame, which made the
 * picture position and colour vary from boot to boot.
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_idf_version.h"
#include "esp_cache.h"
#include "esp_rom_sys.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "hal/mipi_dsi_hal.h"
#include "hal/mipi_dsi_types.h"
#include "hal/mipi_dsi_ll.h"
#include "hosyond_board.h"
#include "orcsdr_board_profile.h"
#include "orcsdr_display_profile.h"
#include "attiny88_panel.h"
#include "tc358762_bridge.h"

static const char *TAG = "HOSYOND_BOARD";

/* ---- Board facts ---- */
/* The I2C pins come from the board profile (orcsdr_board_profile.h). */
#define I2C_FREQ_HZ 100000
#define DSI_PHY_LDO_CHAN 3 /* LDO_VO3 -> VDD_MIPI_DPHY */
#define DSI_PHY_LDO_MV 2500

#define H_RES 800
#define V_RES 480
/* Profile "rpi-observed-stable": 1102 x 526 at 25.98 MHz = 44.8 Hz, 1 lane at 600 Mbps. */
#define H_FP 254
#define H_SW 2
#define H_BP 46
#define V_FP 22
#define V_SW 20
#define V_BP 4
#define DPI_MHZ 25.98f
#define LANE_MBPS 600.0f
#define LANE_COUNT 1

#ifndef HOSYOND_NUM_FBS
#define HOSYOND_NUM_FBS 2
#endif

static i2c_master_bus_handle_t s_i2c;
static esp_ldo_channel_handle_t s_ldo;
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_dsi_bus_handle_t s_bus;

/* esp_lcd_dsi_bus_t is opaque, but its first members are bus_id followed by the HAL
 * context (components/esp_lcd/dsi/mipi_dsi_priv.h). Declaring that exact prefix
 * reaches the HAL for LL control and raw generic writes. */
typedef struct {
    int bus_id;
    mipi_dsi_hal_context_t hal;
} dsi_bus_prefix_t;

/* ---------------- ATTINY88 (I2C 0x45) ---------------- */

static esp_err_t attiny_dev(i2c_master_dev_handle_t *dev)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ATTINY_I2C_ADDR,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    return i2c_master_bus_add_device(s_i2c, &cfg, dev);
}

/* Upstream protocol: a separate address write (retried once, the first transfer
 * after power-up often fails), a ~6 ms gap, then a separate data read. */
static esp_err_t attiny_read(uint8_t reg, uint8_t *val)
{
    i2c_master_dev_handle_t dev;
    esp_err_t err = attiny_dev(&dev);
    if (err != ESP_OK) {
        return err;
    }
    err = i2c_master_transmit(dev, &reg, 1, 50);
    if (err != ESP_OK) {
        err = i2c_master_transmit(dev, &reg, 1, 50);
    }
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(6));
        err = i2c_master_receive(dev, val, 1, 50);
    }
    i2c_master_bus_rm_device(dev);
    return err;
}

static esp_err_t attiny_write(uint8_t reg, uint8_t val)
{
    i2c_master_dev_handle_t dev;
    esp_err_t err = attiny_dev(&dev);
    if (err != ESP_OK) {
        return err;
    }
    const uint8_t buf[2] = {reg, val};
    err = i2c_master_transmit(dev, buf, sizeof(buf), 50);
    i2c_master_bus_rm_device(dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ATTINY write reg 0x%02X=0x%02X failed: %s", reg, val, esp_err_to_name(err));
    }
    return err;
}

static esp_err_t attiny_power_on(void)
{
    uint8_t id = 0;
    esp_err_t err = attiny_read(ATTINY_REG_ID, &id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel controller not reachable at 0x%02X: %s", ATTINY_I2C_ADDR, esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "panel controller ID = 0x%02X (0xC3 = v2)", id);
    if (id != ATTINY_VERSION_V2 && id != ATTINY_VERSION_V1) {
        ESP_LOGW(TAG, "unexpected ATTINY ID 0x%02X", id);
    }
    /* Kernel sequence: POWERON=0, 30 ms, PWM=0; PORTC=0, PORTA=LR, PORTB=MAIN, PORTC=LED_EN. */
    err = attiny_write(ATTINY_REG_POWERON, 0x00);
    vTaskDelay(pdMS_TO_TICKS(30));
    err |= attiny_write(ATTINY_REG_PWM, 0x00);
    err |= attiny_write(ATTINY_REG_PORTC, 0x00); /* all resets asserted */
    vTaskDelay(pdMS_TO_TICKS(10));
    err |= attiny_write(ATTINY_REG_PORTA, ATTINY_PA_LCD_LR);
    vTaskDelay(pdMS_TO_TICKS(10));
    err |= attiny_write(ATTINY_REG_PORTB, ATTINY_PB_LCD_MAIN);
    vTaskDelay(pdMS_TO_TICKS(10));
    err |= attiny_write(ATTINY_REG_PORTC, ATTINY_PC_LED_EN);
    vTaskDelay(pdMS_TO_TICKS(80));
    return err == ESP_OK ? ESP_OK : ESP_FAIL;
}

/* Releasing RST_BRIDGE_N triggers the ATTINY88 bridge-access proxy writes of
 * SYSPMCTRL (0x047C), in this order with 5-8 ms gaps, then a 100 ms settle. */
static esp_err_t attiny_release_bridge_and_wake(void)
{
    esp_err_t err = attiny_write(ATTINY_REG_PORTC, ATTINY_PC_RELEASE_LCD_BRIDGE);
    vTaskDelay(pdMS_TO_TICKS(7));
    err |= attiny_write(ATTINY_REG_ADDR_H, 0x04);
    vTaskDelay(pdMS_TO_TICKS(7));
    err |= attiny_write(ATTINY_REG_ADDR_L, 0x7C);
    vTaskDelay(pdMS_TO_TICKS(7));
    err |= attiny_write(ATTINY_REG_WRITE_DATA_H, 0x00);
    vTaskDelay(pdMS_TO_TICKS(7));
    err |= attiny_write(ATTINY_REG_WRITE_DATA_L, 0x00);
    vTaskDelay(pdMS_TO_TICKS(100));
    return err == ESP_OK ? ESP_OK : ESP_FAIL;
}

/* ---------------- TC358762 over DSI generic long writes ---------------- */

static void tc358762_write(uint16_t addr, uint32_t val)
{
    dsi_bus_prefix_t *priv = (dsi_bus_prefix_t *)s_bus;
    uint8_t payload[6] = {
        (uint8_t)(addr & 0xff), (uint8_t)(addr >> 8),
        (uint8_t)(val & 0xff), (uint8_t)((val >> 8) & 0xff),
        (uint8_t)((val >> 16) & 0xff), (uint8_t)((val >> 24) & 0xff),
    };
    mipi_dsi_hal_host_gen_write_long_packet(&priv->hal, 0, MIPI_DSI_DT_GENERIC_LONG_WRITE,
                                            payload, sizeof(payload));
}

static void tc358762_init(void)
{
    /* Bridge DE-start compensation is traded out of the front porch so the bridge
     * line total equals the host's (1102); if they differ the panel goes black. */
    const int bridge_hbp = H_BP + TC358762_DE_START_COMP;
    const int bridge_hfp = H_FP - TC358762_DE_START_COMP;
    _Static_assert(H_FP >= TC358762_DE_START_COMP, "front porch must cover the DE compensation");
    ESP_LOGI(TAG, "line totals: host %d, bridge %d", H_RES + H_SW + H_BP + H_FP,
             H_RES + H_SW + bridge_hbp + bridge_hfp);

    tc358762_write(TC358762_DSI_LANEENABLE, TC358762_LANEENABLE_L0EN_CLEN);
    tc358762_write(TC358762_PPI_D0S_CLRSIPO, TC358762_CLRSIPO_COUNT);
    tc358762_write(TC358762_PPI_D1S_CLRSIPO, TC358762_CLRSIPO_COUNT);
    tc358762_write(TC358762_PPI_D0S_ATMR, 0);
    tc358762_write(TC358762_PPI_D1S_ATMR, 0);
    tc358762_write(TC358762_PPI_LPTXTIMECNT, TC358762_LPX_PERIOD);
    tc358762_write(TC358762_SPICMR, 0x00);
    tc358762_write(TC358762_LCDCTRL, TC358762_LCDCTRL_UPSTREAM);
    tc358762_write(TC358762_SYSCTRL, TC358762_SYSCTRL_VALUE);
    tc358762_write(TC358762_LCD_HS_HBP, ((uint32_t)bridge_hbp << 16) | (uint32_t)H_SW);
    tc358762_write(TC358762_LCD_HDISP_HFP, ((uint32_t)bridge_hfp << 16) | (uint32_t)H_RES);
    tc358762_write(TC358762_LCD_VS_VBP, ((uint32_t)V_BP << 16) | (uint32_t)V_SW);
    tc358762_write(TC358762_LCD_VDISP_VFP, ((uint32_t)V_FP << 16) | (uint32_t)V_RES);
    vTaskDelay(pdMS_TO_TICKS(100));
    tc358762_write(TC358762_PPI_STARTPPI, 1);
    tc358762_write(TC358762_DSI_STARTDSI, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
}

/* ---------------- Public interface ---------------- */

esp_err_t hosyond_board_start(hosyond_display_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    esp_err_t err;

    orcsdr_board_profile_log();
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = orcsdr_board_profile()->i2c_port,
        .sda_io_num = orcsdr_board_profile()->i2c_sda,
        .scl_io_num = orcsdr_board_profile()->i2c_scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    err = i2c_new_master_bus(&bus_cfg, &s_i2c);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed: %s", esp_err_to_name(err));
        return err;
    }
    {
        unsigned char found[16] = {0};
        const orcsdr_display_profile_t *panel = orcsdr_display_detect(s_i2c, found);
        if (!panel) {
            orcsdr_display_report(found);
            return ESP_ERR_NOT_FOUND;
        }
    }
    err = attiny_power_on();
    if (err != ESP_OK) {
        return err;
    }

    esp_ldo_channel_config_t ldo_cfg = {.chan_id = DSI_PHY_LDO_CHAN, .voltage_mv = DSI_PHY_LDO_MV};
    err = esp_ldo_acquire_channel(&ldo_cfg, &s_ldo);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MIPI DPHY LDO failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_lcd_dsi_bus_config_t bus = {
        .bus_id = 0,
        .num_data_lanes = LANE_COUNT,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = LANE_MBPS,
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 1, 0)
        .flags = {.clock_lane_force_hs = 1},
#endif
    };
    err = esp_lcd_new_dsi_bus(&bus, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "DSI bus failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Creating the DBI IO latches low-power generic writes in the DSI controller. The
     * bridge only accepts LP command packets and the setting persists after deletion. */
    esp_lcd_dbi_io_config_t dbi = {.virtual_channel = 0, .lcd_cmd_bits = 8, .lcd_param_bits = 8};
    esp_lcd_panel_io_handle_t dbi_io = NULL;
    err = esp_lcd_new_panel_io_dbi(s_bus, &dbi, &dbi_io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "DBI IO failed: %s", esp_err_to_name(err));
        return err;
    }
    esp_lcd_panel_io_del(dbi_io);

    esp_lcd_dpi_panel_config_t dpi = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = DPI_MHZ,
        .in_color_format = LCD_COLOR_FMT_RGB888,
        .out_color_format = LCD_COLOR_FMT_RGB888,
        .num_fbs = HOSYOND_NUM_FBS,
        .video_timing = {
            .h_size = H_RES,
            .v_size = V_RES,
            .hsync_back_porch = H_BP,
            .hsync_pulse_width = H_SW,
            .hsync_front_porch = H_FP,
            .vsync_back_porch = V_BP,
            .vsync_pulse_width = V_SW,
            .vsync_front_porch = V_FP,
        },
        .flags = {.disable_lp = 0},
    };
    err = esp_lcd_new_panel_dpi(s_bus, &dpi, &s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "DPI panel failed: %s", esp_err_to_name(err));
        return err;
    }

    dsi_bus_prefix_t *priv = (dsi_bus_prefix_t *)s_bus;
    /* ESP-IDF defaults to burst-with-sync-pulses; the bridge needs non-burst. */
    mipi_dsi_host_ll_dpi_set_video_burst_type(priv->hal.host, MIPI_DSI_LL_VIDEO_NON_BURST_WITH_SYNC_PULSES);
    mipi_dsi_host_ll_dpi_enable_frame_ack(priv->hal.host, false);

    /* Video must run before the bridge is addressed. */
    err = esp_lcd_panel_init(s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel init failed: %s", esp_err_to_name(err));
        return err;
    }
    mipi_dsi_host_ll_set_clock_lane_state(priv->hal.host, MIPI_DSI_LL_CLOCK_LANE_STATE_HS);
    mipi_dsi_host_ll_enable_cmd_ack(priv->hal.host, false);

    err = attiny_release_bridge_and_wake();
    if (err != ESP_OK) {
        return err;
    }
    tc358762_init();

    /* Touch reset release, then backlight. */
    err = attiny_write(ATTINY_REG_PORTC, ATTINY_PC_RELEASE_ALL);
    err |= attiny_write(ATTINY_REG_PWM, 0xFF);
    if (err != ESP_OK) {
        return ESP_FAIL;
    }

    void *fbs[HOSYOND_BOARD_MAX_FBS] = {0};
    err = esp_lcd_dpi_panel_get_frame_buffer(s_panel, HOSYOND_NUM_FBS, &fbs[0], &fbs[1]);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "frame buffer query failed: %s", esp_err_to_name(err));
        return err;
    }
    out->width = H_RES;
    out->height = V_RES;
    out->bytes_per_pixel = 3;
    out->stride_bytes = (size_t)H_RES * 3;
    out->fb_bytes = out->stride_bytes * V_RES;
    out->fb_count = HOSYOND_NUM_FBS;
    for (int i = 0; i < HOSYOND_NUM_FBS; i++) {
        out->fb[i] = (uint8_t *)fbs[i];
    }
    ESP_LOGI(TAG, "display up: %dx%d, %d frame buffer(s) of %u bytes in PSRAM, byte order [B][G][R]",
             out->width, out->height, out->fb_count, (unsigned)out->fb_bytes);
    return ESP_OK;
}

esp_err_t hosyond_board_sync(const hosyond_display_t *d, int fb_index, int y0, int y1)
{
    if (!d || fb_index < 0 || fb_index >= d->fb_count) {
        return ESP_ERR_INVALID_ARG;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (y1 > d->height) {
        y1 = d->height;
    }
    if (y1 <= y0) {
        return ESP_OK;
    }
    return esp_cache_msync(d->fb[fb_index] + (size_t)y0 * d->stride_bytes, (size_t)(y1 - y0) * d->stride_bytes,
                           ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
}

esp_err_t hosyond_board_flip(const hosyond_display_t *d, int fb_index)
{
    if (!d || !s_panel || fb_index < 0 || fb_index >= d->fb_count) {
        return ESP_ERR_INVALID_ARG;
    }
    /* A draw buffer inside a panel frame buffer is not copied: the IDF only writes
     * back the named rows (one row here, the rest was synced by the caller) and makes
     * that frame buffer the scan-out buffer. */
    return esp_lcd_panel_draw_bitmap(s_panel, 0, 0, d->width, 1, d->fb[fb_index]);
}

esp_err_t hosyond_board_set_backlight(uint8_t level)
{
    return s_i2c ? attiny_write(ATTINY_REG_PWM, level) : ESP_ERR_INVALID_STATE;
}

i2c_master_bus_handle_t hosyond_board_i2c(void)
{
    return s_i2c;
}

esp_err_t hosyond_board_touch_start(void)
{
    if (!s_i2c) {
        return ESP_ERR_INVALID_STATE;
    }
    return ft5x06_start(s_i2c, TAG);
}
