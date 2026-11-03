#include "orcsdr_sx126x.h"
#include "orcsdr_board_profile.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static orcsdr_board_profile_t profile;
static int bus_owned,device_owned,cs=1,type,busy,floating,drop_write,occupied,spi_failure;
static int transfers,resets,free_calls,cs_changes,remove_failure,free_failure;
static unsigned faults;
static int chip_mode=2,tcxo_set,calibrated,clock_failure,mode_failure,fault_only;
static int64_t now;
static int rx_started,irq_sent,rx_crc_error,rx_header_error,rx_timeout,rx_mode_failure,payload_reads,antenna_external;
static uint8_t registers[4096];
static uint8_t wire[272];
static size_t wire_offset;
static unsigned payload_length=3;
static size_t driver_limit=64;
static int limit_error,fail_payload_chunk;
static unsigned payload_chunks;
const orcsdr_board_profile_t* orcsdr_board_profile(void){return &profile;}
int gpio_config(const gpio_config_t* c){assert(c->pin_bit_mask);return 0;}
int gpio_get_level(int pin){assert(pin==6);return busy;}
int gpio_set_level(int pin,int level){assert(pin==24);cs=level;++cs_changes;if(!level){wire_offset=0;memset(wire,0,sizeof(wire));}return 0;}
void vTaskDelay(unsigned ticks){now+=ticks*1000;}
void esp_rom_delay_us(unsigned us){now+=us;}
int64_t esp_timer_get_time(void){return now;}
esp_err_t orcsdr_board_radio_reset(bool asserted){++resets;if(asserted){type=0;chip_mode=2;tcxo_set=calibrated=0;memset(registers,0,sizeof(registers));registers[0x740]=0x14;registers[0x741]=0x24;}return 0;}
esp_err_t orcsdr_board_radio_select_antenna(bool external){antenna_external=external;return 0;}
esp_err_t orcsdr_board_radio_dio1_read(bool* high){*high=true;return 0;}
int spi_bus_initialize(spi_host_device_t host,const spi_bus_config_t* config,int dma){
  assert(host==1&&config->sclk_io_num==2&&config->miso_io_num==4&&config->mosi_io_num==3&&dma==0);
  if(occupied||bus_owned)return ESP_ERR_INVALID_STATE;
  assert(!bus_owned);bus_owned=1;return 0;
}
int spi_bus_add_device(spi_host_device_t host,const spi_device_interface_config_t* config,spi_device_handle_t* device){
  assert(host==1&&bus_owned&&!device_owned&&config->spics_io_num==-1);device_owned=1;*device=(void*)1;return 0;}
