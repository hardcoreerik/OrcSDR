/*
 * Isolated DSI bring-up probe: Hosyond 7" 800x480 panel on a
 * Waveshare ESP32-P4-Module-DEV-KIT.
 *
 * Four separate, individually useful stages, each logged:
 *   1. Scan the DSI-connector I2C bus and identify what is on it.
 *   2. Power the panel through its ATTINY88 controller (bridge power + backlight).
 *   3. Raise the MIPI DPHY LDO, create a one-lane DSI bus, program the TC358762
 *      bridge and start DPI video.
 *   4. Cycle the LCD controller's test patterns.
 *
 * The timing profile is retained across reboots, so one flash sweeps every
 * candidate: after each pass the board restarts with the next profile and logs
 * which one is active.
 *
 * Logs land on the CH343 UART (COM8).
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_attr.h"
#include "esp_idf_version.h"
#include "esp_heap_caps.h"
#include "driver/i2c_master.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_mipi_dsi.h"
#include "hal/mipi_dsi_hal.h"
#include "hal/mipi_dsi_types.h"
#include "hal/mipi_dsi_ll.h"
#include "hosyond_7in.h"
#include "attiny88_panel.h"
#include "tc358762_bridge.h"

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(6, 1, 0)
#error "This probe requires ESP-IDF >= 6.1 for esp_lcd_dsi_bus_config_t.flags.clock_lane_force_hs"
#endif

#define HOSYOND_LANE_COUNT 1

static const char *TAG = "HOSYOND_PROBE";

static esp_ldo_channel_handle_t s_phy_ldo;
static i2c_master_bus_handle_t s_i2c_bus;

/* Survives a software restart, so the sweep advances one profile per boot. */
RTC_NOINIT_ATTR static int s_profile_idx;

static esp_err_t i2c_read_reg(i2c_master_bus_handle_t bus, uint16_t addr, uint8_t reg, uint8_t *val)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = PROBE_I2C_FREQ_HZ,
    };
    i2c_master_dev_handle_t dev = NULL;
    esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &dev);
    if (err != ESP_OK) {
        return err;
    }
    err = i2c_master_transmit_receive(dev, &reg, 1, val, 1, 50);
    i2c_master_bus_rm_device(dev);
    return err;
}

static esp_err_t i2c_write_reg(uint16_t addr, uint8_t reg, uint8_t val)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = PROBE_I2C_FREQ_HZ,
    };
    i2c_master_dev_handle_t dev = NULL;
    esp_err_t err = i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &dev);
    if (err != ESP_OK) {
        return err;
    }
    uint8_t buf[2] = {reg, val};
    err = i2c_master_transmit(dev, buf, sizeof(buf), 50);
    i2c_master_bus_rm_device(dev);
    return err;
}

/* Read-only identification: these registers are readable on all candidates, so
 * nothing here can reconfigure a device. */
