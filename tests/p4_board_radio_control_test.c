#include "orcsdr_board_control.h"
#include "orcsdr_board_profile.h"
#include "freertos/semphr.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static orcsdr_board_profile_t profile;
static uint8_t regs[8];
static int locked,fail_read,fail_write,timeout;
const orcsdr_board_profile_t* orcsdr_board_profile(void){return &profile;}
SemaphoreHandle_t xSemaphoreCreateMutex(void){return (void*)1;}
int xSemaphoreTake(SemaphoreHandle_t s,unsigned ms){(void)s;(void)ms;if(timeout)return 0;assert(!locked);locked=1;return 1;}
void xSemaphoreGive(SemaphoreHandle_t s){(void)s;assert(locked);locked=0;}
void vTaskDelay(unsigned ms){(void)ms;}
int gpio_set_direction(int pin,int mode){(void)pin;(void)mode;return 0;}
int gpio_set_level(int pin,int level){(void)pin;(void)level;return 0;}
esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t* c,i2c_master_bus_handle_t* b){(void)c;*b=(void*)2;return 0;}
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t b,const i2c_device_config_t* c,i2c_master_dev_handle_t* d){(void)b;(void)c;*d=(void*)3;return 0;}
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t d,const uint8_t* w,size_t wn,uint8_t* r,size_t rn,int ms){
  (void)d;(void)ms;assert(locked&&wn==1&&rn==1&&*w<8);if(fail_read)return ESP_ERR_TIMEOUT;*r=regs[*w];return 0;}
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t d,const uint8_t* w,size_t n,int ms){
  (void)d;(void)ms;assert(locked&&n==2&&w[0]<8);if(fail_write)return ESP_ERR_TIMEOUT;regs[w[0]]=w[1];return 0;}
int main(void){
  memset(&profile,0,sizeof(profile));profile.codec_i2c_sda=-1;
  profile.ioexp_speaker_en.dev=profile.ioexp_sd_power.dev=profile.ioexp_usb_phy_en.dev=-1;
  profile.ioexp_addr[0]=0x20;profile.has_radio=true;
  profile.radio_rst=(orcsdr_ioexp_line_t){0,16};profile.radio_dio1=(orcsdr_ioexp_line_t){0,17};
  assert(orcsdr_board_controls_start((void*)1)==ESP_OK);
  regs[3]=regs[7]=0xff;
  assert(orcsdr_board_gnss_prepare()==ESP_ERR_NOT_SUPPORTED&&regs[3]==0xff);
  profile.has_gnss=true;profile.gnss_startup_line=(orcsdr_ioexp_line_t){0,11};
  assert(orcsdr_board_gnss_prepare()==ESP_OK&&regs[3]==0xfd&&regs[7]==0xfd);
  fail_read=1;assert(orcsdr_board_gnss_prepare()==ESP_ERR_TIMEOUT&&!locked);fail_read=0;
  fail_write=1;assert(orcsdr_board_gnss_prepare()==ESP_ERR_TIMEOUT&&!locked);fail_write=0;
  timeout=1;assert(orcsdr_board_gnss_prepare()==ESP_ERR_TIMEOUT);timeout=0;
  regs[3]=0xff;regs[7]=0x35;
  assert(orcsdr_board_radio_reset(true)==ESP_OK);
  assert(regs[3]==0xbf&&regs[7]==0x35); // bank1 bit6, preserve C6/USB/SD bits
  assert(orcsdr_board_radio_reset(false)==ESP_OK&&regs[3]==0xff);
  regs[7]=0;regs[1]=0x80;bool high=false;
  assert(orcsdr_board_radio_dio1_read(&high)==ESP_OK&&high&&regs[7]==0x80);
  regs[1]=0;assert(orcsdr_board_radio_dio1_read(&high)==ESP_OK&&!high);
  profile.radio_ant_sw=(orcsdr_ioexp_line_t){0,1};
  regs[2]=regs[6]=0xff;
  assert(orcsdr_board_radio_select_antenna(true)==ESP_ERR_NOT_SUPPORTED&&regs[2]==0xff);
  profile.radio_ant_polarity=ORCSDR_RADIO_ANT_EXTERNAL_LOW;
  assert(orcsdr_board_radio_select_antenna(true)==ESP_OK&&regs[2]==0xfd&&regs[6]==0xfd&&regs[3]==0xff);
  assert(orcsdr_board_radio_select_antenna(false)==ESP_OK&&regs[2]==0xff);
  profile.radio_ant_polarity=ORCSDR_RADIO_ANT_EXTERNAL_HIGH;
  assert(orcsdr_board_radio_select_antenna(true)==ESP_OK&&regs[2]==0xff);
  assert(orcsdr_board_radio_dio1_read(NULL)==ESP_ERR_INVALID_ARG);
  fail_read=1;high=true;assert(orcsdr_board_radio_dio1_read(&high)==ESP_ERR_TIMEOUT&&!high&&!locked);fail_read=0;
  fail_write=1;assert(orcsdr_board_radio_reset(true)==ESP_ERR_TIMEOUT&&!locked);fail_write=0;
  timeout=1;assert(orcsdr_board_radio_reset(true)==ESP_ERR_TIMEOUT);timeout=0;
  profile.has_radio=false;assert(orcsdr_board_radio_reset(true)==ESP_ERR_NOT_SUPPORTED);
  high=true;assert(orcsdr_board_radio_dio1_read(&high)==ESP_ERR_NOT_SUPPORTED&&!high);
  profile.has_radio=true;profile.radio_rst.pin=8;assert(orcsdr_board_radio_reset(true)==ESP_ERR_NOT_SUPPORTED);
  puts("P4 board radio expander ownership/mapping/failure checks PASS");
}