int spi_bus_get_max_transaction_len(spi_host_device_t host,size_t* limit){assert(host==1&&device_owned);*limit=driver_limit;return limit_error;}
int spi_bus_remove_device(spi_device_handle_t device){assert(device&&device_owned);if(remove_failure)return ESP_FAIL;device_owned=0;return 0;}
int spi_bus_free(spi_host_device_t host){assert(host==1&&bus_owned&&!device_owned);++free_calls;if(free_failure)return ESP_FAIL;bus_owned=0;return 0;}
int spi_device_polling_transmit(spi_device_handle_t device,spi_transaction_t* transaction){
  assert(device&&device_owned&&bus_owned&&cs==0&&transaction->length%8==0);++transfers;
  if(spi_failure)return ESP_FAIL;
  const unsigned char* tx=transaction->tx_buffer;unsigned char* rx=transaction->rx_buffer;
  const size_t n=transaction->length/8;
  if(n>driver_limit)return ESP_ERR_INVALID_ARG; // configured driver limit
  const size_t offset=wire_offset;
  assert(offset+n<=sizeof(wire));memcpy(wire+offset,tx,n);wire_offset+=n;
  if(wire[0]==0x1e) {
    assert(rx);
    ++payload_chunks;
    if(fail_payload_chunk && payload_chunks==(unsigned)fail_payload_chunk)return ESP_FAIL;
    if(offset==0)++payload_reads;
    memset(rx,0,n);
    for(size_t i=0;i<n;++i)if(offset+i>=3) {
      const size_t index=offset+i-3;assert(index<payload_length);
      rx[i]=payload_length==3?(uint8_t)"abc"[index]:(uint8_t)(index%251);
    }
    return 0;
  }
  assert(offset==0);
  assert(tx[0]!=0x83&&tx[0]!=0xd1&&tx[0]!=0xd2); // no TX/cw/preamble opcode
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
  if(tx[0]==0x1d){unsigned address=(tx[1]<<8)|tx[2];assert(n>=4&&rx&&address+n-4<sizeof(registers));memcpy(rx+4,registers+address,n-4);}
  if(tx[0]==0x0d){unsigned address=(tx[1]<<8)|tx[2];assert(n>=3&&address+n-3<sizeof(registers));memcpy(registers+address,tx+3,n-3);}
  if(tx[0]==0x8b)assert(n==5&&tx[1]==11&&tx[2]==5&&tx[3]==1&&tx[4]==0);
  if(tx[0]==0x8c)assert(n==7&&tx[1]==0&&tx[2]==16&&tx[3]==0&&tx[4]==255&&tx[5]==1&&tx[6]==0);
  if(tx[0]==0x86)assert(n==5&&tx[1]==0x38&&tx[2]==0xae&&tx[3]==0&&tx[4]==0);
  if(tx[0]==0x82){assert(n==4&&tx[1]==255&&tx[2]==255&&tx[3]==255);rx_started=1;chip_mode=rx_mode_failure?2:5;}
  if(tx[0]==0x12){assert(n==4&&rx_started&&rx);if(!irq_sent){rx[3]=(uint8_t)(2|(rx_crc_error?64:0)|(rx_header_error?32:0));if(rx_timeout){rx[2]=2;rx[3]=0;}irq_sent=1;}}
  if(tx[0]==0x13){assert(n==4&&rx);rx[2]=(uint8_t)payload_length;rx[3]=0;}
  if(tx[0]==0x14){assert(n==5&&rx);rx[2]=100;rx[3]=20;rx[4]=100;}
  return 0;
}
typedef struct {unsigned ready,packets,rejected,polls;} receive_test_t;
static bool keep_receiving(void* context){receive_test_t* t=context;return !t->packets&&!t->rejected&&t->polls++<8;}
static void receive_event(void* context,const orcsdr_sx126x_rx_event_t* event){
  receive_test_t* t=context;
  if(event->kind==ORCSDR_SX126X_RX_READY)++t->ready;
  else if(event->kind==ORCSDR_SX126X_RX_REJECTED)++t->rejected;
  else {++t->packets;assert(event->length==payload_length&&event->rssi_dbm==-50&&event->snr_db==5&&event->dio1_high);
    if(payload_length==3)assert(!memcmp(event->data,"abc",3));
    else for(unsigned i=0;i<payload_length;++i)assert(event->data[i]==i%251);
  }
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
  profile.radio_tcxo_mv=1600;
  receive_test_t receive={0};
  orcsdr_sx126x_rx_request_t request={906875000,true,keep_receiving,receive_event,&receive};
  assert(orcsdr_sx126x_receive_longfast(&request,&result)==ESP_OK);
  assert(receive.ready==1&&receive.packets==1&&payload_reads==1&&!antenna_external&&chip_mode==2);clean();
  assert(registers[0x740]==0x24&&registers[0x741]==0xb4);
  receive=(receive_test_t){0};irq_sent=rx_started=0;rx_crc_error=1;
  assert(orcsdr_sx126x_receive_longfast(&request,&result)==ESP_OK);
  assert(receive.rejected==1&&receive.packets==0&&payload_reads==1&&!antenna_external);clean();rx_crc_error=0;
  receive=(receive_test_t){0};irq_sent=rx_started=0;rx_header_error=1;
  assert(orcsdr_sx126x_receive_longfast(&request,&result)==ESP_OK&&receive.rejected==1&&receive.packets==0&&payload_reads==1);clean();rx_header_error=0;
  receive=(receive_test_t){0};irq_sent=rx_started=0;rx_timeout=1;
  assert(orcsdr_sx126x_receive_longfast(&request,&result)==ESP_ERR_TIMEOUT&&!receive.packets&&!antenna_external);clean();rx_timeout=0;
  receive=(receive_test_t){0};irq_sent=rx_started=0;rx_mode_failure=1;
  assert(orcsdr_sx126x_receive_longfast(&request,&result)==ESP_ERR_INVALID_RESPONSE);
  assert(!receive.ready&&!receive.packets&&!antenna_external);clean();rx_mode_failure=0;
  receive=(receive_test_t){0};irq_sent=rx_started=0;payload_length=255;
  assert(orcsdr_sx126x_receive_longfast(&request,&result)==ESP_OK&&receive.packets==1);clean();
  const size_t limits[]={32,64,128,272,4096};
  const unsigned lengths[]={1,3,16,61,62,63,64,65,121,127,128,129,254,255};
  for(unsigned a=0;a<sizeof(limits)/sizeof(limits[0]);++a){
    driver_limit=limits[a];
    for(unsigned b=0;b<sizeof(lengths)/sizeof(lengths[0]);++b){
      receive=(receive_test_t){0};irq_sent=rx_started=0;payload_length=lengths[b];payload_chunks=0;
      assert(orcsdr_sx126x_receive_longfast(&request,&result)==ESP_OK&&receive.packets==1);
      assert(payload_chunks==(payload_length+3+driver_limit-1)/driver_limit);clean();
    }
  }
  driver_limit=64;payload_length=255;payload_chunks=0;fail_payload_chunk=2;
  receive=(receive_test_t){0};irq_sent=rx_started=0;
  assert(orcsdr_sx126x_receive_longfast(&request,&result)==ESP_FAIL&&!receive.packets);clean();fail_payload_chunk=0;
  const int before_invalid=transfers;
  driver_limit=0;assert(orcsdr_sx126x_probe(&result)==ESP_ERR_INVALID_SIZE&&!result.detected);clean();
  driver_limit=64;limit_error=ESP_FAIL;
  assert(orcsdr_sx126x_probe(&result)==ESP_FAIL&&!result.detected);clean();limit_error=0;
  assert(transfers==before_invalid);
  assert(orcsdr_sx126x_probe(&result)==ESP_OK&&result.detected);clean();
  remove_failure=1;
  const int before_remove_free=free_calls;
  assert(orcsdr_sx126x_probe(&result)==ESP_FAIL&&cs==1&&bus_owned&&device_owned);
  assert(free_calls==before_remove_free); // don't free a bus with a live device
  assert(orcsdr_sx126x_probe(&result)==ESP_ERR_INVALID_STATE); // no silent successful retry
  remove_failure=0;device_owned=bus_owned=0; // simulate platform recovery/reset
  free_failure=1;
  assert(orcsdr_sx126x_probe(&result)==ESP_FAIL&&cs==1&&bus_owned&&!device_owned);
  assert(orcsdr_sx126x_probe(&result)==ESP_ERR_INVALID_STATE);
  free_failure=0;bus_owned=0;
  assert(orcsdr_sx126x_probe(&result)==ESP_OK);clean();
  puts("SX126x driver-reported SPI limits: 70 packet/limit cases, partial failure, invalid limits and retry PASS");
  puts("SX126x real driver/HAL probe framing, floating bus, BUSY, ownership and retry PASS");
}
