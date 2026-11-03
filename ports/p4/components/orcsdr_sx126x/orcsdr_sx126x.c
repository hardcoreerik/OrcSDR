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
    size_t transaction_limit;
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
    if(!bus || !bus->spi || !bus->transaction_limit || !command || !command_length ||
       (size_t)command_length+data_length>MAX_TRANSFER ||
       (data_length && !write_data && !read_data))return SX126X_HAL_STATUS_ERROR;
    if(wait_ready(bus)!=ESP_OK)return SX126X_HAL_STATUS_ERROR;
    uint8_t tx[MAX_TRANSFER]={0},rx[MAX_TRANSFER]={0};
    memcpy(tx,command,command_length);
    if(write_data && data_length)memcpy(tx+command_length,write_data,data_length);
    gpio_set_level(bus->cs,0);
    // Respect the active SPI driver limit (FIFO or DMA), while holding NSS
    // across chunks so the radio sees one command and its entire payload.
    const size_t total=(size_t)command_length+data_length;
    for(size_t offset=0;offset<total;) {
        const size_t chunk=total-offset>bus->transaction_limit?bus->transaction_limit:total-offset;
        spi_transaction_t transaction={.length=8u*chunk,
            .tx_buffer=tx+offset,.rx_buffer=read_data?rx+offset:NULL};
        bus->error=spi_device_polling_transmit(bus->spi,&transaction);
        if(bus->error!=ESP_OK)break;
        offset+=chunk;
    }
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

static esp_err_t receive_loop(radio_bus_t* bus,const orcsdr_sx126x_rx_request_t* request)
{
    const orcsdr_board_profile_t* profile=orcsdr_board_profile();
    const uint16_t mhz=(uint16_t)(request->frequency_hz/1000000);
    const sx126x_mod_params_lora_t modulation={SX126X_LORA_SF11,SX126X_LORA_BW_250,SX126X_LORA_CR_4_5,0};
    const sx126x_pkt_params_lora_t packet={16,SX126X_LORA_PKT_EXPLICIT,255,true,false};
    const uint16_t interrupts=SX126X_IRQ_RX_DONE|SX126X_IRQ_CRC_ERROR|SX126X_IRQ_HEADER_ERROR|SX126X_IRQ_TIMEOUT;
    if(sx126x_set_dio2_as_rf_sw_ctrl(bus,profile->radio_dio2_rf_switch)!=SX126X_STATUS_OK ||
       sx126x_cal_img_in_mhz(bus,mhz-4,mhz+4)!=SX126X_STATUS_OK ||
       sx126x_set_pkt_type(bus,SX126X_PKT_TYPE_LORA)!=SX126X_STATUS_OK ||
       sx126x_set_rf_freq(bus,request->frequency_hz)!=SX126X_STATUS_OK ||
       sx126x_set_buffer_base_address(bus,0,0)!=SX126X_STATUS_OK ||
       sx126x_set_lora_mod_params(bus,&modulation)!=SX126X_STATUS_OK ||
       sx126x_set_lora_pkt_params(bus,&packet)!=SX126X_STATUS_OK ||
       sx126x_set_lora_sync_word(bus,0x2b)!=SX126X_STATUS_OK ||
       sx126x_set_dio_irq_params(bus,interrupts,interrupts,0,0)!=SX126X_STATUS_OK ||
       sx126x_clear_irq_status(bus,SX126X_IRQ_ALL)!=SX126X_STATUS_OK)
        return bus->error==ESP_OK?ESP_FAIL:bus->error;
    if(!request->keep_running(request->context))return ESP_OK;
    if(sx126x_set_rx_with_timeout_in_rtc_step(bus,0xffffff)!=SX126X_STATUS_OK)
        return bus->error==ESP_OK?ESP_FAIL:bus->error;
    sx126x_chip_status_t status={0};
    sx126x_errors_mask_t faults=0;
    if(sx126x_get_status(bus,&status)!=SX126X_STATUS_OK ||
       sx126x_get_device_errors(bus,&faults)!=SX126X_STATUS_OK)
        return bus->error==ESP_OK?ESP_FAIL:bus->error;
    if(status.chip_mode!=SX126X_CHIP_MODE_RX || faults)return ESP_ERR_INVALID_RESPONSE;
    orcsdr_sx126x_rx_event_t event={.kind=ORCSDR_SX126X_RX_READY};
    request->event(request->context,&event);
    while(request->keep_running(request->context)) {
        sx126x_irq_mask_t irq=0;
        if(sx126x_get_irq_status(bus,&irq)!=SX126X_STATUS_OK)
            return bus->error==ESP_OK?ESP_FAIL:bus->error;
        if(irq) {
            // Continuous RX should not time out; do not keep advertising RX active.
            if(irq&SX126X_IRQ_TIMEOUT)return ESP_ERR_TIMEOUT;
            memset(&event,0,sizeof(event));event.irq=irq;
            if(irq&(SX126X_IRQ_CRC_ERROR|SX126X_IRQ_HEADER_ERROR)) {
                event.kind=ORCSDR_SX126X_RX_REJECTED;
                request->event(request->context,&event);
            } else if(irq&SX126X_IRQ_RX_DONE) {
                sx126x_rx_buffer_status_t buffer={0};
                sx126x_pkt_status_lora_t signal={0};
                if(sx126x_get_rx_buffer_status(bus,&buffer)!=SX126X_STATUS_OK ||
                   sx126x_get_lora_pkt_status(bus,&signal)!=SX126X_STATUS_OK ||
                   sx126x_read_buffer(bus,buffer.buffer_start_pointer,event.data,buffer.pld_len_in_bytes)!=SX126X_STATUS_OK)
                    return bus->error==ESP_OK?ESP_FAIL:bus->error;
                event.kind=ORCSDR_SX126X_RX_PACKET;
                event.length=buffer.pld_len_in_bytes;
                event.rssi_dbm=signal.rssi_pkt_in_dbm;event.snr_db=signal.snr_pkt_in_db;
                (void)orcsdr_board_radio_dio1_read(&event.dio1_high);
                request->event(request->context,&event);
            }
            if(sx126x_clear_irq_status(bus,irq)!=SX126X_STATUS_OK)
                return bus->error==ESP_OK?ESP_FAIL:bus->error;
        }
        vTaskDelay(pdMS_TO_TICKS(10)+1);
    }
    return ESP_OK;
}

