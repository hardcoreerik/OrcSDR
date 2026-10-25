/*
 * Isolated DSI bring-up probe: Hosyond 7" 800x480 panel on a
 * Waveshare ESP32-P4-Module-DEV-KIT.
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
#include "esp_timer.h"
#include "esp_rom_sys.h"
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
#include "ft5x06_touch.h"

/* ESP-IDF >= 6.1 has esp_lcd_dsi_bus_config_t.flags.clock_lane_force_hs. Older IDF
 * (5.5.x, which the OrcSDR app is pinned to) lacks it; the clock lane is then forced
 * to HS only by the explicit mipi_dsi_host_ll_set_clock_lane_state() call after
 * esp_lcd_panel_init(). */
#define PROBE_HAS_FORCE_HS (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 1, 0))

#define HOSYOND_LANE_COUNT 1

/* Readbacks stop and restart DSI video mode at an arbitrary point in a frame,
 * and every one has returned NO RESPONSE. 0 keeps the stream uninterrupted so the
 * bridge locks on once. */
#ifndef PROBE_READBACK
#define PROBE_READBACK 0
#endif

/* 1 = run the on-screen touch self-test (tap the boxes as they appear) at boot. */
#ifndef PROBE_TOUCH_TEST
#define PROBE_TOUCH_TEST 1
#endif

/* 1 = cycle bridge register states at runtime (Experiment 1); 0 = steady card. */
#ifndef PROBE_EXP1
#define PROBE_EXP1 0
#endif

static const char *TAG = "HOSYOND_PROBE";

static esp_ldo_channel_handle_t s_phy_ldo;
static i2c_master_bus_handle_t s_i2c_bus;
static esp_lcd_dsi_bus_handle_t s_bus;

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

    /* This is the configuration that was last confirmed to put a visible image
     * on this panel. The bridge's DE window opens (HSW + HBP) into each line,
     * but the DSI payload from the P4 arrives earlier than that, so the content
     * is displaced and the line tail wraps onto the left edge. The compensation
     * moves the DE window by trading back porch for front porch, which keeps
     * the bridge's line total equal to the host's - if it does not, the panel
     * shows nothing at all. */
    int bridge_hbp = p->hbp + TC358762_DE_START_COMP;
    int bridge_hfp = p->hfp - TC358762_DE_START_COMP;
    if (bridge_hfp < 0) {
        bridge_hfp = 0;
    }
    const int host_total = HOSYOND_H_RES + p->hsw + p->hbp + p->hfp;
    const int bridge_total = HOSYOND_H_RES + p->hsw + bridge_hbp + bridge_hfp;
    ESP_LOGI(TAG, "line totals: host %d (hbp %d + hfp %d), bridge %d (hbp %d + hfp %d)",
             host_total, p->hbp, p->hfp, bridge_total, bridge_hbp, bridge_hfp);
    if (host_total != bridge_total) {
        ESP_LOGE(TAG, "MISMATCH: bridge raster %d vs host line %d - panel will be blank",
                 bridge_total, host_total);
    }
    tc358762_write(bus, TC358762_LCD_HS_HBP,
                   ((uint32_t)bridge_hbp << 16) | (uint32_t)p->hsw);
    tc358762_write(bus, TC358762_LCD_HDISP_HFP,
                   ((uint32_t)bridge_hfp << 16) | (uint32_t)HOSYOND_H_RES);
    tc358762_write(bus, TC358762_LCD_VS_VBP,
                   ((uint32_t)p->vbp << 16) | (uint32_t)p->vsw);
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
#if PROBE_HAS_FORCE_HS
        .flags = {
            .clock_lane_force_hs = 1,
        },
#endif
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

#if PROBE_EXP1
    s_bus = bus;
#endif
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

