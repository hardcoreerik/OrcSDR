/*
 * OrcSDR for the Hosyond 7" 800x480 MIPI-DSI panel on a Waveshare
 * ESP32-P4-Module-DEV-KIT.
 *
 * The bring-up sequence is the one proved by ../orcsdr-p4-dsi-probe; the
 * application layer at the end of this file is what OrcSDR runs on it.
 *
 * Corrected ordering (modular upstream architecture, not the legacy integrated
 * panel driver):
 *   1. ATTINY88 regulator enable with bridge/LCD/touch resets asserted.
 *   2. ESP32-P4 DSI host: one-lane bus, DBI command path latched to low-power
 *      generic writes, DPI panel, non-burst-with-sync-pulses video, low-power
 *      command windows, per-frame ACK off.
 *   3. esp_lcd_panel_init(): this is what actually enables video mode and the
 *      DPI output, so it must run before the bridge is addressed.
 *   4. Clock lane forced to explicit high speed, command ACK off.
 *   5. Staged bridge reset release (PORTC=0x0D) and the ATTINY88 registered
 *      bridge-wake proxy write of TC358762 SYSPMCTRL, then TC358762
 *      initialization over DSI generic long writes.
 *   6. Touch reset release (PORTC=0x0F), then backlight.
 *
 * Logs land on the CH343 UART (COM8).
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_attr.h"
#include "esp_idf_version.h"
#include "esp_heap_caps.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "esp_flash.h"
#include "esp_mac.h"
#include "esp_psram.h"
#include "driver/i2c_master.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "hal/mipi_dsi_hal.h"
#include "hal/mipi_dsi_types.h"
#include "hal/mipi_dsi_ll.h"
#include "hosyond_7in.h"
#include "attiny88_panel.h"
#include "tc358762_bridge.h"

/* Readbacks stop and restart DSI video mode at an arbitrary point in a frame,
 * and every one has returned NO RESPONSE. 0 keeps the stream uninterrupted so the
 * bridge locks on once. */
#ifndef PROBE_READBACK
#define PROBE_READBACK 0
#endif

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(6, 1, 0)
#error "This probe requires ESP-IDF >= 6.1 for esp_lcd_dsi_bus_config_t.flags.clock_lane_force_hs"
#endif

#define HOSYOND_LANE_COUNT 1

static const char *TAG = "ORCSDR_P4";

static esp_ldo_channel_handle_t s_phy_ldo;
static i2c_master_bus_handle_t s_i2c_bus;

/* esp_lcd_dsi_bus_t is opaque, but its first members are bus_id followed by the
 * HAL context (components/esp_lcd/dsi/mipi_dsi_priv.h). Declaring that exact
 * prefix reaches the HAL for LL control and raw generic writes. */
typedef struct {
    int bus_id;
    mipi_dsi_hal_context_t hal;
} dsi_bus_prefix_t;

/* ---------------- I2C primitives ---------------- */

static esp_err_t i2c_dev_add(uint16_t addr, i2c_master_dev_handle_t *dev)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = PROBE_I2C_FREQ_HZ,
    };
    return i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, dev);
}

/* Combined-transaction read: used only for the discovery scan. */
static esp_err_t i2c_read_reg_combined(uint16_t addr, uint8_t reg, uint8_t *val)
{
    i2c_master_dev_handle_t dev = NULL;
    esp_err_t err = i2c_dev_add(addr, &dev);
    if (err != ESP_OK) {
        return err;
    }
    err = i2c_master_transmit_receive(dev, &reg, 1, val, 1, 50);
    i2c_master_bus_rm_device(dev);
    return err;
}

/* Upstream rpi-panel-attiny-regulator.c protocol: a separate address write
 * (retried once, because the first transfer after host power-up often fails),
 * a 5-10 ms gap, then a separate data read. */
static esp_err_t attiny_read(uint8_t reg, uint8_t *val)
{
    i2c_master_dev_handle_t dev = NULL;
    esp_err_t err = i2c_dev_add(ATTINY_I2C_ADDR, &dev);
    if (err != ESP_OK) {
        return err;
    }
    uint8_t addr_buf[1] = {reg};
    err = i2c_master_transmit(dev, addr_buf, 1, 50);
    if (err != ESP_OK) {
        err = i2c_master_transmit(dev, addr_buf, 1, 50);
    }
    if (err != ESP_OK) {
        i2c_master_bus_rm_device(dev);
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(6));
    err = i2c_master_receive(dev, val, 1, 50);
    i2c_master_bus_rm_device(dev);
    return err;
}

static esp_err_t attiny_write_logged(uint8_t reg, uint8_t val, const char *label)
{
    i2c_master_dev_handle_t dev = NULL;
    esp_err_t err = i2c_dev_add(ATTINY_I2C_ADDR, &dev);
    if (err == ESP_OK) {
        uint8_t buf[2] = {reg, val};
        err = i2c_master_transmit(dev, buf, sizeof(buf), 50);
        i2c_master_bus_rm_device(dev);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "  I2C write %s (reg 0x%02X=0x%02X) FAILED: %s",
                 label, reg, val, esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "  I2C write %s (reg 0x%02X=0x%02X) ok", label, reg, val);
    }
    return err;
}

/* ---------------- Discovery ---------------- */

static void identify_device(uint16_t addr)
{
    uint8_t v = 0;
    for (uint8_t reg = 0x00; reg <= 0x03; reg++) {
        if (i2c_read_reg_combined(addr, reg, &v) != ESP_OK) {
            break;
        }
        ESP_LOGI(TAG, "    0x%02X reg 0x%02X = 0x%02X", addr, reg, v);
    }
}