static esp_err_t inspect(orcsdr_sx126x_probe_result_t* result,bool clock_check,
                         const orcsdr_sx126x_rx_request_t* receiver)
{
    if(!result)return ESP_ERR_INVALID_ARG;
    memset(result,0,sizeof(*result));
    if(receiver && (!receiver->keep_running || !receiver->event ||
                    receiver->frequency_hz<150000000 || receiver->frequency_hz>960000000))return ESP_ERR_INVALID_ARG;
    const orcsdr_board_profile_t* profile=orcsdr_board_profile();
    if(!profile->has_radio || !profile->radio_part || strcmp(profile->radio_part,"SX1262") ||
       profile->radio_spi_host<1 || profile->radio_spi_host>2 || profile->radio_spi_hz<=0 ||
       !GPIO_IS_VALID_OUTPUT_GPIO(profile->radio_sclk) || !GPIO_IS_VALID_OUTPUT_GPIO(profile->radio_mosi) ||
       !GPIO_IS_VALID_GPIO(profile->radio_miso) || !GPIO_IS_VALID_OUTPUT_GPIO(profile->radio_cs) ||
       !GPIO_IS_VALID_GPIO(profile->radio_busy))return ESP_ERR_NOT_SUPPORTED;
    int tcxo_voltage=-1;
    uint32_t startup_ticks=0;
    if(clock_check) {
        const unsigned mv[]={1600,1700,1800,2200,2400,2700,3000,3300};
        for(unsigned i=0;i<sizeof(mv)/sizeof(mv[0]);++i)
            if(profile->radio_tcxo_mv==mv[i])tcxo_voltage=(int)i;
        if(tcxo_voltage<0 || !profile->radio_tcxo_startup_us || profile->radio_tcxo_startup_us>100000)
            return ESP_ERR_NOT_SUPPORTED;
        // SX126x TCXO timeout is in 15.625 us RTC steps; round up.
        startup_ticks=(uint32_t)(((uint64_t)profile->radio_tcxo_startup_us*64+999)/1000);
    }
    portENTER_CRITICAL(&s_claim_mux);
    const bool claimed=!s_claimed;
    if(claimed)s_claimed=true;
    portEXIT_CRITICAL(&s_claim_mux);
    if(!claimed)return ESP_ERR_INVALID_STATE;
    const spi_host_device_t host=(spi_host_device_t)profile->radio_spi_host;
    radio_bus_t bus={.busy=profile->radio_busy,.cs=profile->radio_cs,.error=ESP_OK};
    bool bus_owned=false;
    bool antenna_selected=false;
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
    if((error=spi_bus_get_max_transaction_len(host,&bus.transaction_limit))!=ESP_OK)goto cleanup;
    if(!bus.transaction_limit){error=ESP_ERR_INVALID_SIZE;goto cleanup;}
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
    result->initial_device_errors=faults;
    result->detected=true;error=ESP_OK;
    if(clock_check) {
        result->clock_checked=true;
        if(sx126x_set_dio3_as_tcxo_ctrl(&bus,(sx126x_tcxo_ctrl_voltages_t)tcxo_voltage,startup_ticks)!=SX126X_STATUS_OK ||
           sx126x_clear_device_errors(&bus)!=SX126X_STATUS_OK ||
           sx126x_cal(&bus,SX126X_CAL_ALL)!=SX126X_STATUS_OK ||
           sx126x_set_standby(&bus,SX126X_STANDBY_CFG_XOSC)!=SX126X_STATUS_OK ||
           sx126x_get_status(&bus,&status)!=SX126X_STATUS_OK ||
           sx126x_get_device_errors(&bus,&faults)!=SX126X_STATUS_OK) {
            error=bus.error==ESP_OK?ESP_FAIL:bus.error;goto cleanup;
        }
        result->device_errors=faults;
        result->chip_mode=status.chip_mode;result->command_status=status.cmd_status;
        if(faults || status.chip_mode!=SX126X_CHIP_MODE_STBY_XOSC ||
           status.cmd_status==SX126X_CMD_STATUS_CMD_TIMEOUT ||
           status.cmd_status==SX126X_CMD_STATUS_CMD_PROCESS_ERROR ||
           status.cmd_status==SX126X_CMD_STATUS_CMD_EXEC_FAILURE) {
            error=ESP_ERR_INVALID_RESPONSE;goto cleanup;
        }
        if(sx126x_set_standby(&bus,SX126X_STANDBY_CFG_RC)!=SX126X_STATUS_OK) {
            error=bus.error==ESP_OK?ESP_FAIL:bus.error;goto cleanup;
        }
        result->clock_ready=true;
    }
    if(receiver && receiver->keep_running(receiver->context)) {
        antenna_selected=true;
        error=orcsdr_board_radio_select_antenna(receiver->external_antenna);
        if(error!=ESP_OK)goto cleanup;
        error=receive_loop(&bus,receiver);
    }
cleanup:
    if(antenna_selected) {
        if(error==ESP_OK && sx126x_set_standby(&bus,SX126X_STANDBY_CFG_RC)!=SX126X_STATUS_OK)
            error=bus.error==ESP_OK?ESP_FAIL:bus.error;
        const esp_err_t antenna_error=orcsdr_board_radio_select_antenna(false);
        if(error==ESP_OK && antenna_error!=ESP_OK)error=antenna_error;
    }
    if(error!=ESP_OK && bus_owned)(void)orcsdr_board_radio_reset(true);
    if(bus.spi) {
        const esp_err_t removed=spi_bus_remove_device(bus.spi);
        if(removed==ESP_OK)bus.spi=NULL;
        if(error==ESP_OK)error=removed;
    }
    if(bus_owned) {
        // A failed remove still owns the device; do not free its live bus.
        if(!bus.spi) {
            const esp_err_t freed=spi_bus_free(host);
            if(error==ESP_OK)error=freed;
        }
        const esp_err_t deselected=gpio_set_level(bus.cs,1);
        if(error==ESP_OK)error=deselected;
    }
    portENTER_CRITICAL(&s_claim_mux);s_claimed=false;portEXIT_CRITICAL(&s_claim_mux);
    return error;
}
esp_err_t orcsdr_sx126x_probe(orcsdr_sx126x_probe_result_t* result)
{return inspect(result,false,NULL);}
esp_err_t orcsdr_sx126x_clock_check(orcsdr_sx126x_probe_result_t* result)
{return inspect(result,true,NULL);}
esp_err_t orcsdr_sx126x_receive_longfast(const orcsdr_sx126x_rx_request_t* request,
                                      orcsdr_sx126x_probe_result_t* result)
{
    if(!request) {if(result)memset(result,0,sizeof(*result));return ESP_ERR_INVALID_ARG;}
    return inspect(result,true,request);
}