static void identify_device(i2c_master_bus_handle_t bus, uint16_t addr)
{
    uint8_t v;
    for (uint8_t reg = 0x00; reg <= 0x03; reg++) {
        if (i2c_read_reg(bus, addr, reg, &v) != ESP_OK) {
            ESP_LOGW(TAG, "    0x%02X reg 0x%02X: read failed", addr, reg);
            break;
        }
        ESP_LOGI(TAG, "    0x%02X reg 0x%02X = 0x%02X", addr, reg, v);
    }
    if (addr == ATTINY_I2C_ADDR && i2c_read_reg(bus, addr, ATTINY_REG_ID, &v) == ESP_OK) {
        ESP_LOGI(TAG, "    0x45 panel-controller ID (0x80) = 0x%02X  [0xDE=v1, 0xC3=v2]", v);
    }
    if (addr == 0x38 && i2c_read_reg(bus, addr, 0xA3, &v) == ESP_OK) {
        ESP_LOGI(TAG, "    0x38 FT5x06 chip ID (0xA3) = 0x%02X", v);
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
    int found = 0;
    for (uint16_t addr = 0x08; addr <= 0x77; addr++) {
        if (i2c_master_probe(s_i2c_bus, addr, 20) == ESP_OK) {
            ESP_LOGI(TAG, "  I2C device ACK at 0x%02X", addr);
            identify_device(s_i2c_bus, addr);
            found++;
        }
    }
    if (found == 0) {
        ESP_LOGW(TAG, "  no I2C devices responded on the DSI connector bus");
    } else {
        ESP_LOGI(TAG, "  %d device(s) responded", found);
    }
}

/* Panel power + backlight in the order the kernel uses it:
 * POWERON=0 -> PWM=0 -> PORTC=0 -> PORTA=LR -> PORTB=MAIN -> PORTC=LED_EN -> PWM.
 * Until this runs the panel looks completely dead even with valid DSI. */
static void attiny_panel_power_on(void)
{
    uint8_t id = 0;
    if (i2c_read_reg(s_i2c_bus, ATTINY_I2C_ADDR, ATTINY_REG_ID, &id) != ESP_OK) {
        ESP_LOGE(TAG, "panel controller not reachable at 0x%02X - skipping power sequence", ATTINY_I2C_ADDR);
        return;
    }
    if (id != ATTINY_VERSION_V1 && id != ATTINY_VERSION_V2) {
        ESP_LOGW(TAG, "panel controller ID 0x%02X is not a known revision (0xDE/0xC3); trying anyway", id);
    } else {
        ESP_LOGI(TAG, "panel controller revision 0x%02X (%s)", id,
                 id == ATTINY_VERSION_V2 ? "v2" : "v1");
    }

    i2c_write_reg(ATTINY_I2C_ADDR, ATTINY_REG_POWERON, 0x00);
    vTaskDelay(pdMS_TO_TICKS(30));
    i2c_write_reg(ATTINY_I2C_ADDR, ATTINY_REG_PWM, 0x00);

    i2c_write_reg(ATTINY_I2C_ADDR, ATTINY_REG_PORTC, 0x00);
    vTaskDelay(pdMS_TO_TICKS(10));
    i2c_write_reg(ATTINY_I2C_ADDR, ATTINY_REG_PORTA, ATTINY_PA_LCD_LR);
    vTaskDelay(pdMS_TO_TICKS(10));
    i2c_write_reg(ATTINY_I2C_ADDR, ATTINY_REG_PORTB, ATTINY_PB_LCD_MAIN);
    vTaskDelay(pdMS_TO_TICKS(10));
    i2c_write_reg(ATTINY_I2C_ADDR, ATTINY_REG_PORTC, ATTINY_PC_LED_EN);
    vTaskDelay(pdMS_TO_TICKS(80));

    uint8_t a = 0, b = 0, c = 0;
    i2c_read_reg(s_i2c_bus, ATTINY_I2C_ADDR, ATTINY_REG_PORTA, &a);
    i2c_read_reg(s_i2c_bus, ATTINY_I2C_ADDR, ATTINY_REG_PORTB, &b);
    i2c_read_reg(s_i2c_bus, ATTINY_I2C_ADDR, ATTINY_REG_PORTC, &c);
    ESP_LOGI(TAG, "ports after power-on: A=0x%02X B=0x%02X C=0x%02X", a, b, c);

    esp_err_t bl = i2c_write_reg(ATTINY_I2C_ADDR, ATTINY_REG_PWM, 0xFF);
    ESP_LOGI(TAG, "backlight PWM -> 0xFF: %s", esp_err_to_name(bl));
}

static void log_configured_mode(const h4_profile_t *p)
{
    const uint32_t htotal = HOSYOND_H_RES + p->hfp + p->hsw + p->hbp;
    const uint32_t vtotal = HOSYOND_V_RES + p->vfp + p->vsw + p->vbp;
    ESP_LOGI(TAG, "profile %s", p->name);
    ESP_LOGI(TAG, "  %dx%d RGB888 lanes=%d lane_rate=%.0f Mbps dpi_clk=%.2f MHz",
             HOSYOND_H_RES, HOSYOND_V_RES, HOSYOND_LANE_COUNT,
             (double)p->lane_mbps, (double)p->dpi_mhz);
    ESP_LOGI(TAG, "  h: fp=%d sw=%d bp=%d (total %u) | v: fp=%d sw=%d bp=%d (total %u) -> ~%.1f Hz",
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

/* esp_lcd_dsi_bus_t is opaque, but its first members are bus_id followed by the
 * HAL context (components/esp_lcd/dsi/mipi_dsi_priv.h). Declaring that exact
 * prefix reaches the HAL for a raw generic long write, which is the only way to
 * reach the bridge's 16-bit register interface. */
typedef struct {
    int bus_id;
    mipi_dsi_hal_context_t hal;
} dsi_bus_prefix_t;

static void tc358762_write(esp_lcd_dsi_bus_handle_t bus, uint16_t addr, uint32_t val)
{
    dsi_bus_prefix_t *priv = (dsi_bus_prefix_t *)bus;
    uint8_t payload[6] = {
        (uint8_t)(addr & 0xff), (uint8_t)(addr >> 8),
        (uint8_t)(val & 0xff), (uint8_t)((val >> 8) & 0xff),
        (uint8_t)((val >> 16) & 0xff), (uint8_t)((val >> 24) & 0xff),
    };
    mipi_dsi_hal_host_gen_write_long_packet(&priv->hal, 0, MIPI_DSI_DT_GENERIC_LONG_WRITE,
                                            payload, sizeof(payload));
}

static void tc358762_bridge_init(esp_lcd_dsi_bus_handle_t bus)
{
    tc358762_write(bus, TC358762_DSI_LANEENABLE, TC358762_LANEENABLE_L0EN_CLEN);
    tc358762_write(bus, TC358762_PPI_D0S_CLRSIPO, TC358762_CLRSIPO_COUNT);
    tc358762_write(bus, TC358762_PPI_D1S_CLRSIPO, TC358762_CLRSIPO_COUNT);
    tc358762_write(bus, TC358762_PPI_D0S_ATMR, 0);
    tc358762_write(bus, TC358762_PPI_D1S_ATMR, 0);
    tc358762_write(bus, TC358762_PPI_LPTXTIMECNT, TC358762_LPX_PERIOD);
    tc358762_write(bus, TC358762_SPICMR, 0x00);

    const uint32_t lcdctrl = TC358762_LCDCTRL_VSDELAY(1) | TC358762_LCDCTRL_RGB888 |
                             TC358762_LCDCTRL_UNK6 | TC358762_LCDCTRL_VTGEN;
    tc358762_write(bus, TC358762_LCDCTRL, lcdctrl);

    tc358762_write(bus, TC358762_SYSCTRL, TC358762_SYSCTRL_VALUE);
    vTaskDelay(pdMS_TO_TICKS(100));

    tc358762_write(bus, TC358762_PPI_STARTPPI, 1);
    tc358762_write(bus, TC358762_DSI_STARTDSI, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "bridge programmed: 1 lane, RGB888, LCDCTRL=0x%08X", (unsigned)lcdctrl);
}

static esp_err_t start_dsi_video(const h4_profile_t *p, esp_lcd_panel_handle_t *out_panel)
{
    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = HOSYOND_LANE_COUNT,
        .phy_clk_src = MIPI_DSI_PHY_PLLREF_CLK_SRC_PLL_F20M,
        .lane_bit_rate_mbps = p->lane_mbps,
        .flags = {
            /* A DSI-to-RGB bridge can drop synchronization unless the clock lane
             * stays in high-speed mode between packets. */
            .clock_lane_force_hs = 1,
        },
    };
    esp_lcd_dsi_bus_handle_t bus = NULL;
    esp_err_t err = esp_lcd_new_dsi_bus(&bus_config, &bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_dsi_bus failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "DSI bus up: %d lane(s) @ %.0f Mbps, clock lane forced high-speed",
             HOSYOND_LANE_COUNT, (double)p->lane_mbps);

    /* A DBI IO must exist before generic writes: creating it is what sets the
     * command path's speed mode and ACK handling in the DSI host. */
    esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    esp_lcd_panel_io_handle_t dbi_io = NULL;
    if (esp_lcd_new_panel_io_dbi(bus, &dbi_config, &dbi_io) == ESP_OK) {
        ESP_LOGI(TAG, "DSI command path (DBI IO) ready");
    } else {
        ESP_LOGW(TAG, "DBI IO creation failed; generic bridge writes may not land");
    }

    /* Release every reset the panel controller exposes, not just LED enable. */
    i2c_write_reg(ATTINY_I2C_ADDR, ATTINY_REG_PORTC, ATTINY_PC_DEASSERT_ALL);

    tc358762_bridge_init(bus);

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
            .disable_lp = 0,
        },
    };

    err = esp_lcd_new_panel_dpi(bus, &dpi_config, out_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_dpi failed: %s", esp_err_to_name(err));
        return err;
    }

    /* The ESP-IDF DPI driver always selects burst-with-sync-pulses. A TC358762
     * converting to parallel RGB needs the continuous non-burst stream the
     * Raspberry Pi produces, so the mode is overridden after panel creation. */
    dsi_bus_prefix_t *priv = (dsi_bus_prefix_t *)bus;
    mipi_dsi_host_ll_dpi_set_video_burst_type(priv->hal.host,
                                              MIPI_DSI_LL_VIDEO_NON_BURST_WITH_SYNC_PULSES);
    ESP_LOGI(TAG, "video mode override: non-burst with sync pulses");

    ESP_LOGI(TAG, "DPI panel created, framebuffer in PSRAM (num_fbs=1)");
    return ESP_OK;
}