static esp_err_t i2c_bus_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = PROBE_I2C_PORT,
        .sda_io_num = PROBE_I2C_SDA_GPIO,
        .scl_io_num = PROBE_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed: %s", esp_err_to_name(err));
        s_i2c_bus = NULL;
    }
    return err;
}

static void probe_i2c_bus(void)
{
    ESP_LOGI(TAG, "I2C scan: port=%d SDA=GPIO%d SCL=GPIO%d @%d Hz",
             PROBE_I2C_PORT, PROBE_I2C_SDA_GPIO, PROBE_I2C_SCL_GPIO, PROBE_I2C_FREQ_HZ);
    for (uint16_t addr = 0x08; addr <= 0x77; addr++) {
        if (i2c_master_probe(s_i2c_bus, addr, 20) == ESP_OK) {
            ESP_LOGI(TAG, "  I2C device ACK at 0x%02X", addr);
            identify_device(addr);
        }
    }
}

static void dump_attiny_block(void)
{
    for (int base = 0x80; base < 0x92; base += 8) {
        char line[3 * 8 + 1] = {0};
        int pos = 0;
        for (int i = 0; i < 8; i++) {
            uint8_t v = 0;
            if (attiny_read((uint8_t)(base + i), &v) == ESP_OK) {
                pos += snprintf(line + pos, sizeof(line) - pos, "%02X ", v);
            } else {
                pos += snprintf(line + pos, sizeof(line) - pos, "-- ");
            }
        }
        ESP_LOGI(TAG, "  0x45 [%02X] %s", base, line);
    }
}

/* ---------------- ATTINY88 panel control ---------------- */

/* Modular upstream power-up: bridge, LCD and touch resets stay asserted here. */
static void attiny_panel_power_on(void)
{
    uint8_t id = 0;
    if (attiny_read(ATTINY_REG_ID, &id) != ESP_OK) {
        ESP_LOGE(TAG, "panel controller not reachable at 0x%02X", ATTINY_I2C_ADDR);
        return;
    }
    ESP_LOGI(TAG, "panel controller ID = 0x%02X [0xDE=v1, 0xC3=v2]", id);

    attiny_write_logged(ATTINY_REG_POWERON, 0x00, "POWERON=0");
    vTaskDelay(pdMS_TO_TICKS(30));
    attiny_write_logged(ATTINY_REG_PWM, 0x00, "PWM=0");
    attiny_write_logged(ATTINY_REG_PORTC, 0x00, "PORTC=0 (all resets asserted)");
    vTaskDelay(pdMS_TO_TICKS(10));
    attiny_write_logged(ATTINY_REG_PORTA, ATTINY_PA_LCD_LR, "PORTA=LR");
    vTaskDelay(pdMS_TO_TICKS(10));
    attiny_write_logged(ATTINY_REG_PORTB, ATTINY_PB_LCD_MAIN, "PORTB=MAIN");
    vTaskDelay(pdMS_TO_TICKS(10));
    attiny_write_logged(ATTINY_REG_PORTC, ATTINY_PC_LED_EN, "PORTC=LED_EN");
    vTaskDelay(pdMS_TO_TICKS(80));

    dump_attiny_block();
}

/* Releasing RST_BRIDGE_N triggers the ATTINY88 bridge-access proxy writes in
 * upstream rpi-panel-attiny-regulator.c (attiny_gpio_set): the address and data
 * registers of SYSPMCTRL (0x047C) are written in this exact order with 5-8 ms
 * gaps and a 100 ms settle. */
static void attiny_release_bridge_and_wake(void)
{
    attiny_write_logged(ATTINY_REG_PORTC, ATTINY_PC_RELEASE_LCD_BRIDGE,
                        "PORTC=0x0D (LED + LCD reset + bridge reset released)");
    vTaskDelay(pdMS_TO_TICKS(7));
    attiny_write_logged(ATTINY_REG_ADDR_H, 0x04, "proxy ADDR_H=0x04");
    vTaskDelay(pdMS_TO_TICKS(7));
    attiny_write_logged(ATTINY_REG_ADDR_L, 0x7C, "proxy ADDR_L=0x7C");
    vTaskDelay(pdMS_TO_TICKS(7));
    attiny_write_logged(ATTINY_REG_WRITE_DATA_H, 0x00, "proxy DATA_H=0x00");
    vTaskDelay(pdMS_TO_TICKS(7));
    attiny_write_logged(ATTINY_REG_WRITE_DATA_L, 0x00, "proxy DATA_L=0x00");
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "bridge reset released and SYSPMCTRL wake issued");
}

static void attiny_release_touch(void)
{
    attiny_write_logged(ATTINY_REG_PORTC, ATTINY_PC_RELEASE_ALL, "PORTC=0x0F (touch reset released)");
}

static void attiny_backlight_on(void)
{
    attiny_write_logged(ATTINY_REG_PWM, 0xFF, "backlight PWM=0xFF");
}

/* ---------------- Timing profiles ---------------- */

static void log_configured_mode(const h4_profile_t *p)
{
    const uint32_t htotal = HOSYOND_H_RES + p->hfp + p->hsw + p->hbp;
    const uint32_t vtotal = HOSYOND_V_RES + p->vfp + p->vsw + p->vbp;
    ESP_LOGI(TAG, "profile %s: %dx%d RGB888 lanes=%d lane_rate=%.1f Mbps dpi=%.4f MHz",
             p->name, HOSYOND_H_RES, HOSYOND_V_RES, HOSYOND_LANE_COUNT,
             (double)p->lane_mbps, (double)p->dpi_mhz);
    ESP_LOGI(TAG, "  h fp/sw/bp=%d/%d/%d (total %u) v fp/sw/bp=%d/%d/%d (total %u) -> %.2f Hz",
             p->hfp, p->hsw, p->hbp, (unsigned)htotal,
             p->vfp, p->vsw, p->vbp, (unsigned)vtotal,
             (double)(p->dpi_mhz * 1000000.0f / (float)(htotal * vtotal)));
}

