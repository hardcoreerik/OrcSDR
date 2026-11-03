#include "orcsdr_sx126x.h"
#include "orcsdr_board_control.h"
#include "orcsdr_board_profile.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sx126x.h"
#include "sx126x_hal.h"
#include <string.h>

enum { MAX_TRANSFER=272 };
typedef struct {
    spi_device_handle_t spi;
    gpio_num_t busy,cs;
    esp_err_t error;
} radio_bus_t;
static portMUX_TYPE s_claim_mux=portMUX_INITIALIZER_UNLOCKED;
static bool s_claimed;

static esp_err_t wait_ready(radio_bus_t* bus)
{
    const int64_t deadline=esp_timer_get_time()+200000;
    while(gpio_get_level(bus->busy)) {
        if(esp_timer_get_time()>=deadline)return bus->error=ESP_ERR_TIMEOUT;
        vTaskDelay(1);
    }
    return ESP_OK;
}

static sx126x_hal_status_t transfer(const void* context,const uint8_t* command,uint16_t command_length,
                                  const uint8_t* write_data,uint8_t* read_data,uint16_t data_length)
{
    radio_bus_t* bus=(radio_bus_t*)context;
    if(!bus || !bus->spi || !command || !command_length ||
       (size_t)command_length+data_length>MAX_TRANSFER ||
       (data_length && !write_data && !read_data))return SX126X_HAL_STATUS_ERROR;
    if(wait_ready(bus)!=ESP_OK)return SX126X_HAL_STATUS_ERROR;
    uint8_t tx[MAX_TRANSFER]={0},rx[MAX_TRANSFER]={0};
    memcpy(tx,command,command_length);
    if(write_data && data_length)memcpy(tx+command_length,write_data,data_length);
    spi_transaction_t transaction={.length=8u*(command_length+data_length),
        .tx_buffer=tx,.rx_buffer=read_data?rx:NULL};
    gpio_set_level(bus->cs,0);
    bus->error=spi_device_polling_transmit(bus->spi,&transaction);
    gpio_set_level(bus->cs,1);
    if(bus->error!=ESP_OK)return SX126X_HAL_STATUS_ERROR;
    if(read_data && data_length)memcpy(read_data,rx+command_length,data_length);
    // Sleep deliberately raises BUSY until a later wake transaction.
    if(command[0]!=0x84 && wait_ready(bus)!=ESP_OK)return SX126X_HAL_STATUS_ERROR;
    return SX126X_HAL_STATUS_OK;
}

sx126x_hal_status_t sx126x_hal_write(const void* context,const uint8_t* command,uint16_t command_length,
                                  const uint8_t* data,uint16_t data_length)
{return transfer(context,command,command_length,data,NULL,data_length);}
sx126x_hal_status_t sx126x_hal_read(const void* context,const uint8_t* command,uint16_t command_length,
                                 uint8_t* data,uint16_t data_length)
{return transfer(context,command,command_length,NULL,data,data_length);}
sx126x_hal_status_t sx126x_hal_reset(const void* context)
{
    radio_bus_t* bus=(radio_bus_t*)context;
    bus->error=orcsdr_board_radio_reset(true);
    if(bus->error!=ESP_OK)return SX126X_HAL_STATUS_ERROR;
    vTaskDelay(pdMS_TO_TICKS(2));
    bus->error=orcsdr_board_radio_reset(false);
    if(bus->error!=ESP_OK)return SX126X_HAL_STATUS_ERROR;
    vTaskDelay(pdMS_TO_TICKS(10));
    return wait_ready(bus)==ESP_OK?SX126X_HAL_STATUS_OK:SX126X_HAL_STATUS_ERROR;
}
sx126x_hal_status_t sx126x_hal_wakeup(const void* context)
{
    radio_bus_t* bus=(radio_bus_t*)context;
    const uint8_t bytes[]={0xc0,0x00};
    spi_transaction_t transaction={.length=16,.tx_buffer=bytes};
    gpio_set_level(bus->cs,0);
    esp_rom_delay_us(200);
    bus->error=spi_device_polling_transmit(bus->spi,&transaction);
    gpio_set_level(bus->cs,1);
    if(bus->error!=ESP_OK)return SX126X_HAL_STATUS_ERROR;
    return wait_ready(bus)==ESP_OK?SX126X_HAL_STATUS_OK:SX126X_HAL_STATUS_ERROR;
}

