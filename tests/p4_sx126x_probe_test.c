#include "orcsdr_sx126x.h"
#include "orcsdr_board_profile.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static orcsdr_board_profile_t profile;
static int bus_owned,device_owned,cs=1,type,busy,floating,drop_write,occupied,spi_failure;
static int transfers,resets,free_calls,cs_changes;
static unsigned faults;
static int chip_mode=2,tcxo_set,calibrated,clock_failure,mode_failure,fault_only;
static int64_t now;
const orcsdr_board_profile_t* orcsdr_board_profile(void){return &profile;}
int gpio_config(const gpio_config_t* c){assert(c->pin_bit_mask);return 0;}
int gpio_get_level(int pin){assert(pin==6);return busy;}
int gpio_set_level(int pin,int level){assert(pin==24);cs=level;++cs_changes;return 0;}
void vTaskDelay(unsigned ticks){now+=ticks*1000;}
void esp_rom_delay_us(unsigned us){now+=us;}
int64_t esp_timer_get_time(void){return now;}
esp_err_t orcsdr_board_radio_reset(bool asserted){++resets;if(asserted){type=0;chip_mode=2;tcxo_set=calibrated=0;}return 0;}
int spi_bus_initialize(spi_host_device_t host,const spi_bus_config_t* config,int dma){
  assert(host==1&&config->sclk_io_num==2&&config->miso_io_num==4&&config->mosi_io_num==3&&dma==0);
  if(occupied)return ESP_ERR_INVALID_STATE;
  assert(!bus_owned);bus_owned=1;return 0;
}
int spi_bus_add_device(spi_host_device_t host,const spi_device_interface_config_t* config,spi_device_handle_t* device){
  assert(host==1&&bus_owned&&!device_owned&&config->spics_io_num==-1);device_owned=1;*device=(void*)1;return 0;}