static esp_err_t enable_phy_power(void)
{
    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id = PROBE_DSI_PHY_LDO_CHAN,
        .voltage_mv = PROBE_DSI_PHY_LDO_VOLTAGE_MV,
    };
    esp_err_t err = esp_ldo_acquire_channel(&ldo_cfg, &s_phy_ldo);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MIPI DPHY LDO (chan %d @ %d mV) failed: %s",
                 PROBE_DSI_PHY_LDO_CHAN, PROBE_DSI_PHY_LDO_VOLTAGE_MV, esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "MIPI DPHY power on: LDO chan %d @ %d mV",
             PROBE_DSI_PHY_LDO_CHAN, PROBE_DSI_PHY_LDO_VOLTAGE_MV);
    return ESP_OK;
}

/* ---------------- TC358762 over DSI ---------------- */

static void tc358762_write(esp_lcd_dsi_bus_handle_t bus, uint16_t addr, uint32_t val)
{
    dsi_bus_prefix_t *priv = (dsi_bus_prefix_t *)bus;
    uint8_t payload[6] = {
        (uint8_t)(addr & 0xff), (uint8_t)(addr >> 8),
        (uint8_t)(val & 0xff), (uint8_t)((val >> 8) & 0xff),
        (uint8_t)((val >> 16) & 0xff), (uint8_t)((val >> 24) & 0xff),
    };
    ESP_LOGI(TAG, "  dsi generic write %04X = %08X", addr, (unsigned)val);
    mipi_dsi_hal_host_gen_write_long_packet(&priv->hal, 0, MIPI_DSI_DT_GENERIC_LONG_WRITE,
                                            payload, sizeof(payload));
}

/*
 * Verification readback.
 *
 * The upstream Linux driver only ever writes these registers, and a DSI write
 * has no acknowledgement, so nothing so far has confirmed that a packet is
 * actually applied inside the bridge. This performs the standard MIPI
 * generic-read handshake: the 16-bit register address as a Generic Short Write
 * (2 parameters), then a Generic Read Request, with bus turnaround enabled.
 *
 * Every wait is bounded. The stock esp_lcd helper polls the read FIFO without a
 * timeout, which would wedge the probe if the bridge stays silent.
 */
static bool tc358762_read(esp_lcd_dsi_bus_handle_t bus, uint16_t addr, uint32_t *val,
                          bool restore_video)
{
    dsi_bus_prefix_t *priv = (dsi_bus_prefix_t *)bus;
    dsi_host_dev_t *host = priv->hal.host;
    *val = 0;

    /* Readback uses command mode with bus turnaround. */
    mipi_dsi_host_ll_enable_video_mode(host, false);
    mipi_dsi_host_ll_enable_bta(host, true);
    mipi_dsi_host_ll_gen_set_rx_vcid(host, 0);

    /* Drop anything stale so a leftover word cannot be mistaken for a reply. */
    for (int i = 0; i < 64 && !mipi_dsi_host_ll_gen_is_read_fifo_empty(host); i++) {
        (void)mipi_dsi_host_ll_gen_read_payload_fifo(host);
    }

    mipi_dsi_hal_host_gen_write_short_packet(&priv->hal, 0, MIPI_DSI_DT_SET_MAXIMUM_RETURN_PKT, 4);
    mipi_dsi_hal_host_gen_write_short_packet(&priv->hal, 0, MIPI_DSI_DT_GENERIC_SHORT_WRITE_2, addr);
    mipi_dsi_hal_host_gen_write_short_packet(&priv->hal, 0, MIPI_DSI_DT_GENERIC_READ_REQUEST_0, 0x0000);

    bool got = false;
    for (int i = 0; i < 1000; i++) {
        if (!mipi_dsi_host_ll_gen_is_read_cmd_busy(host) &&
            !mipi_dsi_host_ll_gen_is_read_fifo_empty(host)) {
            got = true;
            break;
        }
        esp_rom_delay_us(100);
    }

    if (got) {
        *val = mipi_dsi_host_ll_gen_read_payload_fifo(host);
    }

    mipi_dsi_host_ll_enable_bta(host, false);
    if (restore_video) {
        mipi_dsi_host_ll_enable_video_mode(host, true);
    }
    return got;
}

static void bridge_readback_probe(esp_lcd_dsi_bus_handle_t bus, const char *stage,
                                  bool restore_video)
{
    static const struct {
        uint16_t addr;
        const char *name;
        uint32_t expected;
    } regs[] = {
        { TC358762_SYSCTRL, "SYSCTRL", TC358762_SYSCTRL_VALUE },
        { TC358762_LCDCTRL, "LCDCTRL", TC358762_LCDCTRL_UPSTREAM },
    };

    ESP_LOGI(TAG, "bridge readback (%s):", stage);
    for (size_t i = 0; i < sizeof(regs) / sizeof(regs[0]); i++) {
        uint32_t val = 0;
        if (!tc358762_read(bus, regs[i].addr, &val, restore_video)) {
            ESP_LOGW(TAG, "  %-7s @%04X: NO RESPONSE (no reply within 100 ms)",
                     regs[i].name, regs[i].addr);
            continue;
        }
        ESP_LOGI(TAG, "  %-7s @%04X: read %08X (expected %08X) -> %s",
                 regs[i].name, regs[i].addr, (unsigned)val, (unsigned)regs[i].expected,
                 val == regs[i].expected ? "MATCH" : "MISMATCH");
    }
}