esp_err_t orcsdr_sx126x_probe(orcsdr_sx126x_probe_result_t* result)
{
    if(!result)return ESP_ERR_INVALID_ARG;
    memset(result,0,sizeof(*result));
    const orcsdr_board_profile_t* profile=orcsdr_board_profile();
    if(!profile->has_radio || !profile->radio_part || strcmp(profile->radio_part,"SX1262") ||
       profile->radio_spi_host<1 || profile->radio_spi_host>2 || profile->radio_spi_hz<=0 ||
       !GPIO_IS_VALID_OUTPUT_GPIO(profile->radio_sclk) || !GPIO_IS_VALID_OUTPUT_GPIO(profile->radio_mosi) ||
       !GPIO_IS_VALID_GPIO(profile->radio_miso) || !GPIO_IS_VALID_OUTPUT_GPIO(profile->radio_cs) ||
       !GPIO_IS_VALID_GPIO(profile->radio_busy))return ESP_ERR_NOT_SUPPORTED;
    portENTER_CRITICAL(&s_claim_mux);
    const bool claimed=!s_claimed;
    if(claimed)s_claimed=true;
    portEXIT_CRITICAL(&s_claim_mux);
    if(!claimed)return ESP_ERR_INVALID_STATE;
    const spi_host_device_t host=(spi_host_device_t)profile->radio_spi_host;
    radio_bus_t bus={.busy=profile->radio_busy,.cs=profile->radio_cs,.error=ESP_OK};
    bool bus_owned=false;
    esp_err_t error;
    const spi_bus_config_t config={.mosi_io_num=profile->radio_mosi,.miso_io_num=profile->radio_miso,
        .sclk_io_num=profile->radio_sclk,.quadwp_io_num=-1,.quadhd_io_num=-1,.max_transfer_sz=MAX_TRANSFER};
    if((error=spi_bus_initialize(host,&config,SPI_DMA_DISABLED))!=ESP_OK)goto cleanup;
    bus_owned=true;
    gpio_config_t pins={.pin_bit_mask=1ULL<<bus.cs,.mode=GPIO_MODE_OUTPUT};
    if((error=gpio_config(&pins))!=ESP_OK)goto cleanup;
    gpio_set_level(bus.cs,1);
    pins.pin_bit_mask=1ULL<<bus.busy;pins.mode=GPIO_MODE_INPUT;
    if((error=gpio_config(&pins))!=ESP_OK)goto cleanup;
    const spi_device_interface_config_t device={.clock_speed_hz=profile->radio_spi_hz,
        .mode=0,.spics_io_num=-1,.queue_size=1};
    if((error=spi_bus_add_device(host,&device,&bus.spi))!=ESP_OK)goto cleanup;
    sx126x_chip_status_t status={0};
    sx126x_pkt_type_t packet_type=SX126X_PKT_TYPE_GFSK;
    sx126x_errors_mask_t faults=0;
    if(sx126x_hal_reset(&bus)!=SX126X_HAL_STATUS_OK ||
       sx126x_set_standby(&bus,SX126X_STANDBY_CFG_RC)!=SX126X_STATUS_OK ||
       sx126x_get_status(&bus,&status)!=SX126X_STATUS_OK) {
        error=bus.error==ESP_OK?ESP_FAIL:bus.error;goto cleanup;
    }
    result->chip_mode=status.chip_mode;result->command_status=status.cmd_status;
    if(status.chip_mode!=SX126X_CHIP_MODE_STBY_RC || status.cmd_status==SX126X_CMD_STATUS_CMD_TIMEOUT ||
       status.cmd_status==SX126X_CMD_STATUS_CMD_PROCESS_ERROR || status.cmd_status==SX126X_CMD_STATUS_CMD_EXEC_FAILURE) {
        error=ESP_ERR_INVALID_RESPONSE;goto cleanup;
    }
    // A change/readback pair rejects floating all-zero/all-one MISO responses.
    if(sx126x_set_pkt_type(&bus,SX126X_PKT_TYPE_LORA)!=SX126X_STATUS_OK ||
       sx126x_get_pkt_type(&bus,&packet_type)!=SX126X_STATUS_OK || packet_type!=SX126X_PKT_TYPE_LORA ||
       sx126x_set_pkt_type(&bus,SX126X_PKT_TYPE_GFSK)!=SX126X_STATUS_OK ||
       sx126x_get_pkt_type(&bus,&packet_type)!=SX126X_STATUS_OK || packet_type!=SX126X_PKT_TYPE_GFSK ||
       sx126x_get_device_errors(&bus,&faults)!=SX126X_STATUS_OK) {
        error=bus.error==ESP_OK?ESP_ERR_INVALID_RESPONSE:bus.error;goto cleanup;
    }
    result->device_errors=faults;
    result->detected=true;error=ESP_OK;
cleanup:
    if(error!=ESP_OK && bus_owned)(void)orcsdr_board_radio_reset(true);
    if(bus.spi)spi_bus_remove_device(bus.spi);
    if(bus_owned) {
        spi_bus_free(host);
        gpio_set_level(bus.cs,1);
    }
    portENTER_CRITICAL(&s_claim_mux);s_claimed=false;portEXIT_CRITICAL(&s_claim_mux);
    return error;
}
