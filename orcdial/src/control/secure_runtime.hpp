#pragma once
#include "secure_session.hpp"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <nvs.h>
#include <mbedtls/ctr_drbg.h>
#if CONFIG_IDF_TARGET_ESP32P4
#include <bootloader_random.h>
#endif
#include <atomic>

namespace orc::secure {
enum class Action : uint8_t { pair, cancel, confirm, connect, disconnect, forget, forget_repair, boot };
class Runtime {
 public:
  using Transmit=bool (*)(const uint8_t*,const uint8_t*);
  using Scan=void (*)(uint8_t);
  // P4 has no local Wi-Fi RF entropy source. Call this before M5/ADC/audio
  // initialization, seed a DRBG while SAR entropy is enabled, then release it.
  bool prepare_entropy() {
    if(entropy_ready_)return true;
    mbedtls_ctr_drbg_init(&drbg_);
#if CONFIG_IDF_TARGET_ESP32P4
    bootloader_random_enable();
#endif
    const unsigned char domain[]="OrcDial protocol 4";
    const int result=mbedtls_ctr_drbg_seed(&drbg_,hardware_entropy,nullptr,domain,sizeof(domain)-1);
#if CONFIG_IDF_TARGET_ESP32P4
    bootloader_random_disable();
#endif
    // Reseeding after startup cannot assume P4 SAR is still available. The
    // fresh boot seed supports vastly more output than this accessory uses.
    mbedtls_ctr_drbg_set_reseed_interval(&drbg_,0x7fffffff);
    entropy_ready_=result==0;
    if(!entropy_ready_)mbedtls_ctr_drbg_free(&drbg_);
    return entropy_ready_;
  }
  bool begin(uint8_t role,const uint8_t mac[6],bool legacy,Transmit transmit,Scan scan=nullptr) {
    if(task_)return true;
#if CONFIG_IDF_TARGET_ESP32P4
    if(!entropy_ready_)return initialization_failed(Failure::authentication);
#else
    if(!prepare_entropy())return initialization_failed(Failure::authentication); // S3 Wi-Fi is already enabled by Link.
#endif
    role_=role;transmit_=transmit;scan_=scan;std::memcpy(mac_,mac,6);
    input_=xQueueCreate(20,sizeof(Event));actions_=xQueueCreate(8,sizeof(Event));output_=xQueueCreate(8,sizeof(Control));
    if(!input_||!actions_||!output_)return initialization_failed(Failure::transport);
    if(nvs_open("orcdial4",NVS_READWRITE,&store_)!=ESP_OK)return initialization_failed(Failure::storage);
    Trust trust{};uint8_t record[76];size_t n=sizeof record;
    if(nvs_get_blob(store_,"trust",record,&n)==ESP_OK && n==sizeof record && !std::memcmp(record,"ODT4",4) && record[74]<=1 && record[75]<=1) {
      std::memcpy(trust.identity,record+4,16);std::memcpy(trust.peer_identity,record+20,16);
      std::memcpy(trust.peer_mac,record+36,6);std::memcpy(trust.secret,record+42,32);trust.trusted=record[74];trust.boot_connect=record[75];
    } else {
      size_t length=16;
      if(nvs_get_blob(store_,"identity",trust.identity,&length)!=ESP_OK || length!=16) {
        if(random(this,trust.identity,16))return initialization_failed(Failure::authentication);
        if(nvs_set_blob(store_,"identity",trust.identity,16)!=ESP_OK||nvs_commit(store_)!=ESP_OK)return initialization_failed(Failure::storage);
      }
      trust.upgrade=legacy;
    }
    initial_=trust;publish_initial(trust);
    if(xTaskCreate(run,"orcdial_secure",12288,this,1,&task_)!=pdPASS)
      return initialization_failed(Failure::transport);
    return true;
  }
  void enabled(bool on) {enabled_.store(on,std::memory_order_relaxed);}
  bool receive(const uint8_t* mac,const uint8_t* wire,size_t n) {
    if(!input_||n!=64 || !mac || !wire)return false;
    Event e{};e.kind=0;
    std::memcpy(e.mac,mac,6);std::memcpy(e.data,wire,64);return xQueueSend(input_,&e,0)==pdTRUE;
  }
  bool action(Action a,uint32_t value=0) {if(!actions_)return false;Event e{};e.kind=1;e.action=a;e.value=value;return xQueueSend(actions_,&e,0)==pdTRUE;}
  bool send(const uint8_t* p,size_t n) {
    if(!input_||n>128||!p)return false;
    Event e{};e.kind=2;e.size=uint8_t(n);std::memcpy(e.data,p,n);return xQueueSend(input_,&e,0)==pdTRUE;
  }
  struct Control {uint8_t size=0;uint8_t data[128]{};uint32_t generation=0;};
  bool take(Control& c) {while(output_&&xQueueReceive(output_,&c,0)==pdTRUE)if(c.generation==generation_.load(std::memory_order_acquire))return true;return false;}
  Status status() const {portENTER_CRITICAL(&lock_);Status s=status_;portEXIT_CRITICAL(&lock_);return s;}
 private:
  bool initialization_failed(Failure reason) {
    if(input_){vQueueDelete(input_);input_=nullptr;}
    if(actions_){vQueueDelete(actions_);actions_=nullptr;}
    if(output_){vQueueDelete(output_);output_=nullptr;}
    if(store_){nvs_close(store_);store_=0;}
    task_=nullptr;wipe(&initial_,sizeof initial_);
    portENTER_CRITICAL(&lock_);status_={};status_.state=State::failed;status_.failure=reason;portEXIT_CRITICAL(&lock_);
    return false;
  }
  struct Event {uint8_t kind=0,size=0,mac[6]{},data[128]{};Action action{};uint32_t value=0;};
  static uint32_t now(){return uint32_t(esp_timer_get_time()/1000);}
  static int hardware_entropy(void*,unsigned char* p,size_t n){esp_fill_random(p,n);return 0;}
  static int random(void* context,unsigned char* p,size_t n) {
    auto* r=static_cast<Runtime*>(context);
    return r->entropy_ready_?mbedtls_ctr_drbg_random(&r->drbg_,p,n):-1;
  }
  static bool save(void* ctx,const Trust& t) {
    auto* r=static_cast<Runtime*>(ctx);uint8_t p[76]{};std::memcpy(p,"ODT4",4);std::memcpy(p+4,t.identity,16);
    std::memcpy(p+20,t.peer_identity,16);std::memcpy(p+36,t.peer_mac,6);std::memcpy(p+42,t.secret,32);p[74]=t.trusted;p[75]=t.boot_connect;
    uint8_t check[76];size_t n=sizeof check;
    const bool ok=nvs_set_blob(r->store_,"trust",p,sizeof p)==ESP_OK && nvs_commit(r->store_)==ESP_OK &&
      nvs_get_blob(r->store_,"trust",check,&n)==ESP_OK && n==sizeof p && equal(p,check,n);
    wipe(p,sizeof p);wipe(check,sizeof check);return ok;
  }
  static bool transmit(void* ctx,const uint8_t* mac,const Message& m) {
    auto* r=static_cast<Runtime*>(ctx);uint8_t frame[64];
    for(uint8_t i=0;fragment(m,i,frame);++i) {
      if(!r->enabled_.load(std::memory_order_relaxed)||!r->transmit_(mac,frame))return false;
      vTaskDelay(pdMS_TO_TICKS(8)); // Pace only this worker, never callbacks or UI.
    }return true;
  }
  static void deliver(void* ctx,const uint8_t* p,size_t n) {
    auto* r=static_cast<Runtime*>(ctx);if(n>128)return;Control c{};c.size=uint8_t(n);
    c.generation=r->generation_.load(std::memory_order_relaxed);std::memcpy(c.data,p,n);xQueueSend(r->output_,&c,0);
  }
  void publish_initial(const Trust& t){portENTER_CRITICAL(&lock_);status_={};status_.trusted=t.trusted;status_.boot_connect=t.boot_connect;status_.upgrade=t.upgrade;std::memcpy(status_.identity,t.identity,16);std::memcpy(status_.peer_identity,t.peer_identity,16);portEXIT_CRITICAL(&lock_);}
  static void run(void* ctx) {static_cast<Runtime*>(ctx)->loop();}
  void loop() {
    // Constructor deferred until transport staging permits a fresh boot challenge.
    while(!enabled_.load(std::memory_order_relaxed))vTaskDelay(pdMS_TO_TICKS(20));
    Hooks hooks{this,random,transmit,save,deliver};Session session(role_,mac_,initial_,hooks);
    if(initial_.trusted && initial_.boot_connect)session.connect(false,now());
    wipe(&initial_,sizeof initial_);
    Reassembly assembly;uint8_t assembly_mac[6]{};uint32_t scan_at=0;uint8_t channel=1;State previous=State::offline;
    for(;;) {
      const uint32_t time=now();Event e{};
      assembly.expire(time);
      // Network input cannot fill or starve the local Disconnect/Forget queue.
      if(xQueueReceive(actions_,&e,0)==pdTRUE || xQueueReceive(input_,&e,pdMS_TO_TICKS(20))==pdTRUE) {
        if(e.kind==1) {
          switch(e.action){case Action::pair:session.pair(time);break;case Action::cancel:session.cancel();break;
            case Action::confirm:session.confirm(e.value,time);break;case Action::connect:session.connect(true,time);break;
            case Action::disconnect:session.disconnect(time);break;case Action::forget:session.forget(time);break;
            case Action::forget_repair:if(session.forget(time))session.pair(time);break;
            case Action::boot:session.boot(e.value!=0);break;}
          assembly.reset();
        }else if(enabled_.load(std::memory_order_relaxed)) {
          if(e.kind==2)session.send_control(e.data,e.size,time);
          else {
            if(!assembly.active())std::memcpy(assembly_mac,e.mac,6);
            if(equal(assembly_mac,e.mac,6)) {Message m{};if(assembly.push(e.data,64,time,m))session.receive(e.mac,m,time);}
          }
        }
      }
      if(enabled_.load(std::memory_order_relaxed))session.tick(time);
      const Status current=session.status();
      if(current.state!=previous && current.state!=State::connected){generation_.fetch_add(1,std::memory_order_release);xQueueReset(output_);}
      previous=current.state;
      portENTER_CRITICAL(&lock_);status_=current;portEXIT_CRITICAL(&lock_);
      const bool passive_listen=current.trusted && (current.state==State::offline || current.state==State::paused || current.state==State::failed);
      if(scan_ && !current.channel_locked && (passive_listen || current.state==State::searching || current.state==State::connecting) && uint32_t(time-scan_at)>350) {
        scan_(channel);channel=channel==11?1:channel+1;scan_at=time;
      }
    }
  }
  uint8_t role_=0,mac_[6]{};Transmit transmit_=nullptr;Scan scan_=nullptr;Trust initial_{};
  nvs_handle_t store_=0;QueueHandle_t input_=nullptr,actions_=nullptr,output_=nullptr;TaskHandle_t task_=nullptr;
  std::atomic<bool> enabled_{false};std::atomic<uint32_t> generation_{0};
  mutable portMUX_TYPE lock_=portMUX_INITIALIZER_UNLOCKED;Status status_{};
  mbedtls_ctr_drbg_context drbg_{};bool entropy_ready_=false;
};
} // namespace orc::secure