static void tc358762_bridge_init(esp_lcd_dsi_bus_handle_t bus, const h4_profile_t *p)
{
    tc358762_write(bus, TC358762_DSI_LANEENABLE, TC358762_LANEENABLE_L0EN_CLEN);
    tc358762_write(bus, TC358762_PPI_D0S_CLRSIPO, TC358762_CLRSIPO_COUNT);
    tc358762_write(bus, TC358762_PPI_D1S_CLRSIPO, TC358762_CLRSIPO_COUNT);
    tc358762_write(bus, TC358762_PPI_D0S_ATMR, 0);
    tc358762_write(bus, TC358762_PPI_D1S_ATMR, 0);
    tc358762_write(bus, TC358762_PPI_LPTXTIMECNT, TC358762_LPX_PERIOD);
    tc358762_write(bus, TC358762_SPICMR, 0x00);
    tc358762_write(bus, TC358762_LCDCTRL, TC358762_LCDCTRL_UPSTREAM);
    tc358762_write(bus, TC358762_SYSCTRL, TC358762_SYSCTRL_VALUE);

    /* LCDCTRL sets VTGEN (BIT 4), so the bridge generates the LCD raster from
     * its own timing generator. The four timing registers below are that
     * generator's only source of H/V totals; leaving them at reset defaults
     * produces a raster that does not match the 480-line stream arriving over
     * DSI, which is why the frame came out repeated with a seam at mid-height.
     * Layout follows the ESP-IDF port that drives this panel successfully. */
    int hbp = p->hbp + TC358762_DE_START_COMP;
    int hfp = p->hfp - TC358762_DE_START_COMP;
    if (hfp < 0) {
        hfp = 0;
    }
    tc358762_write(bus, TC358762_LCD_HS_HBP, ((uint32_t)hbp << 16) | (uint32_t)p->hsw);
    tc358762_write(bus, TC358762_LCD_HDISP_HFP,
                   ((uint32_t)hfp << 16) | (uint32_t)HOSYOND_H_RES);
    tc358762_write(bus, TC358762_LCD_VS_VBP, ((uint32_t)p->vbp << 16) | (uint32_t)p->vsw);
    tc358762_write(bus, TC358762_LCD_VDISP_VFP,
                   ((uint32_t)p->vfp << 16) | (uint32_t)HOSYOND_V_RES);

    vTaskDelay(pdMS_TO_TICKS(100));
    tc358762_write(bus, TC358762_PPI_STARTPPI, 1);
    tc358762_write(bus, TC358762_DSI_STARTDSI, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
}

/* ---------------- Video ---------------- */

static esp_err_t start_dsi_video(const h4_profile_t *p, esp_lcd_panel_handle_t *out_panel)
{
    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = HOSYOND_LANE_COUNT,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = p->lane_mbps,
        .flags = {
            .clock_lane_force_hs = 1,
        },
    };
    esp_lcd_dsi_bus_handle_t bus = NULL;
    esp_err_t err = esp_lcd_new_dsi_bus(&bus_config, &bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_dsi_bus failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "DSI bus up: %d lane(s) @ %.1f Mbps (host rate, separate from panel timing)",
             HOSYOND_LANE_COUNT, (double)p->lane_mbps);

    esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    esp_lcd_panel_io_handle_t dbi_io = NULL;
    err = esp_lcd_new_panel_io_dbi(bus, &dbi_config, &dbi_io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_io_dbi failed: %s", esp_err_to_name(err));
        return err;
    }
    /* Creating the DBI IO latches low-power generic writes in the DSI
     * controller. The bridge only accepts LP command packets, and the setting
     * persists after the IO is deleted. */
    esp_lcd_panel_io_del(dbi_io);
    ESP_LOGI(TAG, "DSI command path ready: generic writes latched to low-power");

    esp_lcd_dpi_panel_config_t dpi_config = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = p->dpi_mhz,
        .in_color_format = LCD_COLOR_FMT_RGB888,
        .out_color_format = LCD_COLOR_FMT_RGB888,
        .num_fbs = 1,
        .video_timing = {
            .h_size = HOSYOND_H_RES,
            .v_size = HOSYOND_V_RES,
            .hsync_back_porch = p->hbp,
            .hsync_pulse_width = p->hsw,
            .hsync_front_porch = p->hfp,
            .vsync_back_porch = p->vbp,
            .vsync_pulse_width = p->vsw,
            .vsync_front_porch = p->vfp,
        },
        .flags = {
            /* Low-power command windows stay enabled. */
            .disable_lp = 0,
        },
    };

    err = esp_lcd_new_panel_dpi(bus, &dpi_config, out_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_dpi failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "DPI panel created (num_fbs=1, RGB888 in/out)");

    dsi_bus_prefix_t *priv = (dsi_bus_prefix_t *)bus;

    /* ESP-IDF defaults to burst-with-sync-pulses; the bridge needs the
     * continuous non-burst stream. Applied before video is enabled. */
    mipi_dsi_host_ll_dpi_set_video_burst_type(priv->hal.host,
                                              MIPI_DSI_LL_VIDEO_NON_BURST_WITH_SYNC_PULSES);
    ESP_LOGI(TAG, "video mode: non-burst with sync pulses, low-power command windows on");

    /* Per-frame ACK is not needed by this bridge and can stall the stream. */
    mipi_dsi_host_ll_dpi_enable_frame_ack(priv->hal.host, false);
    ESP_LOGI(TAG, "per-frame ACK disabled");

    /* Video has to be running before the bridge is addressed. The TC358762 needs
     * a live HS clock to receive LP generic writes, so a packet sent while the
     * link is idle is dropped without any host-visible error. */
    err = esp_lcd_panel_init(*out_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_panel_init failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "esp_lcd_panel_init OK: DMA started, DSI video mode and DPI output enabled");

    /* Make the high-speed clock lane state explicit rather than relying only on
     * the bus-config flag. */
    mipi_dsi_host_ll_set_clock_lane_state(priv->hal.host, MIPI_DSI_LL_CLOCK_LANE_STATE_HS);
    ESP_LOGI(TAG, "clock lane set to high-speed state");

    /* Generic bridge writes are issued without bus turnaround, so command ACK
     * must not be required. */
    mipi_dsi_host_ll_enable_cmd_ack(priv->hal.host, false);

    /* Reset release and ATTINY proxy wake, now that the link is live. */
    attiny_release_bridge_and_wake();

