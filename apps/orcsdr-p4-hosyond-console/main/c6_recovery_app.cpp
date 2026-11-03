#include "c6_recovery_app.hpp"
#include "c6_rom_package.hpp"
#include "wifi_app.hpp"
#include "c6_update_gate.hpp"
#include "orcsdr_board_profile.h"
#include "orcsdr_board_control.h"
#include "esp_loader.h"
#include "esp32_sdio_port.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <atomic>
#include <cstring>

namespace c6_recovery_app {
namespace {
std::atomic<bool> busy{false};
std::atomic<Phase> phase{Phase::idle};
std::atomic<uint8_t> percent{0};
std::atomic<int> result{0}, decision{0}; // 1 install, 2 probe-only release, 3 cancel
struct Context {orc_c6_rom_loader_session_t session{};};
void publish(Phase p,uint8_t n=0) {
  percent=n;phase=p;
  ESP_LOGI("C6_ROM","RTL_C6_ROM_PROGRESS phase=%s percent=%u",phase_name(p),unsigned(n));
}
int reset(void*) {
  esp_err_t e=orcsdr_board_c6_enable(false);
  if (e==ESP_OK) vTaskDelay(pdMS_TO_TICKS(20));
  const esp_err_t released=orcsdr_board_c6_enable(true);
  vTaskDelay(pdMS_TO_TICKS(20));
  return e==ESP_OK && released==ESP_OK?0:-1;
}
int connect(void* p,uint32_t* chip,uint32_t* size,bool* security) {
  return orc_c6_rom_loader_connect_callback(&static_cast<Context*>(p)->session,chip,size,security);
}
int write(void* p,uint32_t offset,const uint8_t* data,size_t size) {
  return orc_c6_rom_loader_write_callback(&static_cast<Context*>(p)->session,offset,data,size);
}
int read_hash(void* p,uint32_t offset,size_t size,uint8_t* digest) {
  return orc_c6_rom_loader_sha256_flash_callback(&static_cast<Context*>(p)->session,offset,size,digest);
}
void progress(void*,orc_c6_rom_phase_t p,uint8_t n) {
  if(p==ORC_C6_ROM_PHASE_CONNECTING) publish(Phase::connecting,n);
  else if(p==ORC_C6_ROM_PHASE_RELEASE_BOOT) publish(Phase::release_boot,n);
  else if(p==ORC_C6_ROM_PHASE_WRITING) publish(Phase::writing,n);
  else if(p==ORC_C6_ROM_PHASE_VERIFYING) publish(Phase::verifying,n);
}
bool timed_out(int64_t start,int seconds) {return esp_timer_get_time()-start>int64_t(seconds)*1000000;}
void task(void*) {
  int error=0;
  bool owns_access=false, wrote=false,boot_released=false;
  uint32_t prior_generation=0;
  Context context;
  esp_loader_t loader{};
  esp32_sdio_port_t port{};
  esp_loader_port_ops_t port_ops{};
  orc_c6_rom_update_t update=ORC_C6_ROM_UPDATE_INITIALIZER;
  const auto* profile=orcsdr_board_profile();
  const orc_c6_rom_board_t board={CONFIG_ESP_HOSTED_HOST_SDIO_SLOT,1000,
    profile->c6_sdio_clk,profile->c6_sdio_cmd,profile->c6_sdio_d0,profile->c6_sdio_d1,
    profile->c6_sdio_d2,profile->c6_sdio_d3,0x400000,nullptr,reset};
  orc_c6_rom_loader_session_init(&context.session,&loader,&port,&port_ops,&board);
  const orc_c6_rom_ops_t ops={&context,connect,orc_c6_rom_loader_sha256_callback,write,read_hash,progress};
  do {
    publish(Phase::quiescing);
    if(!wifi_app::request_recovery_access()) {error=-1;break;}
    owns_access=true;
    const auto start=esp_timer_get_time();
    while(wifi_app::recovery_access()==wifi_app::RecoveryAccess::requested && !timed_out(start,40) && decision!=3)
      vTaskDelay(pdMS_TO_TICKS(25));
    if(wifi_app::recovery_access()!=wifi_app::RecoveryAccess::ready || decision==3) {error=-2;break;}
    prior_generation=wifi_app::status().generation;
    const auto prepared=orc_c6_rom_prepare(&update,orcsdr_embedded_c6_rom_manifest(),&ops);
    if(prepared!=ORC_C6_ROM_OK) {error=100+int(prepared);break;}
    // No flash write can occur until this separate physical-release action.
    const auto connected_at=esp_timer_get_time();
    while(decision==0 && !timed_out(connected_at,120)) vTaskDelay(pdMS_TO_TICKS(25));
    if(decision!=1 && decision!=2) {error=-3;break;}
    boot_released=true;
    if(decision==1) {
      wrote=true;
      const auto flashed=orc_c6_rom_flash(&update);
      if(flashed!=ORC_C6_ROM_OK) {error=200+int(flashed);break;}
    }
  } while(false);
  orc_c6_rom_loader_disconnect(&context.session);
  if(owns_access) {
    if(!boot_released) {
      // BOOT is physical: a timeout/cancel cannot establish its release.
      // Keep ownership and wait for a separate release acknowledgment before
      // resetting or allowing Hosted to reset the coprocessor.
      result=error;publish(Phase::release_boot_failure);
      // Preserve a release acknowledgment that arrived at the timeout edge.
      // A late install acknowledgment permits cleanup, never a failed write.
      while(decision!=1 && decision!=2) vTaskDelay(pdMS_TO_TICKS(25));
    }
    publish(Phase::restarting);
    if(reset(nullptr)!=0) {
      ESP_LOGE("C6_ROM","cleanup reset failed; prior_result=%d",error);error=-4;
    }
    wifi_app::release_recovery_access();
    const auto restarting=esp_timer_get_time();
    while(wifi_app::recovery_access()!=wifi_app::RecoveryAccess::idle && !timed_out(restarting,40))
      vTaskDelay(pdMS_TO_TICKS(25));
    if(wifi_app::recovery_access()!=wifi_app::RecoveryAccess::idle) {
      if(!error) error=-5;
      result=error;publish(Phase::failed);
      // The owner task may still be releasing. Retain both guards until it
      // actually acknowledges idle; never start a second lifecycle operation.
      while(wifi_app::recovery_access()!=wifi_app::RecoveryAccess::idle) vTaskDelay(pdMS_TO_TICKS(100));
    }
    if(!error && wrote) {
      // Exact version confirmation is separate from flash readback success.
      const auto checking_version=esp_timer_get_time();
      bool verified=false;
      while(!timed_out(checking_version,45)) {
        const auto wifi=wifi_app::status();
        const char* target=orcsdr_embedded_c6_rom_version();
        if(wifi.started && wifi.generation!=prior_generation && target[0] && std::strcmp(wifi.c6_version,target)==0) {verified=true;break;}
        vTaskDelay(pdMS_TO_TICKS(100));
      }
      if(!verified) error=-6;
    }
  }
  result=error;publish(error?Phase::failed:Phase::complete,error?0:100);
  orcsdr::c6_update::release(orcsdr::c6_update::Owner::rom);
  busy=false;vTaskDelete(nullptr);
}
}
const char* phase_name(Phase p) {
  switch(p) {
    case Phase::idle:return "idle";case Phase::quiescing:return "quiescing";
    case Phase::connecting:return "connecting";case Phase::release_boot:return "release_boot";
    case Phase::writing:return "writing";case Phase::verifying:return "verifying";
    case Phase::restarting:return "restarting";case Phase::complete:return "complete";
    case Phase::release_boot_failure:return "release_boot_failure";
    default:return "failed";
  }
}
Status status() {return {phase.load(),percent.load(),result.load(),busy.load()};}
bool begin() {
  const auto* p=orcsdr_board_profile();
  // Qualified target only; other profiles keep their existing update method.
  if(!orcsdr_embedded_c6_rom_manifest() || !p->c6_rom_sdio_recovery) return false;
  const auto ota=orcsdr::wifi::c6_update_status();
  if(ota.state==orcsdr::wifi::C6UpdateState::updating || ota.state==orcsdr::wifi::C6UpdateState::rebooting) return false;
  bool expected=false;if(!busy.compare_exchange_strong(expected,true)) return false;
  if(!orcsdr::c6_update::claim(orcsdr::c6_update::Owner::rom)) {busy=false;return false;}
  decision=0;result=0;publish(Phase::quiescing);
  if(xTaskCreate(task,"c6_rom_update",12288,nullptr,2,nullptr)!=pdPASS) {
    result=-7;publish(Phase::failed);orcsdr::c6_update::release(orcsdr::c6_update::Owner::rom);busy=false;return false;
  }
  return true;
}
bool confirm_boot_released(bool install) {
  if(!busy || (phase!=Phase::release_boot && phase!=Phase::release_boot_failure)) return false;
  if(phase==Phase::release_boot_failure && install) return false;
  int expected=decision.load();
  do {
    // Failure cleanup accepts release even when a cancel raced the prompt.
    if(expected!=0 && !(expected==3 && phase==Phase::release_boot_failure && !install)) return false;
  } while(!decision.compare_exchange_weak(expected,install?1:2));
  return true;
}
bool cancel() {
  const auto p=phase.load();
  if(!busy || (p!=Phase::quiescing && p!=Phase::connecting && p!=Phase::release_boot)) return false;
  int expected=0;return decision.compare_exchange_strong(expected,3);
}
}
