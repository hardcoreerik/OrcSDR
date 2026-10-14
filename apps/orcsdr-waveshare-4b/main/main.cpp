// OrcSDR Waveshare 4B Home v1. IQ only, no audio, Wi-Fi, or C6 writes.
// A per-screen port; never scale the existing Tab5 dashboard blindly.
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "lvgl.h"
#include "esp_rtl_sdr.h"
#include "home_4b_layout.hpp"
#include "home_fft.hpp"

using namespace orcsdr::waveshare4b;
static constexpr char TAG[]="ORCSDR_4B_HOME";
static constexpr uint32_t kRate=960000;
static QueueHandle_t queue=nullptr;
static std::atomic<uint32_t> requested{kDefaultHz},applied{0},sps{0},drops{0},step_idx{2};
static std::atomic<int> state{0}; // 0 no SDR, 1 USB search, 2 IQ stream, 3 error
static portMUX_TYPE bands_lock=portMUX_INITIALIZER_UNLOCKED;
static uint8_t bands[kFftSize]{};
static uint32_t band_seq=0,rendered_seq=0;
static lv_obj_t *freq_label=nullptr,*status_label=nullptr,*step_label=nullptr;
static lv_obj_t *scope=nullptr,*waterfall=nullptr;
static uint16_t *scope_px=nullptr,*water_px=nullptr;
enum class Command:uint8_t {Down,Up,Step,FM,WX,Air,Ham};
static void tap(lv_event_t* e) {
  const intptr_t code=reinterpret_cast<intptr_t>(lv_event_get_user_data(e));
  if(code<0||code>6)return;
  const Command c=static_cast<Command>(code);
  if(queue)(void)xQueueSend(queue,&c,0);
}
static lv_obj_t* box(lv_obj_t* parent,Rect r,uint32_t color) {
  lv_obj_t* obj=lv_obj_create(parent);
  lv_obj_set_pos(obj,r.x,r.y);
  lv_obj_set_size(obj,r.w,r.h);
  lv_obj_set_style_bg_color(obj,lv_color_hex(color),0);
  lv_obj_set_style_bg_opa(obj,LV_OPA_COVER,0);
  lv_obj_set_style_border_width(obj,1,0);
  lv_obj_set_style_border_color(obj,lv_color_hex(0x245060),0);
  lv_obj_set_style_radius(obj,6,0);
  lv_obj_set_style_pad_all(obj,0,0);
  lv_obj_remove_flag(obj,LV_OBJ_FLAG_SCROLLABLE);
  return obj;
}
static lv_obj_t* caption(lv_obj_t* root,Rect r,const char* title,uint32_t color,bool centered=false) {
  lv_obj_t* label=lv_label_create(root);
  lv_obj_set_pos(label,r.x,r.y);
  lv_obj_set_size(label,r.w,r.h);
  lv_label_set_text(label,title);
  lv_label_set_long_mode(label,LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_color(label,lv_color_hex(color),0);
  if(centered)lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);
  return label;
}
static void control(lv_obj_t* root,Rect r,const char* label,Command c) {
  lv_obj_t* btn=box(root,r,0x12323E);
  lv_obj_add_flag(btn,LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_border_color(btn,lv_color_hex(0x468D96),0);
  lv_obj_add_event_cb(btn,tap,LV_EVENT_CLICKED,
                      reinterpret_cast<void*>(static_cast<intptr_t>(c)));
  caption(btn,{0,(r.h-20)/2,r.w,20},label,0xE0F8F4,true);
}
static uint16_t palette(uint8_t level) {
  if(level<30)return 0x0820;
  if(level<80)return static_cast<uint16_t>(0x0013+(level-30)/2);
  if(level<135)return static_cast<uint16_t>(0x05A0+(level-80)*6);
  if(level<190)return static_cast<uint16_t>(0x07E0+(level-135)*0x0400);
  return static_cast<uint16_t>(0xFFE0+(level-190)/4);
}
static void draw_real_rf(const uint8_t* fft) {
  constexpr int w=kSpectrum.w,h=kSpectrum.h,rows=kWaterfall.h;
  for(int y=0;y<h;++y)for(int x=0;x<w;++x){
    int idx=x*int(kFftSize)/w;
    int line=h-1-int(fft[idx])*int(h-8)/255;
    scope_px[y*w+x]=x==w/2?0x7BEF:
      (y==line||y==line+1)?0x07FF:(x%83==0||y%30==0?0x10A4:0x0841);
  }
  memmove(water_px+w,water_px,(rows-1)*w*sizeof(uint16_t));
  for(int x=0;x<w;++x)water_px[x]=palette(fft[x*int(kFftSize)/w]);
  lv_obj_invalidate(scope);
  lv_obj_invalidate(waterfall);
}
static void ui_timer(lv_timer_t*) {
  char msg[120];
  uint32_t display_hz=applied.load()?applied.load():requested.load();
  snprintf(msg,sizeof(msg),"%u.%06u MHz%s",
           unsigned(display_hz/1000000),unsigned(display_hz%1000000),
           applied.load()?"":" (target)");
  lv_label_set_text(freq_label,msg);
  snprintf(msg,sizeof(msg),"STEP %u Hz  |  IQ ONLY",unsigned(kSteps[step_idx.load()]));
  lv_label_set_text(step_label,msg);
  int link=state.load();
  const char* condition=link==2?"RECEIVING IQ":link==1?"SEARCHING USB":
                        link==3?"USB ERROR - SEE LOG":"NO SDR CONNECTED";
  snprintf(msg,sizeof(msg),"%s | %u kS/s | drops %u",condition,
           unsigned(sps.load()/1000),unsigned(drops.load()));
  lv_label_set_text(status_label,msg);
  uint8_t updated[kFftSize]{};
  bool fresh=false;
  portENTER_CRITICAL(&bands_lock);
  if(band_seq!=rendered_seq) {
    memcpy(updated,bands,sizeof(updated));
    rendered_seq=band_seq;fresh=true;
  }
  portEXIT_CRITICAL(&bands_lock);
  if(fresh&&scope_px&&water_px)draw_real_rf(updated);
}
static bool home_start() {
  if(!bsp_display_lock(3000))return false;
  lv_obj_t* screen=lv_screen_active();
  lv_obj_set_style_bg_color(screen,lv_color_hex(0x07121C),0);
  lv_obj_remove_flag(screen,LV_OBJ_FLAG_SCROLLABLE);
  box(screen,kHeader,0x09212C);
  caption(screen,{28,24,300,25},"ORCSDR  /  HOME",0x75F4DD);
  caption(screen,{350,24,335,25},"WAVESHARE 4B",0x92DAD4,true);
  box(screen,kStatus,0x0A202A);
  status_label=caption(screen,{28,80,664,25},"NO SDR CONNECTED",0xF2C879);
  box(screen,kTuner,0x0E2933);
  control(screen,kTunerMinus,"-",Command::Down);
  freq_label=caption(screen,kTunerReadout,"96.100000 MHz (target)",0xFFFFFF,true);
  control(screen,kTunerPlus,"+",Command::Up);
  box(screen,kSpectrumPanel,0x09212C);
  caption(screen,{28,210,664,20},"SPECTRUM / RELATIVE FFT - NOT dBm",0x8FD7DC);
  box(screen,kWaterfallPanel,0x09212C);
  caption(screen,{28,371,664,20},"LIVE WATERFALL / NEWEST AT TOP",0x8FD7DC);
  box(screen,kControls,0x09212C);
  control(screen,kPreset1,"FM 96.1",Command::FM);
  control(screen,kPreset2,"WX 162.4",Command::WX);
  control(screen,kPreset3,"AIR 125",Command::Air);
  control(screen,kPreset4,"2M 145.120",Command::Ham);
  control(screen,kTuneDown,"TUNE -",Command::Down);
  control(screen,kStep,"STEP",Command::Step);
  control(screen,kTuneUp,"TUNE +",Command::Up);
  step_label=caption(screen,{216,669,286,21},"STEP 12500 Hz | IQ ONLY",0xD9EEEB,true);
  const size_t scope_size=size_t(kSpectrum.w)*kSpectrum.h*2;
  const size_t water_size=size_t(kWaterfall.w)*kWaterfall.h*2;
  scope_px=static_cast<uint16_t*>(heap_caps_malloc(scope_size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  water_px=static_cast<uint16_t*>(heap_caps_malloc(water_size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if(!scope_px||!water_px){
    ESP_LOGE(TAG,"PSRAM canvas allocation failed");
    bsp_display_unlock();return false;
  }
  for(size_t i=0;i<scope_size/2;++i)scope_px[i]=0x0841;
  for(size_t i=0;i<water_size/2;++i)water_px[i]=0x0820;
  scope=lv_canvas_create(screen);
  lv_canvas_set_buffer(scope,scope_px,kSpectrum.w,kSpectrum.h,LV_COLOR_FORMAT_RGB565);
  lv_obj_set_pos(scope,kSpectrum.x,kSpectrum.y);
  waterfall=lv_canvas_create(screen);
  lv_canvas_set_buffer(waterfall,water_px,kWaterfall.w,kWaterfall.h,LV_COLOR_FORMAT_RGB565);
  lv_obj_set_pos(waterfall,kWaterfall.x,kWaterfall.y);
  lv_timer_create(ui_timer,150,nullptr);
  bsp_display_unlock();
  ESP_LOGI(TAG,"HOME_4B screen initialized 720x720 / IQ-only");
  return true;
}
static void radio_task(void*) {
  uint8_t* iq=static_cast<uint8_t*>(heap_caps_malloc(16384,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if(!iq){state=3;ESP_LOGE(TAG,"IQ buffer alloc failed");vTaskDelete(nullptr);return;}
  esp_rtl_sdr_config_t cfg;
  esp_rtl_sdr_config_default(&cfg);
  cfg.host_library_already_installed=false; // One USB Host owner: esp-rtl-sdr.
  cfg.delivery_mode=ESP_RTL_SDR_DELIVERY_READ;
  cfg.transfer_count=6;cfg.transfer_bytes=16384;
  esp_rtl_sdr_handle_t sdr=nullptr;
  esp_err_t result=esp_rtl_sdr_install(&cfg,&sdr);
  if(result!=ESP_OK){
    ESP_LOGE(TAG,"SDR install failed: %s",esp_rtl_sdr_err_to_name(result));
    state=3;heap_caps_free(iq);vTaskDelete(nullptr);return;
  }
  bool streaming=false;
  int64_t last_scan=0,last_fft=0,last_metrics=0;
  for(;;){
    Command cmd;
    while(xQueueReceive(queue,&cmd,0)==pdTRUE){
      if(cmd==Command::Step){step_idx=(step_idx.load()+1)%6;continue;}
      uint32_t next=requested.load();
      switch(cmd){
        case Command::Down:next=tune(next,-1,kSteps[step_idx.load()]);break;
        case Command::Up:next=tune(next,1,kSteps[step_idx.load()]);break;
        case Command::FM:next=96100000;break;
        case Command::WX:next=162400000;break;
        case Command::Air:next=125000000;break;
        case Command::Ham:next=145120000;break;
        default:break;
      }
      requested=next;
      ESP_LOGI(TAG,"HOME_4B TUNE requested_hz=%u streaming=%d",unsigned(next),int(streaming));
      if(streaming){
        esp_err_t e=esp_rtl_sdr_retune_hz(sdr,next);
        if(e!=ESP_OK)ESP_LOGE(TAG,"retune failed: %s",esp_rtl_sdr_err_to_name(e));
      }
    }
    int64_t now=esp_timer_get_time();
    if(!streaming){
      applied=0;sps=0;
      if(now-last_scan>1000000){
        last_scan=now;state=1;
        size_t count=0,raw_count=0;
        const esp_err_t raw_result=esp_rtl_sdr_usb_device_count(&raw_count);
        const esp_err_t refresh_result=esp_rtl_sdr_refresh_device_list(sdr);
        const esp_err_t count_result=
            refresh_result==ESP_OK ? esp_rtl_sdr_get_device_count(sdr,&count)
                                   : refresh_result;
        // The raw USB count includes hubs and unsupported devices. An empty
        // candidate list is NOT evidence that the 4B supplies USB VBUS.
        // Distinguish absent power/connection from unsupported SDR profiles.
        static uint32_t scan_count=0;
        if((++scan_count % 5u)==1u || raw_count || count){
          ESP_LOGI(TAG,"HOME_4B USB_DIAG raw_devices=%u accepted_sdr=%u raw=%s scan=%s safe_mode=%u",
                   unsigned(raw_count),unsigned(count),
                   esp_rtl_sdr_err_to_name(raw_result),
                   esp_rtl_sdr_err_to_name(count_result),
                   unsigned(esp_rtl_sdr_usb_safe_mode_active()));
        }
        if(refresh_result==ESP_OK && count_result==ESP_OK && count>0){
          esp_rtl_sdr_stream_config_t sc;
          esp_rtl_sdr_stream_config_default(&sc);
          sc.preset=ESP_RTL_SDR_PRESET_CUSTOM_HZ;
          sc.frequency_hz=requested.load();
          sc.sample_rate_sps=kRate;
          esp_err_t e=esp_rtl_sdr_start(sdr,&sc);
          if(e==ESP_OK){
            streaming=true;state=2;
            ESP_LOGI(TAG,"HOME_4B RF START hz=%u rate=%u",
                     unsigned(sc.frequency_hz),unsigned(kRate));
          }else{
            ESP_LOGE(TAG,"start failed: %s",esp_rtl_sdr_err_to_name(e));
            state=3;
          }
        }else state=0;
      }
      vTaskDelay(pdMS_TO_TICKS(30));continue;
    }
    if(esp_rtl_sdr_get_state(sdr)!=ESP_RTL_SDR_STATE_STREAMING){
      ESP_LOGW(TAG,"stream ended, rescan");
      (void)esp_rtl_sdr_stop(sdr,1500);
      streaming=false;state=0;continue;
    }
    size_t n=0;
    esp_err_t e=esp_rtl_sdr_read(sdr,iq,16384,40,&n);
    if(e==ESP_OK&&n>=kFftSize*2&&now-last_fft>150000){
      Spectrum view=spectrum_from_cu8(iq,n);
      if(view.real_samples){
        portENTER_CRITICAL(&bands_lock);
        memcpy(bands,view.values.data(),kFftSize);++band_seq;
        portEXIT_CRITICAL(&bands_lock);
        last_fft=now;
      }
    }else if(e!=ESP_OK&&e!=ESP_RTL_SDR_ERR_TIMEOUT){
      ESP_LOGW(TAG,"read error %s",esp_rtl_sdr_err_to_name(e));
      vTaskDelay(pdMS_TO_TICKS(50));
    }
    if(now-last_metrics>750000){
      last_metrics=now;
      esp_rtl_sdr_metrics_t m{};
      if(esp_rtl_sdr_get_metrics(sdr,&m)==ESP_OK){
        applied=m.frequency_hz;sps=m.effective_sps;
        drops=m.slot_starve_blocks+m.ring_overrun_blocks+m.pull_lock_miss_blocks;
        ESP_LOGI(TAG,"HOME_4B IQ hz=%u sps=%u consumed=%llu drops=%u",
                 unsigned(m.frequency_hz),unsigned(m.effective_sps),
                 (unsigned long long)m.bytes_consumed,unsigned(drops.load()));
      }
    }
  }
}
extern "C" void app_main(void) {
  ESP_LOGI(TAG,"OrcSDR Waveshare 4B Home v1, IQ-only; no audio/C6");
  ESP_LOGI(TAG,"driver=%s psram=%u",esp_rtl_sdr_get_version_string(),
           unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
  queue=xQueueCreate(12,sizeof(Command));
  if(!queue){ESP_LOGE(TAG,"Queue creation failed");return;}
  lv_display_t* disp=bsp_display_start();
  if(!disp||bsp_display_backlight_on()!=ESP_OK||!home_start()){
    ESP_LOGE(TAG,"Display or Home initialization failed");return;
  }
  // Hardware OTG VBUS requires physical validation; powered hub recommended.
  if(xTaskCreatePinnedToCore(radio_task,"4b_radio",8192,nullptr,5,nullptr,1)!=pdPASS){
    state=3;ESP_LOGE(TAG,"Receiver task creation failed");
  }
  ESP_LOGI(TAG,"HOME_4B boot complete - C6 untouched");
}
