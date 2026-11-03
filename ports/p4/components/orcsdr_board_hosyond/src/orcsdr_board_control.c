#include "orcsdr_board_control.h"
#include "orcsdr_board_profile.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static i2c_master_dev_handle_t s_expander;
static i2c_master_bus_handle_t s_secondary;
static SemaphoreHandle_t s_lock;

/* XL9535 vendor IO10..IO17 mean bank1 bits0..7, not linear pins10..17.
 * Profile conversion is explicit here; never shift vendor IO14 as linear14. */
static esp_err_t line(orcsdr_ioexp_line_t pin,bool level,bool input)
{
    if (!s_expander || pin.dev!=0 || pin.pin<0 || pin.pin>17 || pin.pin==8 || pin.pin==9)
        return ESP_ERR_NOT_SUPPORTED;
    const unsigned bank=pin.pin>=10,bit=bank?pin.pin-10:pin.pin;
    const uint8_t mask=(uint8_t)(1u<<bit);
    if (!s_lock || xSemaphoreTake(s_lock,pdMS_TO_TICKS(200))!=pdTRUE) return ESP_ERR_TIMEOUT;
    uint8_t out=0,dir=0,reg=(uint8_t)(2+bank);
    esp_err_t e=i2c_master_transmit_receive(s_expander,&reg,1,&out,1,50);
    reg=(uint8_t)(6+bank);
    if (!e) e=i2c_master_transmit_receive(s_expander,&reg,1,&dir,1,50);
    uint8_t latch[]={(uint8_t)(2+bank),level?(uint8_t)(out|mask):(uint8_t)(out&~mask)};
    uint8_t config[]={(uint8_t)(6+bank),input?(uint8_t)(dir|mask):(uint8_t)(dir&~mask)};
    if (!e) e=i2c_master_transmit(s_expander,latch,2,50);
    if (!e) e=i2c_master_transmit(s_expander,config,2,50);
    xSemaphoreGive(s_lock);
    return e;
}
esp_err_t orcsdr_board_c6_enable(bool enable)
{
    const orcsdr_board_profile_t *b=orcsdr_board_profile();
    if (!b->has_hosted_c6) return ESP_ERR_NOT_SUPPORTED;
    if (b->c6_reset>=0) {
        esp_err_t e=gpio_set_direction(b->c6_reset,GPIO_MODE_OUTPUT);
        return e?e:gpio_set_level(b->c6_reset,enable?1:0);
    }
    return line(b->ioexp_wifi_en,enable,enable); // LilyGO EN release uses external pull-up
}
i2c_master_bus_handle_t orcsdr_board_secondary_i2c(void) { return s_secondary; }
esp_err_t orcsdr_board_radio_reset(bool asserted)
{
    const orcsdr_board_profile_t *b=orcsdr_board_profile();
    return b->has_radio?line(b->radio_rst,!asserted,false):ESP_ERR_NOT_SUPPORTED;
}
esp_err_t orcsdr_board_radio_dio1_read(bool* high)
{
    if(!high)return ESP_ERR_INVALID_ARG;
    *high=false;
    const orcsdr_board_profile_t *b=orcsdr_board_profile();
    const orcsdr_ioexp_line_t pin=b->radio_dio1;
    if(!b->has_radio || !s_expander || pin.dev!=0 || pin.pin<0 || pin.pin>17 || pin.pin==8 || pin.pin==9)
        return ESP_ERR_NOT_SUPPORTED;
    const unsigned bank=pin.pin>=10,bit=bank?pin.pin-10:pin.pin;
    const uint8_t mask=(uint8_t)(1u<<bit);
    if(!s_lock || xSemaphoreTake(s_lock,pdMS_TO_TICKS(200))!=pdTRUE)return ESP_ERR_TIMEOUT;
    uint8_t dir=0,reg=(uint8_t)(6+bank),input=0;
    esp_err_t e=i2c_master_transmit_receive(s_expander,&reg,1,&dir,1,50);
    if(!e && !(dir&mask)) {
        const uint8_t config[]={(uint8_t)(6+bank),(uint8_t)(dir|mask)};
        e=i2c_master_transmit(s_expander,config,2,50);
    }
    reg=(uint8_t)bank;
    if(!e)e=i2c_master_transmit_receive(s_expander,&reg,1,&input,1,50);
    if(!e)*high=(input&mask)!=0;
    xSemaphoreGive(s_lock);
    return e;
}
esp_err_t orcsdr_board_controls_start(i2c_master_bus_handle_t panel_bus)
{
    const orcsdr_board_profile_t *b=orcsdr_board_profile();
    if (!panel_bus) return ESP_ERR_INVALID_ARG;
    s_secondary=panel_bus;
    if (b->codec_i2c_sda>=0) {
        i2c_master_bus_config_t cfg={.i2c_port=b->codec_i2c_port,.sda_io_num=b->codec_i2c_sda,
            .scl_io_num=b->codec_i2c_scl,.clk_source=I2C_CLK_SRC_DEFAULT,.glitch_ignore_cnt=7,
            .flags.enable_internal_pullup=true};
        esp_err_t e=i2c_new_master_bus(&cfg,&s_secondary);
        if (e) return e;
    }
    if (!b->ioexp_addr[0]) return ESP_OK;
    s_lock=xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    i2c_device_config_t cfg={.dev_addr_length=I2C_ADDR_BIT_LEN_7,.device_address=b->ioexp_addr[0],.scl_speed_hz=100000};
    esp_err_t e=i2c_master_bus_add_device(panel_bus,&cfg,&s_expander);
    if (e) return e;
    // Drive only documented board rails. The display/touch drivers own resets.
    if (b->ioexp_speaker_en.dev>=0 && (e=line(b->ioexp_speaker_en,true,false))) return e;
    if (b->ioexp_sd_power.dev>=0 && (e=line(b->ioexp_sd_power,false,false))) return e;
    if (b->ioexp_usb_phy_en.dev>=0 && (e=line(b->ioexp_usb_phy_en,true,false))) return e;
    if (b->has_hosted_c6) {
        e=orcsdr_board_c6_enable(false);
        if (e) return e;
        vTaskDelay(pdMS_TO_TICKS(20));
        e=orcsdr_board_c6_enable(true);
    }
    return e;
}