#if PROBE_READBACK
    bridge_readback_probe(bus, "before TC358762 init", true);
#endif
    tc358762_bridge_init(bus, p);
#if PROBE_READBACK
    bridge_readback_probe(bus, "after TC358762 init", true);
#endif

    attiny_release_touch();
    return ESP_OK;
}

static uint8_t *s_fb;

/* Returns a PSRAM RGB888 buffer sized for the panel, or NULL.
 * No cache maintenance is needed here: esp_lcd_panel_draw_bitmap() copies this
 * into the panel's own DMA frame buffer and syncs that buffer itself. */
static uint8_t *ensure_fb(void)
{
    if (!s_fb) {
        s_fb = heap_caps_malloc((size_t)HOSYOND_H_RES * HOSYOND_V_RES * 3,
                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_fb) {
            ESP_LOGE(TAG, "framebuffer allocation failed");
        }
    }
    return s_fb;
}

static void fill_rect(uint8_t *buf, int x0, int y0, int x1, int y1,
                      uint8_t r, uint8_t g, uint8_t b)
{
    const int w = HOSYOND_H_RES;
    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > w) {
        x1 = w;
    }
    if (y1 > HOSYOND_V_RES) {
        y1 = HOSYOND_V_RES;
    }
    for (int y = y0; y < y1; y++) {
        uint8_t *px = buf + ((size_t)y * w + (size_t)x0) * 3;
        for (int x = x0; x < x1; x++) {
            /* Bytes are emitted as [B][G][R]: confirmed on the panel on 2026-10-25,
             * where the previous [R][B][G] order showed TL blue, TR red, BL green
             * (card colours TL red, TR green, BL blue), i.e. this bridge reads the
             * stream as B,G,R. An earlier bring-up measured a green/blue swap
             * instead, so the bridge byte phase has differed between sessions.
             * This is the one place pixel bytes are produced. */
            *px++ = b;
            *px++ = g;
            *px++ = r;
        }
    }
}

/* =====================================================================
 * OrcSDR application layer
 *
 * The panel bring-up above is the code that was proved on this hardware;
 * everything below is the OrcSDR application that runs on top of it. The
 * screen is composed into a PSRAM framebuffer and pushed to the DPI panel,
 * and the node identity/telemetry that OrcSDR nodes speak is emitted on the
 * console port.
 * ===================================================================== */

#define UI_W HOSYOND_H_RES
#define UI_H HOSYOND_V_RES

#define COL_BG     0x10, 0x12, 0x16
#define COL_PANEL  0x16, 0x1C, 0x1A
#define COL_ACCENT 0x35, 0xD0, 0x7F
#define COL_TEXT   0xE6, 0xF0, 0xEC
#define COL_DIM    0x82, 0x96, 0x90

/* 5x7 glyphs, one row per entry, bit 4 is the leftmost pixel. Only the
 * characters this screen can print are present; anything else renders blank. */
static const uint8_t k_font_alpha[26][7] = {
    {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, /* A */
    {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}, /* B */
    {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}, /* C */
    {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}, /* D */
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}, /* E */
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}, /* F */
    {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}, /* G */
    {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, /* H */
    {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}, /* I */
    {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}, /* J */
    {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, /* K */
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}, /* L */
    {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}, /* M */
    {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}, /* N */
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, /* O */
    {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}, /* P */
    {0x0E, 0x11, 0x11, 0x11, 0x15, 0x13, 0x0D}, /* Q */
    {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}, /* R */
    {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}, /* S */
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, /* T */
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, /* U */
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}, /* V */
    {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11}, /* W */
    {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}, /* X */
    {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}, /* Y */
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}, /* Z */
};

static const uint8_t k_font_digit[10][7] = {
    {0x0E, 0x13, 0x15, 0x15, 0x15, 0x19, 0x0E}, /* 0 */
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}, /* 1 */
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, /* 2 */
    {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}, /* 3 */
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, /* 4 */
    {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}, /* 5 */
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, /* 6 */
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, /* 7 */
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, /* 8 */
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}, /* 9 */
};

