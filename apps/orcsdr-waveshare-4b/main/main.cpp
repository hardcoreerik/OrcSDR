// OrcSDR Waveshare 4B: verified IQ driver unchanged, Home v3 LVGL presentation.
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
#include "home_v3_ui.hpp"
#include "screen_capture.hpp"
#include "sdkconfig.h"
#if CONFIG_ORCUI_ADAPTIVE_HOME
#include "orcui_4b_adapter.hpp"
#else
#include "fm_4b_ui.hpp"
#endif
#include "fm_4b_layout.hpp"
using namespace orcsdr::waveshare4b;
static constexpr char TAG[]="ORCSDR_4B_HOME";
static constexpr uint32_t kRate=960000;
static QueueHandle_t queue=nullptr;
static std::atomic<uint32_t> requested{kDefaultHz},applied{0},sps{0},drops{0},step_idx{2};
static std::atomic<int> state{0};
#if !CONFIG_ORCUI_ADAPTIVE_HOME
static std::atomic<bool> fm_tune_limit{false};
#endif
static portMUX_TYPE bands_lock=portMUX_INITIALIZER_UNLOCKED;
static uint8_t bands[kFftSize]{};
static uint32_t band_seq=0,rendered_seq=0;
static void ui_timer(lv_timer_t*) {
 uint8_t fft[kFftSize]{};
 bool fresh=false;
 portENTER_CRITICAL(&bands_lock);
 if(band_seq!=rendered_seq){
   memcpy(fft,bands,sizeof(fft));rendered_seq=band_seq;fresh=true;
 }
 portEXIT_CRITICAL(&bands_lock);
 const home_v3::Snapshot data{requested.load(),applied.load(),sps.load(),
   drops.load(),kSteps[step_idx.load()],uint32_t(esp_timer_get_time()/1000000),state.load()};
#if CONFIG_ORCUI_ADAPTIVE_HOME
 adaptive::update(data,fft,fresh);
#else
 if(fm::active())fm::update(data,fft,fresh);
 else home_v3::update(data,fft,fresh);
#endif
}
#if !CONFIG_ORCUI_ADAPTIVE_HOME
static void open_fm_preview() {
 // UI task owns view transition; radio task remains the only tuner owner.
 fm_tune_limit.store(true);
 fm::open();
 if(!fm::in_fm_band(requested.load())&&queue){
   const home_v3::Command cmd=home_v3::Command::FM;
   (void)xQueueSend(queue,&cmd,0);
 }
}
static void return_from_fm() {fm_tune_limit.store(false);fm::close();}
#endif
static bool home_start() {
 if(!bsp_display_lock(3000))return false;
#if CONFIG_ORCUI_ADAPTIVE_HOME
 const bool home_ok=adaptive::create(queue);
 const bool fm_ok=home_ok;
#else
 home_v3::set_fm_open_callback(open_fm_preview);
 const bool home_ok=home_v3::create(queue);
 const bool fm_ok=home_ok && fm::create(queue,return_from_fm);
#endif
 if(fm_ok)lv_timer_create(ui_timer,150,nullptr);
 bsp_display_unlock();
 return home_ok&&fm_ok;
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
    home_v3::Command cmd;
    while(xQueueReceive(queue,&cmd,0)==pdTRUE){
      if(cmd==home_v3::Command::Step){step_idx=(step_idx.load()+1)%6;continue;}
      uint32_t next=requested.load();
      switch(cmd){
        case home_v3::Command::Down:next=tune(next,-1,kSteps[step_idx.load()]);break;
        case home_v3::Command::Up:next=tune(next,1,kSteps[step_idx.load()]);break;
        case home_v3::Command::FM:next=96100000;break;
        case home_v3::Command::WX:next=162400000;break;
        case home_v3::Command::Air:next=125000000;break;
        case home_v3::Command::Ham:next=145120000;break;
        default:break;
      }
#if !CONFIG_ORCUI_ADAPTIVE_HOME
      if(fm_tune_limit.load())next=fm::clamp_fm_hz(next);
#endif
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
  queue=xQueueCreate(12,sizeof(home_v3::Command));
  if(!queue){ESP_LOGE(TAG,"Queue creation failed");return;}
  lv_display_t* disp=bsp_display_start();
  if(!disp||bsp_display_backlight_on()!=ESP_OK||!home_start()){
    ESP_LOGE(TAG,"Display or Home initialization failed");return;
  }
#if CONFIG_ORCUI_ADAPTIVE_HOME
  if(!screen_capture::start(adaptive::prepare_capture))ESP_LOGE(TAG,"Screenshot service failed");
#else
  if(!screen_capture::start())ESP_LOGE(TAG,"Screenshot service failed");
#endif
  // Hardware OTG VBUS requires physical validation; powered hub recommended.
  if(xTaskCreatePinnedToCore(radio_task,"4b_radio",8192,nullptr,5,nullptr,1)!=pdPASS){
    state=3;ESP_LOGE(TAG,"Receiver task creation failed");
  }
  ESP_LOGI(TAG,"HOME_4B boot complete - C6 untouched");
}