/* Deterministic calibration pattern.
 *
 * Every feature is at a coordinate that can be predicted from the panel's
 * 800x480 active area alone, so a capture can be measured rather than judged:
 *
 *   perimeter    one pixel wide, on x=0, x=799, y=0, y=479
 *   corner mark  40x40 solid squares, inset 20 px, each a different colour
 *                TL red (20,20)  TR green (740,20)
 *                BL blue (20,420) BR white (740,420)
 *   edge ticks   20x6 bars centred on each edge midpoint
 *   centre cross 3 px thick, arms 50 px, crossing at exactly (400,240)
 *
 * If the raster is correct the perimeter is unbroken, the four marks are
 * symmetric about the centre and the cross sits on (400,240). Any displacement
 * shows up as a gap or duplication on one edge and a measurable shift of the
 * cross, and both are quantities rather than impressions.
 */
#define CAL_X_MARK 20
#define CAL_Y_MARK 20
#define CAL_MARK 40
#define CAL_ARM 50
#define CAL_THICK 3

static void draw_calibration_card(esp_lcd_panel_handle_t panel)
{
    const int w = HOSYOND_H_RES;
    const int h = HOSYOND_V_RES;
    const int cx = w / 2;
    const int cy = h / 2;
    uint8_t *buf = ensure_fb();
    if (!buf) {
        return;
    }

    fill_rect(buf, 0, 0, w, h, 0x00, 0x00, 0x00);

    fill_rect(buf, CAL_X_MARK, CAL_Y_MARK,
              CAL_X_MARK + CAL_MARK, CAL_Y_MARK + CAL_MARK, 0xFF, 0x00, 0x00);
    fill_rect(buf, w - CAL_X_MARK - CAL_MARK, CAL_Y_MARK,
              w - CAL_X_MARK, CAL_Y_MARK + CAL_MARK, 0x00, 0xFF, 0x00);
    fill_rect(buf, CAL_X_MARK, h - CAL_Y_MARK - CAL_MARK,
              CAL_X_MARK + CAL_MARK, h - CAL_Y_MARK, 0x00, 0x00, 0xFF);
    fill_rect(buf, w - CAL_X_MARK - CAL_MARK, h - CAL_Y_MARK - CAL_MARK,
              w - CAL_X_MARK, h - CAL_Y_MARK, 0xFF, 0xFF, 0xFF);

    /* The cross is made exactly symmetric about (cx, cy): each arm runs from
     * centre-1 to centre+2 so the centre pixel is included on both sides. */
    fill_rect(buf, cx - CAL_ARM, cy - 1, cx + CAL_ARM + 1, cy + 2,
              0xFF, 0xFF, 0xFF);
    fill_rect(buf, cx - 1, cy - CAL_ARM, cx + 2, cy + CAL_ARM + 1,
              0xFF, 0xFF, 0xFF);

    fill_rect(buf, cx - 1, 0, cx + 2, 20, 0xFF, 0xFF, 0xFF);
    fill_rect(buf, cx - 1, h - 20, cx + 2, h, 0xFF, 0xFF, 0xFF);
    fill_rect(buf, 0, cy - 1, 20, cy + 2, 0xFF, 0xFF, 0xFF);
    fill_rect(buf, w - 20, cy - 1, w, cy + 2, 0xFF, 0xFF, 0xFF);

    fill_rect(buf, 0, 0, w, 1, 0xFF, 0xFF, 0xFF);
    fill_rect(buf, 0, h - 1, w, h, 0xFF, 0xFF, 0xFF);
    fill_rect(buf, 0, 0, 1, h, 0xFF, 0xFF, 0xFF);
    fill_rect(buf, w - 1, 0, w, h, 0xFF, 0xFF, 0xFF);

    esp_err_t err = esp_lcd_panel_draw_bitmap(panel, 0, 0, w, h, buf);
    ESP_LOGI(TAG, "calibration card submitted -> %s", esp_err_to_name(err));

    /* Prove the content side independently of the panel: if these probes pass,
     * the framebuffer really does hold a symmetric, correctly positioned
     * pattern, so any displacement seen on the LCD is scan-out, not drawing.
     * Expected bytes are in buffer order, which is [B][G][R] - see fill_rect. */
    static const struct {
        int x, y;
        uint8_t p0, p1, p2;
        const char *name;
    } probe[] = {
        {0, 0, 0xFF, 0xFF, 0xFF, "perimeter TL"},
        {w - 1, 0, 0xFF, 0xFF, 0xFF, "perimeter TR"},
        {0, h - 1, 0xFF, 0xFF, 0xFF, "perimeter BL"},
        {w - 1, h - 1, 0xFF, 0xFF, 0xFF, "perimeter BR"},
        {cx, cy, 0xFF, 0xFF, 0xFF, "cross centre"},
        {cx, cy - CAL_ARM, 0xFF, 0xFF, 0xFF, "cross top arm"},
        {cx, cy + CAL_ARM, 0xFF, 0xFF, 0xFF, "cross bottom arm"},
        {cx - CAL_ARM, cy, 0xFF, 0xFF, 0xFF, "cross left arm"},
        {cx + CAL_ARM, cy, 0xFF, 0xFF, 0xFF, "cross right arm"},
        {CAL_X_MARK + CAL_MARK / 2, CAL_Y_MARK + CAL_MARK / 2,
         0x00, 0x00, 0xFF, "corner TL red"},
        {w - 1 - CAL_X_MARK - CAL_MARK / 2, CAL_Y_MARK + CAL_MARK / 2,
         0x00, 0xFF, 0x00, "corner TR green"},
        {CAL_X_MARK + CAL_MARK / 2, h - 1 - CAL_Y_MARK - CAL_MARK / 2,
         0xFF, 0x00, 0x00, "corner BL blue"},
        {w - 1 - CAL_X_MARK - CAL_MARK / 2, h - 1 - CAL_Y_MARK - CAL_MARK / 2,
         0xFF, 0xFF, 0xFF, "corner BR white"},
    };
    bool all_ok = true;
    for (size_t i = 0; i < sizeof(probe) / sizeof(probe[0]); i++) {
        const uint8_t *px = buf + ((size_t)probe[i].y * w + (size_t)probe[i].x) * 3;
        const bool ok = px[0] == probe[i].p0 && px[1] == probe[i].p1 &&
                        px[2] == probe[i].p2;
        if (!ok) {
            all_ok = false;
        }
        ESP_LOGI(TAG, "fb %-17s (%3d,%3d) got %02x%02x%02x want %02x%02x%02x %s",
                 probe[i].name, probe[i].x, probe[i].y, px[0], px[1], px[2],
                 probe[i].p0, probe[i].p1, probe[i].p2, ok ? "ok" : "MISMATCH");
    }
    ESP_LOGI(TAG, "framebuffer self-check: %d probes, %s",
             (int)(sizeof(probe) / sizeof(probe[0])), all_ok ? "PASS" : "FAIL");
}