static const uint8_t k_glyph_blank[7] = {0, 0, 0, 0, 0, 0, 0};
static const uint8_t k_glyph_dot[7] = {0, 0, 0, 0, 0, 0x0C, 0x0C};
static const uint8_t k_glyph_colon[7] = {0, 0x0C, 0x0C, 0, 0x0C, 0x0C, 0};
static const uint8_t k_glyph_dash[7] = {0, 0, 0, 0x1F, 0, 0, 0};
static const uint8_t k_glyph_slash[7] = {0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10};
static const uint8_t k_glyph_plus[7] = {0, 0x04, 0x04, 0x1F, 0x04, 0x04, 0};
static const uint8_t k_glyph_percent[7] = {0x11, 0x02, 0x04, 0x04, 0x08, 0x11, 0};
static const uint8_t k_glyph_bang[7] = {0x04, 0x04, 0x04, 0x04, 0x04, 0, 0x04};
static const uint8_t k_glyph_lt[7] = {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02};
static const uint8_t k_glyph_gt[7] = {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08};
static const uint8_t k_glyph_eq[7] = {0, 0, 0x1F, 0, 0x1F, 0, 0};
static const uint8_t k_glyph_lparen[7] = {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02};
static const uint8_t k_glyph_rparen[7] = {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08};
static const uint8_t k_glyph_star[7] = {0, 0x0A, 0x04, 0x1F, 0x04, 0x0A, 0};

static const uint8_t *glyph_for(char c)
{
    if (c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }
    if (c >= 'A' && c <= 'Z') {
        return k_font_alpha[c - 'A'];
    }
    if (c >= '0' && c <= '9') {
        return k_font_digit[c - '0'];
    }
    switch (c) {
    case '.': return k_glyph_dot;
    case ':': return k_glyph_colon;
    case '-': return k_glyph_dash;
    case '/': return k_glyph_slash;
    case '+': return k_glyph_plus;
    case '%': return k_glyph_percent;
    case '!': return k_glyph_bang;
    case '<': return k_glyph_lt;
    case '>': return k_glyph_gt;
    case '=': return k_glyph_eq;
    case '(': return k_glyph_lparen;
    case ')': return k_glyph_rparen;
    case '*': return k_glyph_star;
    default: return k_glyph_blank;
    }
}

#define GLYPH_PX 5
#define GLYPH_PY 7
#define TEXT_ADV(scale) ((GLYPH_PX + 1) * (scale))
#define TEXT_HEIGHT(scale) (GLYPH_PY * (scale))

static void draw_text(uint8_t *buf, int x, int y, int scale, const char *text,
                      uint8_t r, uint8_t g, uint8_t b)
{
    int pen = x;
    for (const char *s = text; *s; s++) {
        const uint8_t *rows = glyph_for(*s);
        for (int row = 0; row < GLYPH_PY; row++) {
            const uint8_t bits = rows[row];
            for (int col = 0; col < GLYPH_PX; col++) {
                if ((bits >> (GLYPH_PX - 1 - col)) & 1u) {
                    fill_rect(buf, pen + col * scale, y + row * scale,
                              pen + (col + 1) * scale, y + (row + 1) * scale,
                              r, g, b);
                }
            }
        }
        pen += TEXT_ADV(scale);
    }
}

/* OrcSDR badge, converted from the tab5 asset. Source pixels are RGB565 from
 * M5GFX, so they are unpacked to 8 bit and handed to fill_rect, which applies
 * the bridge's byte order in one place. */
extern const uint8_t badge_rgb565_start[] asm("_binary_orc_badge_104_rgb565_start");

#define BADGE_W 104
#define BADGE_H 104

static void draw_badge(uint8_t *buf, int x0, int y0, int scale)
{
    for (int y = 0; y < BADGE_H; y++) {
        const uint8_t *src = badge_rgb565_start + (size_t)y * BADGE_W * 2;
        for (int x = 0; x < BADGE_W; x++) {
            const uint16_t v = (uint16_t)((src[x * 2] << 8) | src[x * 2 + 1]);
            const uint8_t r = (uint8_t)(((v >> 11) & 0x1F) * 255 / 31);
            const uint8_t g = (uint8_t)(((v >> 5) & 0x3F) * 255 / 63);
            const uint8_t b = (uint8_t)((v & 0x1F) * 255 / 31);
            fill_rect(buf, x0 + x * scale, y0 + y * scale,
                      x0 + (x + 1) * scale, y0 + (y + 1) * scale, r, g, b);
        }
    }
}

typedef struct {
    const char *node_id;
    uint64_t frames;
    uint32_t uptime_s;
    uint32_t heap_kb;
    uint32_t refresh_hz;
    int bar_permille;
} ui_state_t;