void app_main(void)
{
    int idx = s_profile_idx;
    if (idx < 0 || idx >= H4_PROFILE_COUNT) {
        idx = 0;
    }
    const h4_profile_t *p = &h4_profiles[idx];

    ESP_LOGW(TAG, "=== OrcSDR P4 DSI probe: Hosyond 7\" 800x480 on Waveshare P4 module dev kit ===");
    ESP_LOGI(TAG, "IDF %s | target %s | PSRAM free %u KB | sweep %d/%d",
             esp_get_idf_version(), CONFIG_IDF_TARGET,
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
             idx + 1, H4_PROFILE_COUNT);

    log_configured_mode(p);

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

    ESP_LOGI(TAG, "DSI video running. Cycling test patterns every 4 s:");
    static const mipi_dsi_pattern_type_t patterns[] = {
        MIPI_DSI_PATTERN_BAR_HORIZONTAL,
        MIPI_DSI_PATTERN_BAR_VERTICAL,
        MIPI_DSI_PATTERN_BER_VERTICAL,
    };
    static const char *names[] = {"horizontal bars", "vertical bars", "BER pattern"};

    for (size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]); i++) {
        if (esp_lcd_dpi_panel_set_pattern(panel, patterns[i]) == ESP_OK) {
            ESP_LOGI(TAG, "  pattern -> %s", names[i]);
        } else {
            ESP_LOGE(TAG, "  set_pattern(%s) failed", names[i]);
        }
        vTaskDelay(pdMS_TO_TICKS(4000));
    }

    int next = (idx + 1) % H4_PROFILE_COUNT;
    s_profile_idx = next;
    ESP_LOGW(TAG, "profile %s gave no confirmation; restarting for profile %d/%d (%s)",
             p->name, next + 1, H4_PROFILE_COUNT, h4_profiles[next].name);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();

idle:
    ESP_LOGE(TAG, "probe stopped; staying idle so the failure log is not overwritten by a reboot");
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
