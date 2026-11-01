/* SPDX-License-Identifier: GPL-3.0-or-later
 * Runtime discovery/polling adapted from LakeShark v2.19.2 ls_touch.c;
 * original protocol attribution: LILYGO_L cpp_bus_driver gt9895.cpp.
 * No controller firmware upload. Integration, calibration and error handling
 * use the OrcSDR board interface. Single UI/input owner polls this driver.
 */
#include "gt9895_touch.h"
#include "gt9895_protocol.h"
#include "orcsdr_board_profile.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static i2c_master_dev_handle_t s_dev;
static uint32_t s_data;
static orcsdr_point_t s_last;
static const orcsdr_touch_calibration_t s_cal = {0,1059,0,2399,0,0,0};

static esp_err_t read_reg(uint32_t address, uint8_t *out, size_t bytes)
{
    const uint8_t reg[] = {address>>24,address>>16,address>>8,address};
    return i2c_master_transmit_receive(s_dev,reg,sizeof(reg),out,bytes,30);
}
static esp_err_t reset_touch(i2c_master_bus_handle_t bus)
{
    i2c_master_dev_handle_t dev;
    i2c_device_config_t cfg = {.dev_addr_length=I2C_ADDR_BIT_LEN_7,
        .device_address=orcsdr_board_profile()->ioexp_addr[0],.scl_speed_hz=100000};
    esp_err_t e=i2c_master_bus_add_device(bus,&cfg,&dev);
    if (e) return e;
    uint8_t out=0,dir=0,reg=2;
    e=i2c_master_transmit_receive(dev,&reg,1,&out,1,30);
    reg=6;
    if (!e) e=i2c_master_transmit_receive(dev,&reg,1,&dir,1,30);
    /* Native IO3 reset, IO4 input. Preserve every other expander output. */
    uint8_t latch[]={2,(uint8_t)(out&~(1u<<3))};
    uint8_t config[]={6,(uint8_t)((dir|1u<<4)&~(1u<<3))};
    if (!e) e=i2c_master_transmit(dev,latch,2,30);
    if (!e) e=i2c_master_transmit(dev,config,2,30);
    if (!e) {
        vTaskDelay(pdMS_TO_TICKS(30));latch[1]|=1u<<3;
        e=i2c_master_transmit(dev,latch,2,30);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    i2c_master_bus_rm_device(dev);
    return e;
}
esp_err_t gt9895_touch_start(i2c_master_bus_handle_t bus)
{
    if (s_data) return ESP_OK;
    esp_err_t e=reset_touch(bus);
    if (e) return e;
    i2c_device_config_t cfg={.dev_addr_length=I2C_ADDR_BIT_LEN_7,.device_address=0x5d,.scl_speed_hz=100000};
    e=i2c_master_bus_add_device(bus,&cfg,&s_dev);
    if (e) return e;
    uint8_t fw[28],info[1024];
    e=read_reg(0x10014,fw,sizeof(fw));
    if (e) goto fail;
    if (!gt9895_checksum(fw,sizeof(fw)) || fw[10]!='9' || fw[11]!='8' || fw[12]!='9' || fw[13]!='5') {
        e=ESP_ERR_INVALID_RESPONSE;goto fail;
    }
    e=read_reg(0x10070,info,2);
    if (e) goto fail;
    size_t n=gt9895_le16(info);
    if (n<87 || n>sizeof(info)) {e=ESP_ERR_INVALID_SIZE;goto fail;}
    e=read_reg(0x10070,info,n);
    if (e) goto fail;
    uint32_t data;
    if (!gt9895_report_address(info,n,&data)) {e=ESP_ERR_INVALID_RESPONSE;goto fail;}
    s_data=data;
    ESP_LOGI("GT9895","runtime report=0x%08lx; raw1060x2400 calibration; single contact",(unsigned long)s_data);
    return ESP_OK;
fail:
    i2c_master_bus_rm_device(s_dev);s_dev=NULL;
    return e;
}
esp_err_t gt9895_touch_poll(orcsdr_touch_sample_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    if (!s_data) return ESP_ERR_INVALID_STATE;
    uint8_t p[18];
    esp_err_t e=read_reg(s_data,p,1);
    if (e) return e;
    if (!p[0]) return ESP_ERR_NOT_FOUND;
    e=read_reg(s_data,p,sizeof(p));
    if (e) return e;
    if (!gt9895_checksum(p,8)) return ESP_ERR_INVALID_CRC;
    orcsdr_point_t raw={0};bool down=false;
    bool decoded=gt9895_decode(p,sizeof(p),&raw.x,&raw.y,&down);
    /* Ack a valid-header unsupported multi-contact frame, then cancel the
     * gesture. Corrupt single-contact payloads remain errors, never coordinates. */
    if (!decoded && (p[2]&15)<=1) return ESP_ERR_INVALID_RESPONSE;
    const uint8_t ack[]={s_data>>24,s_data>>16,s_data>>8,s_data,0};
    e=i2c_master_transmit(s_dev,ack,sizeof(ack),30);
    if (e) return e;
    if (!decoded) return ESP_ERR_NOT_SUPPORTED;
    if (down) {
        orcsdr_display_geometry_t native;
        if (!orcsdr_geometry_init(&native,568,1232,0,0,0,0,0) ||
            !orcsdr_touch_to_view(&native,&s_cal,raw,&s_last)) return ESP_ERR_INVALID_RESPONSE;
    }
    *out=(orcsdr_touch_sample_t){.position=s_last,.pressed=down};
    return ESP_OK;
}