int spi_bus_remove_device(spi_device_handle_t device){assert(device&&device_owned);device_owned=0;return 0;}
int spi_bus_free(spi_host_device_t host){assert(host==1&&bus_owned&&!device_owned);bus_owned=0;++free_calls;return 0;}
int spi_device_polling_transmit(spi_device_handle_t device,spi_transaction_t* transaction){
  assert(device&&device_owned&&bus_owned&&cs==0&&transaction->length%8==0);++transfers;
  if(spi_failure)return ESP_FAIL;
  const unsigned char* tx=transaction->tx_buffer;unsigned char* rx=transaction->rx_buffer;
  const size_t n=transaction->length/8;
  assert(tx[0]==0x80||tx[0]==0xc0||tx[0]==0x8a||tx[0]==0x11||tx[0]==0x17||
         tx[0]==0x97||tx[0]==0x07||tx[0]==0x89); // no RF-start opcode
  if(rx)memset(rx,floating==2?0xff:0,n);
  if(floating)return 0;
  if(tx[0]==0x80){assert(n==2&&tx[1]<=1);chip_mode=tx[1]&&calibrated&&!clock_failure&&!mode_failure?3:2;}
  if(tx[0]==0xc0){assert(n==2&&rx);rx[1]=(unsigned char)((chip_mode<<4)|4);}
  if(tx[0]==0x97){assert(n==5&&tx[1]==0&&tx[2]==0&&tx[3]==1&&tx[4]==0x40);tcxo_set=1;}
  if(tx[0]==0x07){assert(tcxo_set);faults=0;}
  if(tx[0]==0x89){assert(n==2&&tx[1]==0x7f&&tcxo_set);calibrated=1;if(clock_failure)faults=0x20;if(fault_only)faults=0x40;}
  if(tx[0]==0x8a){assert(n==2);if(!drop_write)type=tx[1];}
  if(tx[0]==0x11){assert(n==3&&rx);rx[2]=(unsigned char)type;}
  if(tx[0]==0x17){assert(n==4&&rx);rx[2]=(unsigned char)(faults>>8);rx[3]=(unsigned char)faults;}
  return 0;
}
static void clean(void){assert(!bus_owned&&!device_owned&&cs==1);}
int main(void){
  profile=(orcsdr_board_profile_t){.has_radio=true,.radio_part="SX1262",.radio_spi_host=1,
    .radio_spi_hz=10000000,.radio_sclk=2,.radio_mosi=3,.radio_miso=4,.radio_cs=24,.radio_busy=6};
  orcsdr_sx126x_probe_result_t result;
  assert(orcsdr_sx126x_probe(NULL)==ESP_ERR_INVALID_ARG);
  assert(orcsdr_sx126x_probe(&result)==ESP_OK&&result.detected&&result.chip_mode==2&&type==0);clean();
  assert(transfers==7&&free_calls==1);
  faults=0x20;assert(orcsdr_sx126x_probe(&result)==ESP_OK&&result.detected&&result.device_errors==0x20);clean();faults=0;
  floating=1;assert(orcsdr_sx126x_probe(&result)==ESP_ERR_INVALID_RESPONSE&&!result.detected);clean();
  floating=2;assert(orcsdr_sx126x_probe(&result)==ESP_ERR_INVALID_RESPONSE&&!result.detected);clean();floating=0;
  drop_write=1;assert(orcsdr_sx126x_probe(&result)==ESP_ERR_INVALID_RESPONSE&&!result.detected);clean();drop_write=0;
  spi_failure=1;assert(orcsdr_sx126x_probe(&result)==ESP_FAIL&&!result.detected);clean();spi_failure=0;
  busy=1;assert(orcsdr_sx126x_probe(&result)==ESP_ERR_TIMEOUT&&!result.detected);clean();busy=0;
  const int old_cs_changes=cs_changes,old_resets=resets,old_free=free_calls;
  occupied=1;assert(orcsdr_sx126x_probe(&result)==ESP_ERR_INVALID_STATE&&!result.detected);
  assert(cs_changes==old_cs_changes&&resets==old_resets&&free_calls==old_free);occupied=0;
  profile.radio_part="LR2021";assert(orcsdr_sx126x_probe(&result)==ESP_ERR_NOT_SUPPORTED&&!result.detected);
  profile.radio_part="SX1262";profile.radio_cs=100;
  assert(orcsdr_sx126x_probe(&result)==ESP_ERR_NOT_SUPPORTED&&!result.detected);
  profile.radio_cs=24;
  profile.radio_part="SX1262";assert(orcsdr_sx126x_probe(&result)==ESP_OK&&result.detected);clean();
  profile.radio_tcxo_mv=1600;profile.radio_tcxo_startup_us=5000;
  faults=0x20;assert(orcsdr_sx126x_clock_check(&result)==ESP_OK);
  assert(result.detected&&result.clock_checked&&result.clock_ready&&result.initial_device_errors==0x20&&result.device_errors==0&&chip_mode==2);clean();
  clock_failure=1;assert(orcsdr_sx126x_clock_check(&result)==ESP_ERR_INVALID_RESPONSE);
  assert(result.detected&&result.clock_checked&&!result.clock_ready&&result.device_errors==0x20);clean();clock_failure=0;
  mode_failure=1;assert(orcsdr_sx126x_clock_check(&result)==ESP_ERR_INVALID_RESPONSE);
  assert(result.device_errors==0&&!result.clock_ready);clean();mode_failure=0;
  fault_only=1;assert(orcsdr_sx126x_clock_check(&result)==ESP_ERR_INVALID_RESPONSE);
  assert(result.chip_mode==3&&result.device_errors==0x40&&!result.clock_ready);clean();fault_only=0;
  profile.radio_tcxo_mv=0;
  assert(orcsdr_sx126x_clock_check(&result)==ESP_ERR_NOT_SUPPORTED&&!result.clock_ready);clean();
  puts("SX126x real driver/HAL probe framing, floating bus, BUSY, ownership and retry PASS");
}