static void draw_app_screen(esp_lcd_panel_handle_t panel, const ui_state_t *st)
{
    uint8_t *buf = ensure_fb();
    if (!buf) {
        return;
    }

    fill_rect(buf, 0, 0, UI_W, UI_H, COL_BG);

    draw_badge(buf, 28, 26, 1);

    draw_text(buf, 152, 34, 5, "ORCSDR", COL_TEXT);
    fill_rect(buf, 152, 82, 470, 86, COL_ACCENT);
    draw_text(buf, 152, 98, 2, "SOFTWARE DEFINED RADIO", COL_ACCENT);
    draw_text(buf, 152, 124, 2, "ESP32-P4  MIPI-DSI 800X480", COL_DIM);

    fill_rect(buf, 24, 164, UI_W - 24, 392, COL_PANEL);
    fill_rect(buf, 24, 164, UI_W - 24, 166, COL_DIM);
    fill_rect(buf, 24, 390, UI_W - 24, 392, COL_DIM);

    char line[64];
    int y = 190;
    const int step = 36;

    snprintf(line, sizeof(line), "NODE    %s", st->node_id);
    draw_text(buf, 48, y, 3, line, COL_TEXT);
    y += step;

    snprintf(line, sizeof(line), "UPTIME  %02u:%02u:%02u",
             (unsigned)(st->uptime_s / 3600),
             (unsigned)((st->uptime_s / 60) % 60),
             (unsigned)(st->uptime_s % 60));
    draw_text(buf, 48, y, 3, line, COL_TEXT);
    y += step;

    snprintf(line, sizeof(line), "HEAP    %u KB FREE", (unsigned)st->heap_kb);
    draw_text(buf, 48, y, 3, line, COL_TEXT);
    y += step;

    snprintf(line, sizeof(line), "FRAME   %u  %u HZ",
             (unsigned)st->frames, (unsigned)st->refresh_hz);
    draw_text(buf, 48, y, 3, line, COL_TEXT);
    y += step;

    draw_text(buf, 48, y, 3, "DISPLAY  HOSYOND 7IN TC358762", COL_DIM);

    /* Activity bar: moves with every refresh, so a frozen screen is obvious
     * without needing anything else on the panel. */
    fill_rect(buf, 48, 368, UI_W - 48, 380, 0x0A, 0x0E, 0x0C);
    {
        const int span = UI_W - 48 - 48;
        const int lit = span * st->bar_permille / 1000;
        fill_rect(buf, 48, 368, 48 + lit, 380, COL_ACCENT);
    }

    draw_text(buf, 28, 412, 2, "ORCSDR APPLICATION RUNNING", COL_ACCENT);
    draw_text(buf, 28, 436, 2, "PANEL BRING-UP COMPLETE - 800X480 RGB888", COL_DIM);

    fill_rect(buf, 0, 0, UI_W, 1, COL_ACCENT);
    fill_rect(buf, 0, UI_H - 1, UI_W, UI_H, COL_ACCENT);
    fill_rect(buf, 0, 0, 1, UI_H, COL_ACCENT);
    fill_rect(buf, UI_W - 1, 0, UI_W, UI_H, COL_ACCENT);

    esp_err_t err = esp_lcd_panel_draw_bitmap(panel, 0, 0, UI_W, UI_H, buf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "draw_bitmap failed: %s", esp_err_to_name(err));
    }
}

/* Confirm that what was composed really is at the coordinates it claims, so a
 * later displacement can only be scan-out. */
static void fb_spot_check(void)
{
    const uint8_t *buf = s_fb;
    if (!buf) {
        ESP_LOGE(TAG, "spot check skipped: no framebuffer");
        return;
    }
    const int w = UI_W;
    const int h = UI_H;
    static const struct {
        int x, y;
        const char *what;
    } spots[] = {
        {0, 0, "perimeter TL"},
        {w - 1, 0, "perimeter TR"},
        {0, h - 1, "perimeter BL"},
        {w - 1, h - 1, "perimeter BR"},
        {400, 240, "panel centre"},
    };
    bool ok = true;
    for (size_t i = 0; i < sizeof(spots) / sizeof(spots[0]); i++) {
        const uint8_t *px = buf + ((size_t)spots[i].y * w + (size_t)spots[i].x) * 3;
        const bool lit = (px[0] | px[1] | px[2]) != 0;
        if (!lit) {
            ok = false;
        }
        ESP_LOGI(TAG, "spot %-13s (%3d,%3d) = %02x%02x%02x %s", spots[i].what,
                 spots[i].x, spots[i].y, px[0], px[1], px[2],
                 lit ? "ok" : "MISSING");
    }
    ESP_LOGI(TAG, "display content check: %s", ok ? "PASS" : "FAIL");
}

/* OrcSDR node protocol, same shape the tab5 firmware speaks so a host sees
 * this board as an ordinary OrcSDR node. */
static void emit_node_hello(const char *node_id, uint32_t flash_bytes,
                            size_t psram_bytes)
{
    printf("{\"type\":\"hello\",\"message_id\":\"p4hosyond_hello_01\","
           "\"protocol_version\":{\"major\":1,\"minor\":0},"
           "\"payload\":{\"adapter\":{\"id\":\"p4hosyond.serial\",\"version\":\"0.1.0\"},"
           "\"features\":[\"telemetry\"],\"capability_namespaces\":[\"p4hosyond\"],"
           "\"config_schema_version\":1}}\n");
    printf("{\"type\":\"node_snapshot\",\"message_id\":\"p4hosyond_snapshot_01\","
           "\"protocol_version\":{\"major\":1,\"minor\":0},"
           "\"payload\":{\"node_id\":\"%s\",\"connection_state\":\"online\","
           "\"hardware\":{\"chip\":\"ESP32-P4\",\"flash_bytes\":%u,"
           "\"psram_bytes\":%u,\"display\":\"hosyond-7in-800x480-dsi\"},"
           "\"capabilities\":[\"p4hosyond.device.info\",\"p4hosyond.display.status\"]}}\n",
           node_id, (unsigned)flash_bytes, (unsigned)psram_bytes);
}

static void emit_node_heartbeat(const char *node_id, uint64_t sequence,
                               uint32_t uptime_ms, uint32_t heap_kb)
{
    printf("{\"type\":\"heartbeat\",\"message_id\":\"p4hosyond_heartbeat_%u\","
           "\"protocol_version\":{\"major\":1,\"minor\":0},"
           "\"payload\":{\"node_id\":\"%s\",\"sequence\":%u,\"uptime_ms\":%u,"
           "\"free_heap_bytes\":%u}}\n",
           (unsigned)sequence, node_id, (unsigned)sequence, (unsigned)uptime_ms,
           (unsigned)heap_kb * 1024);
}