/* Report where lit runs actually sit inside IDF's internal DMA framebuffer -
 * the buffer the DSI host scans out, which is not the buffer we draw into.
 * If this is correct and the panel is still displaced, the fault is downstream
 * of the framebuffer and belongs to scan-out, not drawing. */
static void dma_fb_report(esp_lcd_panel_handle_t panel)
{
    void *fb = NULL;
    esp_err_t err = esp_lcd_dpi_panel_get_frame_buffer(panel, 1, &fb);
    if (err != ESP_OK || !fb) {
        ESP_LOGE(TAG, "cannot get DMA framebuffer: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "DMA framebuffer at %p", fb);

    const int w = HOSYOND_H_RES;
    const int h = HOSYOND_V_RES;
    const uint8_t *base = (const uint8_t *)fb;

    static const int rows[] = {0, 1, 240, h - 2, h - 1};
    for (size_t r = 0; r < sizeof(rows) / sizeof(rows[0]); r++) {
        const int y = rows[r];
        const uint8_t *line = base + (size_t)y * w * 3;
        char buf[192];
        int n = 0;
        int x = 0;
        int first = -1;
        int last = -1;
        int runs = 0;
        while (x < w && runs < 6) {
            const bool lit = (line[x * 3] | line[x * 3 + 1] | line[x * 3 + 2]) != 0;
            if (lit) {
                if (first < 0) {
                    first = x;
                }
                last = x;
            } else if (first >= 0) {
                n += snprintf(buf + n, sizeof(buf) - n, " %d-%d", first, last);
                runs++;
                first = -1;
            }
            if (n > (int)sizeof(buf) - 24) {
                break;
            }
            x++;
        }
        if (first >= 0 && runs < 6) {
            n += snprintf(buf + n, sizeof(buf) - n, " %d-%d", first, last);
            runs++;
        }
        ESP_LOGI(TAG, "dma row %3d runs(%d):%s", y, runs, n ? buf : " none");
    }

    static const int cols[] = {0, 1, HOSYOND_H_RES / 2, w - 2, w - 1};
    for (size_t c = 0; c < sizeof(cols) / sizeof(cols[0]); c++) {
        const int x = cols[c];
        char buf[192];
        int n = 0;
        int y = 0;
        int first = -1;
        int last = -1;
        int runs = 0;
        while (y < h && runs < 6) {
            const uint8_t *px = base + ((size_t)y * w + (size_t)x) * 3;
            const bool lit = (px[0] | px[1] | px[2]) != 0;
            if (lit) {
                if (first < 0) {
                    first = y;
                }
                last = y;
            } else if (first >= 0) {
                n += snprintf(buf + n, sizeof(buf) - n, " %d-%d", first, last);
                runs++;
                first = -1;
            }
            if (n > (int)sizeof(buf) - 24) {
                break;
            }
            y++;
        }
        if (first >= 0 && runs < 6) {
            n += snprintf(buf + n, sizeof(buf) - n, " %d-%d", first, last);
            runs++;
        }
        ESP_LOGI(TAG, "dma col %3d runs(%d):%s", x, runs, n ? buf : " none");
    }
}

#if PROBE_EXP1
/* Experiment 1: is the bridge's own timing generator live?
 * Host profile and every line total stay fixed; only bridge registers change,
 * at runtime, one state at a time, each held long enough to film. The
 * "EXP1 state" log lines carry the timestamps to align a video against. */

static void exp1_run(esp_lcd_panel_handle_t panel)
{
    const h4_profile_t *p = &h4_profiles[H4_DEFAULT_PROFILE];
    static const struct { bool vtg; int hbp_delta; } st[] = {
        { false, 0 },     /* S0 baseline: LCDCTRL 0x150, bridge hbp 300 / hfp 0 */
        { false, -100 },  /* S1 timing regs rewritten, VTGEN still off */
        { true, 0 },      /* S2 VFUEN + LCDCTRL 0x152, hbp 300 */
        { true, -100 },   /* S3 VTGEN on, hbp 200 / hfp 100 */
        { true, -200 },   /* S4 VTGEN on, hbp 100 / hfp 200 */
    };
    for (;;) {
        for (size_t i = 0; i < sizeof(st) / sizeof(st[0]); i++) {
            const int hbp = p->hbp + TC358762_DE_START_COMP + st[i].hbp_delta;
            const int hfp = p->hfp - TC358762_DE_START_COMP - st[i].hbp_delta;
            ESP_LOGW(TAG, "EXP1 state S%u t=%lld ms: VTGEN %d, bridge hbp %d hfp %d (total %d, host %d)",
                     (unsigned)i, (long long)(esp_timer_get_time() / 1000), st[i].vtg, hbp, hfp,
                     HOSYOND_H_RES + p->hsw + hbp + hfp,
                     HOSYOND_H_RES + p->hsw + p->hbp + p->hfp);
            tc358762_write(s_bus, TC358762_LCD_HS_HBP, ((uint32_t)hbp << 16) | (uint32_t)p->hsw);
            tc358762_write(s_bus, TC358762_LCD_HDISP_HFP, ((uint32_t)hfp << 16) | HOSYOND_H_RES);
            tc358762_write(s_bus, TC358762_VFUEN, 1);
            tc358762_write(s_bus, TC358762_LCDCTRL,
                           TC358762_LCDCTRL_UPSTREAM | (st[i].vtg ? 2u : 0u));
            draw_calibration_card(panel);
            vTaskDelay(pdMS_TO_TICKS(12000));
        }
    }
}
#endif

/* ---------------- Touch (ft5x06_touch.h task + marker) ---------------- */

#define TOUCH_MARK 20

/* Yellow square under the touch point, written straight into the panel's
 * frame buffer. Bytes are [B][G][R], so yellow is 00 FF FF. It stays until the
 * next calibration card is submitted. */
static void touch_mark(esp_lcd_panel_handle_t panel, int x, int y)
{
    static uint8_t sq[TOUCH_MARK * TOUCH_MARK * 3];
    for (int i = 0; i < TOUCH_MARK * TOUCH_MARK; i++) {
        sq[i * 3] = 0x00;
        sq[i * 3 + 1] = 0xFF;
        sq[i * 3 + 2] = 0xFF;
    }
    int x0 = x - TOUCH_MARK / 2;
    int y0 = y - TOUCH_MARK / 2;
    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x0 > HOSYOND_H_RES - TOUCH_MARK) {
        x0 = HOSYOND_H_RES - TOUCH_MARK;
    }
    if (y0 > HOSYOND_V_RES - TOUCH_MARK) {
        y0 = HOSYOND_V_RES - TOUCH_MARK;
    }
    esp_lcd_panel_draw_bitmap(panel, x0, y0, x0 + TOUCH_MARK, y0 + TOUCH_MARK, sq);
}

/* Draws a marker for every new touch position for `ms` milliseconds. */
static void touch_service(esp_lcd_panel_handle_t panel, int ms)
{
    uint32_t seen = 0;
    for (int t = 0; t < ms; t += 10) {
        ft5x06_point_t pt;
        if (ft5x06_get(&pt) && pt.seq != seen) {
            seen = pt.seq;
            touch_mark(panel, pt.x, pt.y);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}


/* Self-guided touch test: the screen shows one target at a time, the operator
 * taps it, the device checks the hit and moves on. Nothing to count or time.
 * Result: logged per target, and the final screen is a thick green border when
 * every target was hit, red when any was missed or timed out. */
#define TT_BOX 100    /* target box edge; a hit is anywhere inside it */
#define TT_WAIT_MS 0   /* 0 = wait for the tap as long as it takes */

static void tt_show(esp_lcd_panel_handle_t panel, int cx, int cy, bool all_done,
                    uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t *buf = ensure_fb();
    if (!buf) {
        return;
    }
    fill_rect(buf, 0, 0, HOSYOND_H_RES, HOSYOND_V_RES, 0, 0, 0);
    if (all_done) {
        const int t = 24;
        fill_rect(buf, 0, 0, HOSYOND_H_RES, t, r, g, b);
        fill_rect(buf, 0, HOSYOND_V_RES - t, HOSYOND_H_RES, HOSYOND_V_RES, r, g, b);
        fill_rect(buf, 0, 0, t, HOSYOND_V_RES, r, g, b);
        fill_rect(buf, HOSYOND_H_RES - t, 0, HOSYOND_H_RES, HOSYOND_V_RES, r, g, b);
    } else {
        fill_rect(buf, cx - TT_BOX / 2, cy - TT_BOX / 2, cx + TT_BOX / 2, cy + TT_BOX / 2, 255, 255, 255);
        fill_rect(buf, cx - TT_BOX / 2 + 6, cy - TT_BOX / 2 + 6, cx + TT_BOX / 2 - 6,
                  cy + TT_BOX / 2 - 6, 0, 0, 0);
        fill_rect(buf, cx - 8, cy - 8, cx + 8, cy + 8, 255, 255, 0);
    }
    esp_lcd_panel_draw_bitmap(panel, 0, 0, HOSYOND_H_RES, HOSYOND_V_RES, buf);
}

static void touch_selftest(esp_lcd_panel_handle_t panel)
{
    static const int pts[][2] = {
        {70, 70}, {730, 70}, {730, 410}, {70, 410}, {400, 240},
        {400, 70}, {730, 240}, {400, 410}, {70, 240},
    };
    const int n = sizeof(pts) / sizeof(pts[0]);
    int hits = 0;
    ESP_LOGW(TAG, "TOUCH SELFTEST: tap each yellow-centred box as it appears (%d targets)", n);
    for (int i = 0; i < n; i++) {
        tt_show(panel, pts[i][0], pts[i][1], false, 0, 0, 0);
        /* Wait for the finger to be off the glass, then for a fresh press. */
        ft5x06_point_t pt;
        for (int t = 0; t < 2000 && ft5x06_get(&pt); t += 10) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        bool got = false;
        for (int t = 0; !got && (TT_WAIT_MS == 0 || t < TT_WAIT_MS); t += 10) {
            got = ft5x06_get(&pt);
            if (!got) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }
        if (!got) {
            ESP_LOGW(TAG, "target %d/%d (%d,%d): TIMEOUT, no touch", i + 1, n, pts[i][0], pts[i][1]);
            continue;
        }
        const int dx = pt.x - pts[i][0];
        const int dy = pt.y - pts[i][1];
        const bool hit = dx > -TT_BOX / 2 && dx < TT_BOX / 2 && dy > -TT_BOX / 2 && dy < TT_BOX / 2;
        hits += hit;
        ESP_LOGI(TAG, "target %d/%d (%3d,%3d): touch (%3d,%3d) error dx=%+d dy=%+d -> %s",
                 i + 1, n, pts[i][0], pts[i][1], pt.x, pt.y, dx, dy, hit ? "HIT" : "MISS");
    }
    ESP_LOGW(TAG, "TOUCH SELFTEST RESULT: %d/%d hit -> %s", hits, n, hits == n ? "PASS" : "FAIL");
    tt_show(panel, 0, 0, true, hits == n ? 0 : 255, hits == n ? 255 : 0, 0);
    vTaskDelay(pdMS_TO_TICKS(8000));
}

void app_main(void)
{
    const h4_profile_t *p = &h4_profiles[H4_DEFAULT_PROFILE];

    ESP_LOGW(TAG, "=== OrcSDR P4 DSI probe: Hosyond 7\" 800x480 on Waveshare P4 module dev kit ===");
    ESP_LOGI(TAG, "build %s %s | IDF %s | target %s | PSRAM free %u KB",
             __DATE__, __TIME__, esp_get_idf_version(), CONFIG_IDF_TARGET,
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));

    for (int i = 0; i < H4_PROFILE_COUNT; i++) {
        log_configured_mode(&h4_profiles[i]);
    }
    ESP_LOGI(TAG, "active profile: %s", p->name);

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

    esp_err_t terr = ft5x06_start(s_i2c_bus, TAG);
    ESP_LOGI(TAG, "touch controller 0x%02X start: %s", FT5X06_I2C_ADDR, esp_err_to_name(terr));

#if PROBE_EXP1
    exp1_run(panel);
#endif

    /* Steady-state image: a deterministic calibration pattern whose geometry is
     * fully predictable, re-submitted every few seconds so it also recovers from
     * any transient loss of sync. */
#if PROBE_TOUCH_TEST
    touch_selftest(panel);
#endif
    ESP_LOGW(TAG, "display pipeline configured; showing the calibration card");
    for (;;) {
        draw_calibration_card(panel);
        dma_fb_report(panel);
        touch_service(panel, 5000);
    }

idle:
    ESP_LOGE(TAG, "probe stopped; staying idle so the failure log is not overwritten");
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
