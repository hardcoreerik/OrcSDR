#include "map_downloads.hpp"
#include "map_install.hpp"
#include "map_delivery_nvs.hpp"
#include "map_catalog_session.hpp"
#include "map_catalog_fetch.hpp"
#include "map_catalog_source.hpp"
#include "map_http_stream.hpp"
#include "receiver_location.hpp"
#include "time_service.hpp"
#include "wifi_service.hpp"
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>
#include <cstdio>
#include <memory>
#include <new>
#include <utility>

namespace orcsdr::maps::downloads {
namespace {
constexpr uint32_t kRecover=1,kCheck=2,kReset=3,kInstall=256;
EXT_RAM_BSS_ATTR Snapshot g_snapshot;
EXT_RAM_BSS_ATTR Entry g_entries[32];
EXT_RAM_BSS_ATTR catalog::Catalog g_catalog;
portMUX_TYPE g_lock=portMUX_INITIALIZER_UNLOCKED;
std::atomic<uint32_t> g_command{0},g_last_command{kRecover};
std::atomic<bool> g_busy{false},g_cancel{false};
std::atomic<bool> g_time_sync_requested{false};
uint64_t g_last_yield=0,g_last_progress=0;
void publish(Phase phase,bool pending,const char* message) {
  portENTER_CRITICAL(&g_lock);
  g_snapshot.phase=phase;g_snapshot.pending=pending;g_snapshot.busy=g_busy.load();
  std::snprintf(g_snapshot.message,sizeof(g_snapshot.message),"%s",message);
  ++g_snapshot.revision;
  portEXIT_CRITICAL(&g_lock);
}
void finish(Phase phase,bool pending,const char* message) {
  g_time_sync_requested.store(false);
  g_busy.store(false);publish(phase,pending,message);
}
bool cancelled(void*) {
  const auto now=static_cast<uint64_t>(esp_timer_get_time());
  if(now-g_last_yield>=3000){vTaskDelay(1);g_last_yield=now;}
  return g_cancel.load();
}
void progress(void*,uint64_t used,uint64_t total) {
  const auto now=static_cast<uint64_t>(esp_timer_get_time());
  if(used!=total&&now-g_last_progress<100000)return;
  g_last_progress=now;
  portENTER_CRITICAL(&g_lock);
  g_snapshot.completed_bytes=used;g_snapshot.total_bytes=total;++g_snapshot.revision;
  portEXIT_CRITICAL(&g_lock);
}
void verifying(void*){publish(Phase::verifying,true,"Checking saved map file and installing...");}
void verify_progress(void*,uint64_t used,uint64_t total) {
  const auto now=static_cast<uint64_t>(esp_timer_get_time());
  if(used&&used!=total&&now-g_last_progress<100000)return;
  g_last_progress=now;
  portENTER_CRITICAL(&g_lock);
  if(!used)++g_snapshot.verify_pass;
  g_snapshot.verified_bytes=used;g_snapshot.verify_total_bytes=total;
  ++g_snapshot.revision;
  portEXIT_CRITICAL(&g_lock);
}
void verify_profile(void*,const transfer::VerificationProfile& p) {
    std::printf("ORCMAPS_VERIFY_PROFILE check=structure bytes=%llu blocks=%llu read_us=%llu hash_us=%llu elapsed_us=%llu result=%u\n",
      static_cast<unsigned long long>(p.bytes),static_cast<unsigned long long>(p.blocks),
      static_cast<unsigned long long>(p.read_us),static_cast<unsigned long long>(p.hash_us),
      static_cast<unsigned long long>(p.elapsed_us),static_cast<unsigned>(p.result));
}
const transfer::Hooks kHooks{nullptr,cancelled,progress,verifying,verify_progress,verify_profile};
bool queue(uint32_t command,Phase phase,const char* message) {
  bool expected=false;
  if(!g_busy.compare_exchange_strong(expected,true))return false;
  g_cancel.store(false);
  portENTER_CRITICAL(&g_lock);
  g_snapshot.verified_bytes=g_snapshot.verify_total_bytes=0;g_snapshot.verify_pass=0;
  portEXIT_CRITICAL(&g_lock);
  Snapshot current;snapshot(&current);publish(phase,current.pending,message);
  g_command.store(command);return true;
}
void invalidate_catalog() {
  portENTER_CRITICAL(&g_lock);g_snapshot.catalog_ready=false;g_snapshot.count=0;
  ++g_snapshot.revision;portEXIT_CRITICAL(&g_lock);
}
bool catalog_entries(storage::FileSystem* fs,delivery::Store& store) {
  invalidate_catalog();
  delivery::State state;if(delivery::load(store,&state)!=delivery::Error::none)return false;
  const auto location=receiver_location::snapshot();
  const double lat=location.latitude_e7/1e7,lon=location.longitude_e7/1e7;
  const size_t count=g_catalog.packs.size()<32?g_catalog.packs.size():32;
  for(size_t i=0;i<count;++i) {
    const auto& pack=g_catalog.packs[i];const auto& m=pack.manifest;
    Entry e;
    std::snprintf(e.title,sizeof(e.title),"%s",m.display_name.c_str());
    std::snprintf(e.region,sizeof(e.region),"%s",m.region_name.c_str());
    std::snprintf(e.product,sizeof(e.product),"%s",pack.product_id.c_str());
    e.bytes=m.size_bytes;e.min_zoom=m.min_zoom;e.max_zoom=m.max_zoom;
    const auto& b=m.bounds;
    const bool longitude=b.min_lon_deg<=b.max_lon_deg?
        lon>=b.min_lon_deg&&lon<=b.max_lon_deg:lon>=b.min_lon_deg||lon<=b.max_lon_deg;
    e.recommended=m.region_id=="world"||(location.configured&&longitude&&lat>=b.min_lat_deg&&lat<=b.max_lat_deg);
    const auto target=activation::target_manifest(pack);
    for(const auto& owned:state.owned)if(owned.product==pack.product_id&&owned.manifest==target&&fs) {
      const auto archive=target.substr(0,target.size()-14)+".pmtiles";
      if(fs->status(target.c_str())==storage::EntryStatus::file&&fs->status(archive.c_str())==storage::EntryStatus::file) {
        auto file=fs->open(archive.c_str(),FILE_READ);
        e.installed=file&&file.size()==m.size_bytes;
        if(!file.close())e.installed=false;
      }
    }
    g_entries[i]=e;
  }
  portENTER_CRITICAL(&g_lock);
  g_snapshot.count=static_cast<uint8_t>(count);g_snapshot.catalog_ready=true;
  g_snapshot.pending=!state.pending.product.empty();++g_snapshot.revision;
  portEXIT_CRITICAL(&g_lock);return true;
}
bool ensure_catalog_clock() {
  if(time_service::now().wallclock_valid)return true;
  publish(Phase::checking,false,"Getting network time automatically...");
  g_time_sync_requested.store(true);
  const auto started=static_cast<uint64_t>(esp_timer_get_time());
  while(!time_service::now().wallclock_valid) {
    if(cancelled(nullptr)) {
      finish(Phase::cancelled,false,"Catalog check cancelled.");return false;
    }
    if(!wifi::connected()) {
      finish(Phase::failed,false,"Wi-Fi disconnected. Reconnect and Retry to check maps.");return false;
    }
    if(static_cast<uint64_t>(esp_timer_get_time())-started>=25000000) {
      finish(Phase::failed,false,"Network time unavailable. Check internet access and Retry.");return false;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  g_time_sync_requested.store(false);
  publish(Phase::checking,false,"Checking signed map catalog...");
  return true;
}
void check(storage::FileSystem* fs,delivery::Store& store,uint8_t* buffer) {
  invalidate_catalog();
  if(!catalog_source::base_url[0]||!catalog_source::public_key[0]) {
    finish(Phase::unconfigured,false,"Downloads not configured. Installed maps remain available.");return;
  }
  if(!wifi::connected()){finish(Phase::failed,false,"Connect Wi-Fi to check available maps.");return;}
  if(!ensure_catalog_clock())return;
  std::string base=catalog_source::base_url;
  while(!base.empty()&&base.back()=='/')base.pop_back();
  transfer::HttpStream stream;std::string payload,signature;
  auto fetched=catalog::fetch(stream,base+"/maps-catalog-v1.json",128*1024,buffer,16384,kHooks,&payload);
  if(fetched==catalog::FetchError::none)
    fetched=catalog::fetch(stream,base+"/maps-catalog-v1.sig",128,buffer,16384,kHooks,&signature);
  if(fetched!=catalog::FetchError::none) {
    finish(fetched==catalog::FetchError::cancelled?Phase::cancelled:Phase::failed,false,
           "Catalog check stopped or failed. Installed maps retained.");
    std::printf("ORCMAPS_CATALOG fetch_error=%u\n",static_cast<unsigned>(fetched));return;
  }
  if(cancelled(nullptr)){finish(Phase::cancelled,false,"Catalog check cancelled.");return;}
  const auto clock=time_service::now();catalog::Catalog next;
  const auto accepted=delivery::accept_catalog(store,payload,signature,catalog_source::public_key,
      clock.utc,clock.wallclock_valid,&next);
  if(!accepted.accepted()) {
    finish(Phase::failed,false,"Catalog verification failed. Installed maps retained.");
    std::printf("ORCMAPS_CATALOG trust_error=%u catalog_error=%s\n",
        static_cast<unsigned>(accepted.storage),catalog::error_name(accepted.catalog));return;
  }
  g_catalog=std::move(next);
  if(!catalog_entries(fs,store)){finish(Phase::failed,true,"Map state unreadable; files retained.");return;}
  finish(Phase::ready,false,"Select a map to download. Existing maps are retained.");
}
void result(const install::Result& outcome,delivery::Store& store,bool recovering) {
  delivery::State state;const auto loaded=delivery::load(store,&state);
  const bool pending=loaded!=delivery::Error::none||!state.pending.product.empty();
  if(outcome.error==install::Error::none&&loaded==delivery::Error::none) {
    finish(recovering?Phase::idle:Phase::complete,pending,
        recovering?"":"Map installed. Return to Installed to use it.");return;
  }
  const bool stopped=outcome.transfer==transfer::Error::cancelled||outcome.activation==activation::Error::cancelled;
  finish(stopped?Phase::cancelled:Phase::failed,pending,
      stopped?"Transfer paused. Retry to resume; existing maps retained.":
      outcome.transfer==transfer::Error::space?"Not enough SD space. Existing maps retained.":
      "Map installation failed. Existing files retained; retry when ready.");
  std::printf("ORCMAPS_INSTALL error=%u state=%u transfer=%u activation=%u\n",
      static_cast<unsigned>(outcome.error),static_cast<unsigned>(outcome.state),
      static_cast<unsigned>(outcome.transfer),static_cast<unsigned>(outcome.activation));
}
}
void begin() {
  g_busy.store(true);g_cancel.store(false);
  portENTER_CRITICAL(&g_lock);
  g_snapshot.configured=catalog_source::base_url[0]&&catalog_source::public_key[0];
  portEXIT_CRITICAL(&g_lock);
  publish(Phase::idle,false,"");g_command.store(kRecover);
}
bool snapshot(Snapshot* output) {
  if(!output)return false;
  const bool connected=wifi::connected();
  portENTER_CRITICAL(&g_lock);
  if(g_snapshot.connected!=connected||g_snapshot.busy!=g_busy.load())++g_snapshot.revision;
  g_snapshot.connected=connected;g_snapshot.busy=g_busy.load();*output=g_snapshot;
  portEXIT_CRITICAL(&g_lock);return true;
}
bool entry(size_t index,Entry* output) {
  if(!output)return false;
  portENTER_CRITICAL(&g_lock);
  const bool valid=g_snapshot.catalog_ready&&index<g_snapshot.count;
  if(valid)*output=g_entries[index];
  portEXIT_CRITICAL(&g_lock);return valid;
}
bool check_catalog() {
  Snapshot s;snapshot(&s);return can_check(s)&&queue(kCheck,Phase::checking,"Checking signed map catalog...");
}
bool request_install(size_t index) {
  Snapshot s;snapshot(&s);
  return can_install(s,index)&&
      queue(kInstall+static_cast<uint32_t>(index),Phase::downloading,"Preparing map download...");
}
bool retry() {
  Snapshot s;snapshot(&s);
  if(!can_retry(s))return false;
  const auto command=s.pending?kRecover:g_last_command.load();
  return queue(command,command==kCheck?Phase::checking:Phase::verifying,"Retrying saved map operation...");
}
bool retry_recovery() {
  Snapshot s;snapshot(&s);return s.pending&&queue(kRecover,Phase::verifying,"Recovering saved map installation...");
}
bool reset_pending() {
  Snapshot s;snapshot(&s);
  return can_reset(s)&&queue(kReset,Phase::verifying,"Clearing the saved install...");
}
void cancel(){g_cancel.store(true);}
bool take_time_sync_request(){return g_time_sync_requested.exchange(false);}
bool work_pending(){return g_command.load(std::memory_order_acquire)!=0;}
void worker_unavailable() {
  const auto command=g_command.exchange(0);if(!command)return;
  g_last_command.store(command);
  Snapshot current;snapshot(&current);
  finish(Phase::failed,current.pending,"Not enough internal memory. Stop reception or free memory, then Retry.");
}
bool service(storage::FileSystem* fs) {
  const auto command=g_command.exchange(0);if(!command)return false;
  g_last_command.store(command);g_busy.store(true);
  portENTER_CRITICAL(&g_lock);g_snapshot.sd_available=fs!=nullptr;++g_snapshot.revision;portEXIT_CRITICAL(&g_lock);
  try {
    delivery::NvsStateStore store;delivery::State state;
    if(delivery::load(store,&state)!=delivery::Error::none) {
      finish(Phase::failed,true,"Map state unreadable; files retained.");return true;
    }
    if(command==kReset) {
      // Escape hatch for any stuck install: forget only the saved intent. Map
      // files on the SD card and installed ownership are left untouched.
      state.pending={};
      if(delivery::save(store,state)==delivery::Error::none) {
        invalidate_catalog();
        finish(Phase::idle,false,"Saved install cleared. Check the catalog to download again.");
      } else finish(Phase::failed,true,"Could not clear the saved install. Try again.");
      return true;
    }
    const bool pending=!state.pending.product.empty();
    if(command==kRecover&&!fs) {
      finish(pending?Phase::failed:Phase::idle,pending,
          pending?"Insert original SD card and restart to finish installation.":"");return true;
    }
    std::unique_ptr<uint8_t,decltype(&heap_caps_free)> buffer(nullptr,heap_caps_free);
    if(command!=kRecover||pending) {
      // Match the shared SD path's cache-aligned PSRAM buffering contract.
      buffer.reset(static_cast<uint8_t*>(heap_caps_malloc(16384,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT|MALLOC_CAP_CACHE_ALIGNED)));
      if(!buffer){finish(Phase::failed,pending,"Not enough map memory; saved files retained.");return true;}
    }
    g_last_yield=static_cast<uint64_t>(esp_timer_get_time());g_last_progress=0;
    if(command==kCheck){check(fs,store,buffer.get());return true;}
    if(command==kRecover) {
      if(pending)publish(Phase::verifying,true,"Verifying saved map installation...");
      const auto outcome=install::resume(*fs,store,buffer.get(),pending?16384:0,kHooks);
      if(!g_catalog.packs.empty())(void)catalog_entries(fs,store);
      result(outcome,store,true);return true;
    }
    const size_t index=command-kInstall;
    if(command<kInstall||index>=g_catalog.packs.size()||!fs) {
      finish(Phase::failed,pending,"Insert an SD card and check the catalog before downloading.");return true;
    }
    portENTER_CRITICAL(&g_lock);
    g_snapshot.completed_bytes=0;g_snapshot.total_bytes=g_catalog.packs[index].manifest.size_bytes;
    portEXIT_CRITICAL(&g_lock);
    publish(Phase::downloading,pending,"Downloading map to SD. You can stop and resume.");
    const auto available=storage::free_bytes();
    const auto clock=time_service::now();transfer::HttpStream stream;install::Result outcome;
    for(size_t mirror=0;mirror<g_catalog.packs[index].urls.size();++mirror) {
      outcome=install::run(*fs,store,g_catalog,index,mirror,stream,available,
          buffer.get(),16384,false,clock.wallclock_valid?clock.utc:0,kHooks);
      if(g_cancel.load()||outcome.error!=install::Error::transfer||
          (outcome.transfer!=transfer::Error::network&&outcome.transfer!=transfer::Error::response))break;
    }
    (void)catalog_entries(fs,store);result(outcome,store,false);
  } catch(const std::bad_alloc&) {
    finish(Phase::failed,true,"Map memory exhausted. Saved files retained; retry recovery.");
  }
  return true;
}
}