/* Unmistakable presence test before any application pixels: four full-screen
 * solid colours. If none of these appear the panel is not being scanned out at
 * all, which is a different fault from an application that draws poorly. */
static void boot_screen_test(esp_lcd_panel_handle_t panel)
{
    uint8_t *buf = ensure_fb();
    if (!buf) {
        return;
    }
    static const struct {
        uint8_t r, g, b;
        const char *name;
    } steps[] = {
        {0xFF, 0xFF, 0xFF, "white"},
        {0xFF, 0x00, 0x00, "red"},
        {0x00, 0xFF, 0x00, "green"},
        {0x00, 0x00, 0xFF, "blue"},
    };
    for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
        fill_rect(buf, 0, 0, UI_W, UI_H, steps[i].r, steps[i].g, steps[i].b);
        esp_err_t err = esp_lcd_panel_draw_bitmap(panel, 0, 0, UI_W, UI_H, buf);
        ESP_LOGW(TAG, "boot screen test: %s -> %s", steps[i].name,
                 esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(1500));
    }
}

/* The bridge cannot be read back, so the one thing it will not survive quietly
 * is a raster whose line total differs from the host stream: the DE window then
 * walks through the payload and the panel goes black. Check the arithmetic the
 * two register sets are derived from and say so at startup. */
static void check_line_totals(const h4_profile_t *p)
{
    const int host_hbp = p->hbp;
    const int host_hfp = p->hfp;
    int bridge_hbp = p->hbp + TC358762_DE_START_COMP;
    int bridge_hfp = p->hfp - TC358762_DE_START_COMP;
    if (bridge_hfp < 0) {
        bridge_hfp = 0;
    }
    const int host_total = HOSYOND_H_RES + p->hsw + host_hbp + host_hfp;
    const int bridge_total = HOSYOND_H_RES + p->hsw + bridge_hbp + bridge_hfp;
    ESP_LOGI(TAG, "line totals: host %d (hbp %d + hfp %d), bridge %d (hbp %d + hfp %d)",
             host_total, host_hbp, host_hfp, bridge_total, bridge_hbp, bridge_hfp);
    if (host_total != bridge_total) {
        ESP_LOGE(TAG, "MISMATCH: bridge raster is %d px against a %d px host line; "
                      "the panel will show nothing. Increase the profile HFP to at "
                      "least TC358762_DE_START_COMP (%d).",
                 bridge_total, host_total, TC358762_DE_START_COMP);
    }
}

void app_main(void)
{
    const h4_profile_t *p = &h4_profiles[H4_DEFAULT_PROFILE];

    setvbuf(stdout, NULL, _IONBF, 0);

    ESP_LOGW(TAG, "=== OrcSDR on Waveshare ESP32-P4 module dev kit + Hosyond 7\" 800x480 DSI ===");
    ESP_LOGI(TAG, "build %s %s | IDF %s | target %s | PSRAM free %u KB",
             __DATE__, __TIME__, esp_get_idf_version(), CONFIG_IDF_TARGET,
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));

    for (int i = 0; i < H4_PROFILE_COUNT; i++) {
        log_configured_mode(&h4_profiles[i]);
    }
    ESP_LOGI(TAG, "active profile: %s", p->name);
    check_line_totals(p);

    esp_err_t err = i2c_bus_init();
    if (err != ESP_OK) {
        goto idle;
    }
    probe_i2c_bus();
    attiny_panel_power_on();

    err = enable_phy_power();
    if (err != ESP_OK) {
        goto idle;
    }

    esp_lcd_panel_handle_t panel = NULL;
    err = start_dsi_video(p, &panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "DSI video start FAILED - panel is not being driven");
        goto idle;
    }

    attiny_backlight_on();

    boot_screen_test(panel);

    char node_id[40];
    uint8_t mac[6];
    uint32_t flash_bytes = 0;
    esp_read_mac(mac, ESP_MAC_BASE);
    esp_flash_get_size(NULL, &flash_bytes);
    snprintf(node_id, sizeof(node_id), "p4hosyond_%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    emit_node_hello(node_id, flash_bytes, esp_psram_get_size());

    /* Runtime cadence: the screen is refreshed at 4 Hz so uptime and the
     * activity bar move, and a heartbeat goes out every 2 s. */
    const int refresh_ms = 250;
    const int refresh_hz = 1000 / refresh_ms;
    const TickType_t period = pdMS_TO_TICKS(refresh_ms);

    ui_state_t st = {
        .node_id = node_id,
        .refresh_hz = (uint32_t)refresh_hz,
    };

    ESP_LOGW(TAG, "OrcSDR application starting: node %s", node_id);

    TickType_t last_wake = xTaskGetTickCount();
    uint64_t heartbeats = 0;
    const uint32_t frames_per_heartbeat = (uint32_t)refresh_hz * 2;
    for (;;) {
        st.frames++;
        st.uptime_s = (uint32_t)(esp_timer_get_time() / 1000000);
        st.heap_kb = (uint32_t)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
        st.bar_permille = (int)((st.frames * 37) % 1000);

        draw_app_screen(panel, &st);

        if (st.frames == 1) {
            fb_spot_check();
        }
        if ((st.frames % frames_per_heartbeat) == 1) {
            heartbeats++;
            emit_node_heartbeat(node_id, heartbeats, st.uptime_s * 1000, st.heap_kb);
        }

        vTaskDelayUntil(&last_wake, period);
    }

idle:
    ESP_LOGE(TAG, "OrcSDR stopped; staying idle so the failure log is not overwritten");
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
