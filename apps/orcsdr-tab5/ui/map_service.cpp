#include "map_service.hpp"
#include "map_timing.hpp"
#include "map_preview.hpp"
#include "map_inventory.hpp"
#include "map_activation.hpp"
#include "map_downloads.hpp"
#include "map_preferences.hpp"
#include "memory_reclaim.hpp"
#include "orcmap/esp_idf/file_byte_source.hpp"
#include "orcmap/esp_idf/pack_filesystem.hpp"
#include "orcmap/esp_idf/partition_byte_source.hpp"
#include "orcmap/m5gfx/display_target.hpp"
#include "orcmap/m5gfx/label_target.hpp"
#include "orcmap/frame_transform.hpp"
#include <M5Unified.h>
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <memory>
#include <new>
#include <iterator>
#include <cerrno>

namespace orcsdr::maps {
namespace {
constexpr char kDirectory[] = "/sd/orcmaps";
constexpr char kWorld[] = "orcmaps_world";
struct Request { orcmap::Viewport view; char preferred[256]{}; uint32_t serial = 0,lifetime = 0; uint64_t requested_us=0; bool labels=true; };
QueueHandle_t g_queue = nullptr;
SemaphoreHandle_t g_lock = nullptr;
SemaphoreHandle_t g_preview_lock = nullptr;
TaskHandle_t g_task = nullptr, g_download_task = nullptr;
std::atomic<bool> g_download_finished{false};
SemaphoreHandle_t g_discovery_lock = nullptr;
EXT_RAM_BSS_ATTR MapSnapshot g_snapshot;
EXT_RAM_BSS_ATTR FrameInfo g_frame;
EXT_RAM_BSS_ATTR Request g_requested, g_ready;
EXT_RAM_BSS_ATTR Inventory g_inventory;
// This object and path are worker/UI state, never ISR or DMA descriptors.
// Keep them out of the on-chip heap boundary: ESP-IDF reserves the existing
// 40 KiB DMA pool before app_main, before this service can handle failures.
#ifdef ORCSDR_P4_GFX_COMPAT
// P4 canvases push into an explicit native target. Leaving these sprites
// unbound avoids coupling them to the compatibility wrapper type.
EXT_RAM_BSS_ATTR M5Canvas g_canvas;
EXT_RAM_BSS_ATTR M5Canvas g_preview;
#else
EXT_RAM_BSS_ATTR M5Canvas g_canvas(&M5.Display);
EXT_RAM_BSS_ATTR M5Canvas g_preview(&M5.Display);
#endif
EXT_RAM_BSS_ATTR Request g_preview_request;
EXT_RAM_BSS_ATTR FrameInfo g_preview_info;
bool g_preview_valid=false;
std::atomic<uint32_t> g_preview_lifetime{0};
std::atomic<size_t> g_preview_bytes{0};
EXT_RAM_BSS_ATTR orcmap::LabelLayout g_labels;
EXT_RAM_BSS_ATTR UpdateTiming g_update_timing;
// Ten bounded 256-sample rings, under 21 KiB including metadata; stage rings
// add about 8 KiB of PSRAM and no internal/DMA allocation.
EXT_RAM_BSS_ATTR PreviewTiming g_preview_pan_timing,g_preview_zoom_timing;
struct PreviewFacts {
  bool valid=false,display_target=false,copy_bbox_exact=false;
  uint32_t request=0,source_depth=0,target_depth=0;
  int rotation=0,offset=0,derived_rotation=0,physical_w=0,physical_h=0;
  int logical_w=0,logical_h=0,source_w=0,source_h=0;
  int32_t clip_x=0,clip_y=0,clip_w=0,clip_h=0;
  uint64_t clipped_map_pixels=0,copy_bbox_pixels=0;
};
// Last observed facts per transform bucket, protected by g_preview_lock.
EXT_RAM_BSS_ATTR PreviewFacts g_preview_facts[2];
static_assert(sizeof(g_preview_facts)<=256,"Bound preview diagnostics independently of timing rings");
std::atomic<uint32_t> g_preview_lock_misses{0};
std::unique_ptr<orcmap::ByteSource> g_source;
std::unique_ptr<Pipeline> g_pipeline;
EXT_RAM_BSS_ATTR char g_archive[256]{};
bool g_sd = false, g_cached = false;
EXT_RAM_BSS_ATTR storage::FileSystem* g_storage = nullptr;
std::atomic<bool> g_scan{true};
std::atomic<bool> g_installed{false};
std::atomic<uint32_t> g_serial{0}, g_revision{0};
std::atomic<size_t> g_canvas_bytes{0};
uint64_t g_yield_us = 0;
EXT_RAM_BSS_ATTR uint64_t g_worker_wait_us;
EXT_RAM_BSS_ATTR uint32_t g_worker_yields;
struct FreeReadBuffer { void operator()(uint8_t* p) const {heap_caps_free(p);} };
EXT_RAM_BSS_ATTR std::unique_ptr<uint8_t,FreeReadBuffer> g_read_buffer;
EXT_RAM_BSS_ATTR ReadCache g_read_cache;
std::atomic<size_t> g_read_cache_bytes{0};
constexpr size_t kReplayCapacity = 768 * 1024;
EXT_RAM_BSS_ATTR FeatureCache g_feature_cache;
EXT_RAM_BSS_ATTR std::unique_ptr<uint8_t,FreeReadBuffer> g_replay_buffer;
std::atomic<size_t> g_replay_bytes{0};
void reset_replay() {
  g_feature_cache.Reset();g_replay_buffer.reset();g_replay_bytes.store(0);
}
void reset_source() {
  reset_replay();
  g_pipeline.reset();g_read_cache.Reset();g_read_buffer.reset();
  g_read_cache_bytes.store(0);g_source.reset();g_archive[0]=0;
}
struct WorkerLock {
  WorkerLock() {xSemaphoreTake(g_lock,portMAX_DELAY);}
  ~WorkerLock() {xSemaphoreGive(g_lock);}
};

// Serialize catalog discovery/recovery against activation, while ordinary map
// reads continue through FatFs' per-volume locking. Installed archives are
// immutable; activation retires metadata without deleting the previous archive.
struct DiscoveryLock {
  bool held;
  explicit DiscoveryLock(TickType_t timeout):held(xSemaphoreTake(g_discovery_lock,timeout)==pdTRUE){}
  ~DiscoveryLock(){if(held)xSemaphoreGive(g_discovery_lock);}
};

template<size_t N> void text(char (&out)[N], const char* value) {
  std::snprintf(out,N,"%s",value ? value : "");
  for(char* p=out;*p;++p)if(static_cast<unsigned char>(*p)<32)*p=' ';
}
bool same(const Request& a,const Request& b) {
  return a.labels==b.labels && a.view.center_lat_deg==b.view.center_lat_deg && a.view.center_lon_deg==b.view.center_lon_deg &&
      a.view.zoom==b.view.zoom && a.view.tile_size_px==b.view.tile_size_px &&
      a.view.width_px==b.view.width_px && a.view.height_px==b.view.height_px &&
      std::strcmp(a.preferred,b.preferred)==0;
}
// Worker holds g_lock; the UI only takes this separate publication lock.
void publish_preview(const Request& request) {
  if(!g_preview_lock)return;
  xSemaphoreTake(g_preview_lock,portMAX_DELAY);
  g_preview_valid=false;
  if(g_preview.width()!=request.view.width_px||g_preview.height()!=request.view.height_px) {
    g_preview.deleteSprite();g_preview_bytes.store(0);
    const size_t bytes=static_cast<size_t>(request.view.width_px)*request.view.height_px*2;
    if(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)>=bytes+128*1024) {
      g_preview.setPsram(true);
      // Convert once while publishing; Tab5 scanout uses native-order RGB565.
      g_preview.setColorDepth(lgfx::rgb565_nonswapped);
      if(g_preview.createSprite(request.view.width_px,request.view.height_px))g_preview_bytes.store(bytes);
    }
  }
  if(g_preview_bytes.load()) {
    g_canvas.pushSprite(&g_preview,0,0);
    g_preview_request=request;g_preview_info=g_frame;g_preview_valid=true;
  }
  xSemaphoreGive(g_preview_lock);
}
void capture_preview_facts(const Request& request,lgfx::v1::LovyanGFX& target,
    const orcmap::FrameTransform& transform,int x,int y) {
  auto& facts=g_preview_facts[transform.scale==1?0:1];facts=PreviewFacts{};
  facts.valid=true;facts.request=request.serial;
  facts.source_depth=static_cast<uint32_t>(g_preview.getColorDepth());
  facts.target_depth=static_cast<uint32_t>(target.getColorDepth());
  facts.logical_w=target.width();facts.logical_h=target.height();
  facts.source_w=g_preview.width();facts.source_h=g_preview.height();
  target.getClipRect(&facts.clip_x,&facts.clip_y,&facts.clip_w,&facts.clip_h);
  // The P4 M5.Display is a compatibility wrapper whose target is its native
  // sprite. Inspect that native target; never reinterpret the wrapper itself
  // as a LovyanGFX/Panel_Device object.
#ifdef ORCSDR_P4_GFX_COMPAT
  const auto* display_target = static_cast<const lgfx::v1::LovyanGFX*>(M5.Display.target());
  if (&target == display_target) {
    facts.display_target=true;facts.rotation=target.getRotation();
    facts.physical_w=target.width();facts.physical_h=target.height();
    facts.derived_rotation=facts.rotation;
  }
#else
  // getPanel() is a Panel_Device API on the real Tab5 display. Do not
  // reinterpret an arbitrary sprite/custom target as the physical display.
  if(&target==&M5.Display) {
    const auto* panel=M5.Display.getPanel();
    if(panel) {
      facts.display_target=true;facts.rotation=panel->getRotation();
      const auto& config=panel->config();facts.offset=config.offset_rotation;
      facts.physical_w=config.panel_width;facts.physical_h=config.panel_height;
      // Exact formula in the checked Panel_FrameBufferBase::setRotation.
      // This is explicitly derived, not a read of its protected state.
      facts.derived_rotation=((facts.rotation+facts.offset)&3)|((facts.rotation&4)^(facts.offset&4));
    }
  }
#endif
  const int64_t left=std::max<int64_t>(x,facts.clip_x),top=std::max<int64_t>(y,facts.clip_y);
  const int64_t right=std::min<int64_t>(int64_t{x}+request.view.width_px,int64_t{facts.clip_x}+facts.clip_w);
  const int64_t bottom=std::min<int64_t>(int64_t{y}+request.view.height_px,int64_t{facts.clip_y}+facts.clip_h);
  facts.clipped_map_pixels=static_cast<uint64_t>(std::max<int64_t>(0,right-left))*std::max<int64_t>(0,bottom-top);
  facts.copy_bbox_exact=transform.scale==1;
  const int64_t image_left=int64_t{x}+(facts.copy_bbox_exact?std::lround(transform.dx):static_cast<int64_t>(std::floor(transform.dx)));
  const int64_t image_top=int64_t{y}+(facts.copy_bbox_exact?std::lround(transform.dy):static_cast<int64_t>(std::floor(transform.dy)));
  const int64_t image_right=facts.copy_bbox_exact?image_left+facts.source_w:
      int64_t{x}+static_cast<int64_t>(std::ceil(transform.dx+facts.source_w*transform.scale));
  const int64_t image_bottom=facts.copy_bbox_exact?image_top+facts.source_h:
      int64_t{y}+static_cast<int64_t>(std::ceil(transform.dy+facts.source_h*transform.scale));
  facts.copy_bbox_pixels=static_cast<uint64_t>(std::max<int64_t>(0,std::min(right,image_right)-std::max(left,image_left)))*
      std::max<int64_t>(0,std::min(bottom,image_bottom)-std::max(top,image_top));
}
void notify_native_display_dirty(lgfx::v1::LovyanGFX& target,int x,int y,int w,int h,
    int32_t clip_x,int32_t clip_y,int32_t clip_w,int32_t clip_h) {
#ifdef ORCSDR_P4_GFX_COMPAT
  const auto* display_target = static_cast<const lgfx::v1::LovyanGFX*>(M5.Display.target());
  if (&target != display_target || w<=0 || h<=0 || clip_w<=0 || clip_h<=0) return;
  const int64_t left=std::max<int64_t>({x,clip_x,0});
  const int64_t top=std::max<int64_t>({y,clip_y,0});
  const int64_t right=std::min<int64_t>({int64_t{x}+w,int64_t{clip_x}+clip_w,target.width()});
  const int64_t bottom=std::min<int64_t>({int64_t{y}+h,int64_t{clip_y}+clip_h,target.height()});
  if(right>left&&bottom>top)
    M5.Display.mark_dirty_target(static_cast<int>(left),static_cast<int>(top),
                                 static_cast<int>(right),static_cast<int>(bottom));
#else
  (void)target;(void)x;(void)y;(void)w;(void)h;
  (void)clip_x;(void)clip_y;(void)clip_w;(void)clip_h;
#endif
}
void draw_preview(const Request& request,lgfx::v1::LovyanGFX& target,int x,int y,FrameInfo* info) {
  if(!g_preview_lock)return;
  if(xSemaphoreTake(g_preview_lock,0)!=pdTRUE){g_preview_lock_misses.fetch_add(1);return;}
  orcmap::FrameTransform transform;
  const auto& source=g_preview_request;
  if(prepare_preview(source.view,request.view,source.preferred,request.preferred,
      source.lifetime,g_preview_lifetime.load(),g_preview_valid,&transform,source.labels,request.labels)) {
    int32_t clip_x,clip_y,clip_w,clip_h;
    target.getClipRect(&clip_x,&clip_y,&clip_w,&clip_h);
    const auto started=static_cast<uint64_t>(esp_timer_get_time());
    const auto paint=paint_preview(g_preview,target,transform,x,y,request.view.width_px,request.view.height_px,
        +[]()->uint64_t{return static_cast<uint64_t>(esp_timer_get_time());});
    auto& timing=transform.scale==1?g_preview_pan_timing:g_preview_zoom_timing;
    if(paint.painted)timing.present(request.serial,request.requested_us,started,
        static_cast<uint64_t>(esp_timer_get_time()),paint.clear_us,paint.transfer_us);
    if(paint.painted) {
      notify_native_display_dirty(target,x,y,request.view.width_px,request.view.height_px,
                                  clip_x,clip_y,clip_w,clip_h);
      capture_preview_facts(request,target,transform,x,y);
      if(info){*info=g_preview_info;info->state=Frame::pending;}
    }
  }
  xSemaphoreGive(g_preview_lock);
}
uint64_t clock_us() { return static_cast<uint64_t>(esp_timer_get_time()); }
bool service(void* context) {
  if(*static_cast<uint32_t*>(context)!=g_serial.load())return false;
  const uint64_t now=clock_us();
  if(now-g_yield_us>=3000) {
    vTaskDelay(1);
    g_yield_us=clock_us();
    g_worker_wait_us+=g_yield_us-now;
    ++g_worker_yields;
  }
  return true;
}
// Bound the directory scan before allocating names, rather than collecting
// an arbitrarily large card directory and imposing a limit afterward.
class Files final : public orcmap::PackFileSystem {
 public:
  bool ListManifestNames(const std::string& directory,std::vector<std::string>* out) override {
    out->clear();
    const std::unique_ptr<DIR,decltype(&closedir)> dir(opendir(directory.c_str()),closedir);
    if(!dir)return false;
    while(const auto* entry=readdir(dir.get())) {
      const size_t n=std::strlen(entry->d_name);
      if(n<14 || std::strcmp(entry->d_name+n-14,".manifest.json"))continue;
      out->emplace_back(entry->d_name);
      if(out->size()>Inventory::kPackLimit)break;
    }
    return true;
  }
  bool ReadTextFile(const std::string& path,size_t maximum,std::string* out) override {
    if(!out)return false;
    out->clear();
    const std::unique_ptr<FILE,decltype(&std::fclose)> file(std::fopen(path.c_str(),"rb"),std::fclose);
    if(!file)return false;
    std::string text;char buffer[512];
    while(const size_t bytes=std::fread(buffer,1,sizeof(buffer),file.get())) {
      if(bytes>maximum-text.size())return false;
      text.append(buffer,bytes);
    }
    if(std::ferror(file.get()))return false;
    *out=std::move(text);return true;
  }
  bool FileSize(const std::string& path,uint64_t* bytes) override { return files_.FileSize(path,bytes); }
 private:
  orcmap::esp_idf::PackFileSystem files_;
};

activation::Error recover_before_discovery() {
  if(!g_storage)return activation::Error::none;
  const auto root=g_storage->status("/orcmaps");
  if(root==storage::EntryStatus::missing)return activation::Error::none;
  if(root!=storage::EntryStatus::directory)return activation::Error::storage;
  try {
    const std::unique_ptr<DIR,decltype(&closedir)> dir(opendir(kDirectory),closedir);
    if(!dir)return activation::Error::storage;
    size_t entries=0,jobs=0;
    while(true) {
      errno=0;const auto* entry=readdir(dir.get());
      if(!entry)return errno?activation::Error::storage:activation::Error::none;
      if(++entries>4096)return activation::Error::storage;
      const auto path=std::string("/orcmaps/")+entry->d_name;
      if(!activation::valid_directory(path))continue;
      if(g_storage->status(path.c_str())!=storage::EntryStatus::directory)
        return activation::Error::storage;
      const auto pending=g_storage->status((path+"/activation.pending").c_str());
      if(pending==storage::EntryStatus::missing)continue;
      if(pending!=storage::EntryStatus::file||++jobs>64)return activation::Error::storage;
      const auto result=activation::recover(*g_storage,path);
      if(result!=activation::Error::none)return result;
      vTaskDelay(1);
    }
  }catch(const std::bad_alloc&){return activation::Error::allocation;}
}

void discover() {
  Files files;
  // Never expose partially activated metadata before its journal is recovered.
  const auto recovery=recover_before_discovery();
  (void)g_inventory.scan(files,kDirectory,g_sd&&recovery==activation::Error::none);
  static EXT_RAM_BSS_ATTR MapSnapshot next;
  // Value-initialise in place; a temporary snapshot would exceed the worker stack.
  next.~MapSnapshot();new(&next) MapSnapshot();
  next.loaded=true;next.sd=g_sd;next.overflow=g_inventory.limit_exceeded();
  next.out_of_memory=g_inventory.out_of_memory();
  if(recovery!=activation::Error::none) {
    auto& item=next.packs[next.count++];item.kind=Kind::rejected;
    text(item.title,"SD map recovery");text(item.status,"Recovery needs attention; files retained");
    std::printf("ORCMAPS_RECOVERY error=%u; SD PMTiles discovery withheld\n",static_cast<unsigned>(recovery));
  }
  auto& world=next.packs[next.count++];
  world.kind=Kind::firmware;text(world.identity,"firmware-world");text(world.title,"Firmware world overview");
  text(world.region,"World; overview detail only");text(world.credit,"Made with Natural Earth");
  text(world.coverage,"World (Mercator); polar caps excluded");
  text(world.provenance,"Natural Earth 5.1.2; public domain");
  orcmap::esp_idf::PartitionByteSource source(kWorld);
  if(source.Valid()) {
    Pipeline probe(source);
    world.usable=probe.open("orcmaps-overview-1");
    if(world.usable) {world.min_zoom=probe.header().min_zoom;world.max_zoom=probe.header().max_zoom;}
    world.bytes=source.Size();
  }
  next.world=world.usable;text(world.status,world.usable ? "FIRMWARE; HASH UNCHECKED" : "NOT PROVISIONED / UNREADABLE");
  if(g_sd) {
    auto& legacy=next.packs[next.count++];
    legacy.kind=Kind::legacy;text(legacy.identity,"lane_county_map");text(legacy.title,"Older map file (unsupported)");
    text(legacy.credit,"OpenStreetMap contributors, ODbL");
    const bool exists=files.FileSize("/sd/orcsdr/data/lane_county_map.idx",&legacy.bytes);
    next.legacy=exists;
    text(legacy.region,"Install a PMTiles map to replace this format");
    text(legacy.status,exists ? "UNSUPPORTED FORMAT; FILE PRESERVED" : "NOT INSTALLED");
    if(!exists)--next.count;
  }
  for(const auto& pack:g_inventory.catalog().Packs()) {
    auto& item=next.packs[next.count++];item.kind=Kind::pmtiles;
    const bool identity_fits=pack.pack_id.size()<sizeof(item.identity);
    const bool path_fits=pack.archive_path.size()<sizeof(g_archive);
    text(item.identity,pack.pack_id.c_str());text(item.title,pack.display_name.c_str());
    text(item.region,pack.region_name.c_str());text(item.version,pack.pack_version.c_str());
    std::snprintf(item.coverage,sizeof(item.coverage),"S %.5f W %.5f N %.5f E %.5f",
        pack.bounds.min_lat_deg,pack.bounds.min_lon_deg,pack.bounds.max_lat_deg,pack.bounds.max_lon_deg);
    std::snprintf(item.provenance,sizeof(item.provenance),"%s %s; source %s; built %s",
        pack.builder.c_str(),pack.builder_version.c_str(),pack.source_snapshot.c_str(),pack.build_date.c_str());
    item.bytes=pack.size_bytes;item.min_zoom=pack.min_zoom;item.max_zoom=pack.max_zoom;
    std::string credit;
    for(const auto& attribution:pack.attribution)if(attribution.required) {
      if(!credit.empty())credit+="; ";
      credit+=attribution.text;
    }
    item.usable=identity_fits && path_fits && credit.size()<sizeof(item.credit);
    item.size_checked=true;text(item.credit,credit.c_str());
    text(item.status,item.usable ? "SIZE CHECKED; HASH UNCHECKED" : "IDENTITY / CREDIT TOO LONG");
  }
  for(const auto& rejected:g_inventory.report().rejected) {
    if(next.count>=std::size(next.packs))break;
    auto& item=next.packs[next.count++];item.kind=Kind::rejected;
    text(item.title,rejected.manifest_name.c_str());text(item.status,orcmap::PackRejectionName(rejected.rejection));
  }
  xSemaphoreTake(g_lock,portMAX_DELAY);
  bool available=false;
  for(size_t i=0;i<next.count;++i)available|=next.packs[i].usable;
  g_installed.store(available);
  next.revision=g_snapshot.revision+1;g_snapshot=next;g_cached=false;
  reset_source();
  // Metadata may select a different archive even for an unchanged camera.
  // Invalidate queued requests/previews and notify cached dashboard consumers.
  cancel();invalidate();
  xSemaphoreGive(g_lock);
}

const PackInfo* info_for(const char* identity) {
  for(size_t i=0;i<g_snapshot.count;++i)if(std::strcmp(g_snapshot.packs[i].identity,identity)==0)return &g_snapshot.packs[i];
  return nullptr;
}

bool choose(const Request& request,orcmap::Viewport* view,char* path,size_t size,const PackInfo** info) {
  *view=request.view;
  orcmap::GeoBounds bounds;
  const orcmap::PackManifest* pack=nullptr;
  if(std::strcmp(request.preferred,"firmware-world") && orcmap::GetVisibleBounds(*view,&bounds))
    pack=g_inventory.resolve(bounds,view->zoom,request.preferred);
  if(pack) {
    *info=info_for(pack->pack_id.c_str());
    if(*info && (*info)->usable && pack_source_view(request.view,pack->min_zoom,pack->max_zoom,view)) {
      std::snprintf(path,size,"%s",pack->archive_path.c_str());return true;
    }
  }
  *info=info_for("firmware-world");
  if(!*info || !(*info)->usable)return false;
  if(!world_overview_view(request.view,(*info)->min_zoom,(*info)->max_zoom,view))return false;
  std::snprintf(path,size,"@world");return true;
}

void download_worker(void*) {
  {
    DiscoveryLock lock(portMAX_DELAY);
    if(downloads::service(g_storage))g_scan.store(true);
  }
  g_download_finished.store(true,std::memory_order_release);
  vTaskSuspend(nullptr); // UI reaps the WithCaps stack without another allocation.
}
void worker(void*) {
  for(;;) {
    Request request;
    try {
    if(g_scan.load()) {
      DiscoveryLock lock(0);
      if(lock.held&&g_scan.exchange(false))discover();
    }
    if(xQueueReceive(g_queue,&request,pdMS_TO_TICKS(200))!=pdTRUE)continue;
    if(request.serial!=g_serial.load())continue;
    // The UI never waits on this mutex. A new frame is published atomically
    // when complete; partially drawn sprites are never sent to the display.
    WorkerLock lock;
    g_frame={};g_frame.state=Frame::unavailable;
    char path[256]{};orcmap::Viewport view;const PackInfo* info=nullptr;
    bool ok=choose(request,&view,path,sizeof(path),&info);
    g_frame.camera_zoom=request.view.zoom;g_frame.source_zoom=view.zoom;
    if(info) {text(g_frame.title,info->title);text(g_frame.credit,info->credit);}
    if(ok && std::strcmp(path,g_archive)) {
      reset_source();
      if(path[0]=='@')g_source.reset(new(std::nothrow) orcmap::esp_idf::PartitionByteSource(kWorld));
      else g_source.reset(new(std::nothrow) orcmap::esp_idf::FileByteSource(path));
      if(g_source && g_source->Valid()) {
        g_read_cache.Reset(g_source.get());
        g_pipeline.reset(new(std::nothrow) Pipeline(g_read_cache,{clock_us,service,&g_ready.serial}));
        const char* profile=path[0]=='@' ? "orcmaps-overview-1" : nullptr;
        if(!profile)for(const auto& pack:g_inventory.catalog().Packs())if(pack.archive_path==path)profile=pack.schema_version.c_str();
        ok=g_pipeline && profile && g_pipeline->open(profile);
        if(ok)std::snprintf(g_archive,sizeof(g_archive),"%s",path);
      } else ok=false;
    }
    g_ready=request; // service hook observes this request's immutable serial
    if(ok && (g_canvas.width()!=view.width_px || g_canvas.height()!=view.height_px)) {
      reset_replay();
      // Mandatory frame storage takes priority over this optional read cache.
      g_read_cache.Reset(g_source.get());g_read_buffer.reset();g_read_cache_bytes.store(0);
      g_canvas.deleteSprite();g_canvas_bytes.store(0);
      const size_t bytes=static_cast<size_t>(view.width_px)*view.height_px*2;
      ok=view.width_px>0 && view.height_px>0 && view.width_px<=1280 && view.height_px<=720 &&
          heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)>=bytes+128*1024;
      if(ok) {g_canvas.setPsram(true);g_canvas.setColorDepth(16);ok=g_canvas.createSprite(view.width_px,view.height_px)!=nullptr;}
      if(ok)g_canvas_bytes.store(bytes);
    }
    // Reserve a preview only if publication will need to allocate one.
    const size_t preview_reserve=g_preview_bytes.load() &&
        g_preview.width()==request.view.width_px && g_preview.height()==request.view.height_px ? 0 :
        static_cast<size_t>(request.view.width_px)*request.view.height_px*2;
    if(ok && path[0]!='@' && !g_read_buffer &&
       heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)>=ReadCache::kCapacity+128*1024+preview_reserve) {
      g_read_buffer.reset(static_cast<uint8_t*>(heap_caps_aligned_alloc(64,ReadCache::kCapacity,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)));
      g_read_cache.Reset(g_source.get(),g_read_buffer.get(),g_read_buffer ? ReadCache::kCapacity : 0);
      g_read_cache_bytes.store(g_read_cache.Capacity());
    }
    // Mandatory canvas/preview and decoding scratch retain priority. A failed
    // optional allocation leaves the original streaming path available.
    if(ok && path[0]!='@' && !g_replay_buffer &&
       heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)>=kReplayCapacity+512*1024+preview_reserve) {
      g_replay_buffer.reset(static_cast<uint8_t*>(heap_caps_malloc(kReplayCapacity,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)));
      g_feature_cache.Reset(g_replay_buffer.get(),g_replay_buffer ? kReplayCapacity : 0);
      g_replay_bytes.store(g_feature_cache.Capacity());
      if(g_pipeline)g_pipeline->set_feature_cache(&g_feature_cache);
    }
    g_worker_wait_us=0;g_worker_yields=0;g_read_cache.ResetStats();
    if(ok) {
      orcmap::m5gfx_adapter::DisplayTarget target(g_canvas);
      orcmap::m5gfx_adapter::LabelTarget text_target(g_canvas,fonts::DejaVu24);
      g_yield_us=clock_us();ok=g_pipeline && g_pipeline->render(view,target,request.labels?&g_labels:nullptr,request.labels?&text_target:nullptr,request.view.zoom);
    }
    if(g_pipeline)g_frame.stats=g_pipeline->stats();
    g_frame.worker_wait_us=g_worker_wait_us;g_frame.worker_yields=g_worker_yields;
    g_frame.read_cache=g_read_cache.stats();g_frame.read_cache_bytes=g_read_cache.Capacity();
    if(g_frame.stats.out_of_memory) {
      reset_source();
      g_canvas.deleteSprite();g_canvas_bytes.store(0);
    }
    if(request.serial==g_serial.load()) {
      if(ok)publish_preview(request);
      g_frame.state=ok ? Frame::ready : Frame::unavailable;
      text(g_frame.status,ok ? "LOCAL MAP" : g_frame.stats.out_of_memory ? "MAP UNAVAILABLE: LOW MEMORY" :
           g_frame.stats.deferred_capacity_failures ? "MAP UNAVAILABLE: TILE TOO DENSE" : "MAP UNAVAILABLE / NO MEMORY");
      if(ok && g_frame.stats.labels.unsupported_glyphs)
        std::snprintf(g_frame.status,sizeof(g_frame.status),"LOCAL MAP: %lu NAMES NEED FONT SUPPORT",
                      static_cast<unsigned long>(g_frame.stats.labels.unsupported_glyphs));
      g_cached=true;g_frame.revision=g_revision.fetch_add(1)+1;
    }
    } catch(const std::bad_alloc&) {
      WorkerLock lock;
      reset_source();
      g_canvas.deleteSprite();g_canvas_bytes.store(0);g_cached=false;
      if(request.serial&&request.serial==g_serial.load()) {
        g_ready=request;g_frame={};g_frame.stats.out_of_memory=true;
        text(g_frame.status,"MAP UNAVAILABLE: LOW MEMORY");
        g_cached=true;g_frame.revision=g_revision.fetch_add(1)+1;
      } else if(!request.serial) {
        g_snapshot.loaded=true;g_snapshot.out_of_memory=true;++g_snapshot.revision;
      }
    }
  }
}
size_t held() { return g_canvas_bytes.load()+g_preview_bytes.load()+g_read_cache_bytes.load()+g_replay_bytes.load(); }
size_t release() {
  if(!g_lock || xSemaphoreTake(g_lock,0)!=pdTRUE)return 0;
  if(g_preview_lock&&xSemaphoreTake(g_preview_lock,0)!=pdTRUE){xSemaphoreGive(g_lock);return 0;}
  const size_t bytes=g_canvas_bytes.exchange(0)+g_preview_bytes.exchange(0)+g_read_cache_bytes.load()+g_replay_bytes.load();
  g_canvas.deleteSprite();g_preview.deleteSprite();g_preview_valid=false;g_preview_lifetime.fetch_add(1);g_cached=false;
  if(g_preview_lock)xSemaphoreGive(g_preview_lock);
  g_serial.fetch_add(1); // the same camera must request a new frame after reclaim
  reset_source();xSemaphoreGive(g_lock);return bytes;
}
} // namespace

bool begin(storage::FileSystem* sd) {
  if(g_task)return true;
  // External BSS is NOLOAD: restore this polymorphic object's vtable at runtime.
  new(&g_read_cache) ReadCache();
  new(&g_feature_cache) FeatureCache();
  preferences::load();
  g_sd=sd!=nullptr;
  g_storage=sd;
  downloads::begin();
  g_lock=xSemaphoreCreateMutex();
  g_preview_lock=xSemaphoreCreateMutex();
  g_discovery_lock=xSemaphoreCreateMutex();
  g_queue=xQueueCreateWithCaps(1,sizeof(Request),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  const auto failed=[]() {
    if(g_lock)vSemaphoreDelete(g_lock);
    if(g_queue)vQueueDeleteWithCaps(g_queue);
    if(g_preview_lock)vSemaphoreDelete(g_preview_lock);
    if(g_discovery_lock)vSemaphoreDelete(g_discovery_lock);
    g_preview_lock=nullptr;g_discovery_lock=nullptr;
    g_lock=nullptr;g_queue=nullptr;g_task=nullptr;return false;
  };
  if(!g_lock || !g_preview_lock || !g_discovery_lock || !g_queue)return failed();
  if(xTaskCreatePinnedToCore(worker,"orcsdr_maps",8192,nullptr,1,&g_task,1)!=pdPASS)return failed();
  (void)memory_reclaim::register_holder({"Map frame",held,release});
  return true;
}
void service_downloads() {
  if(!g_task)return;
  if(g_download_task&&g_download_finished.load(std::memory_order_acquire)&&
     eTaskGetState(g_download_task)==eSuspended) {
    std::printf("ORCMAPS_DOWNLOAD_WORKER done=1 stack_hwm_bytes=%u\n",
        static_cast<unsigned>(uxTaskGetStackHighWaterMark(g_download_task)));
    vTaskDeleteWithCaps(g_download_task);g_download_task=nullptr;
    g_download_finished.store(false,std::memory_order_relaxed);
  }
  if(!g_download_task&&downloads::work_pending()) {
    // NVS commits disable caches: the complete job must have an internal stack.
    // Reclaim it after each job so idle maps do not reduce pairing headroom.
    if(xTaskCreatePinnedToCoreWithCaps(download_worker,"orcsdr_map_dl",8192,nullptr,1,
        &g_download_task,1,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)!=pdPASS) {
      g_download_task=nullptr;downloads::worker_unavailable();
    }
  }
}
bool running() {return g_task!=nullptr;}
bool snapshot(MapSnapshot* out) {
  if(!out || !g_lock || xSemaphoreTake(g_lock,0)!=pdTRUE)return false;
  *out=g_snapshot;xSemaphoreGive(g_lock);return true;
}
void rescan() {g_scan.store(true);(void)downloads::retry_recovery();cancel();}
void cancel() {g_preview_lifetime.fetch_add(1);g_serial.fetch_add(1);}
void invalidate() {g_revision.fetch_add(1);}
uint32_t revision() {return g_revision.load();}
bool installed() {return g_installed.load();}
bool frame_info(FrameInfo* out) {
  if(!out || !g_lock || xSemaphoreTake(g_lock,0)!=pdTRUE)return false;
  *out=g_frame;xSemaphoreGive(g_lock);return true;
}
void report(void (*write)(const char*)) {
  if(!write)return;
  char line[512];
  using preferences::Consumer;
  std::snprintf(line,sizeof(line),"ORC_MAP_LABELS picker=%u lora=%u adsb=%u ft8=%u\n",
      preferences::labels(Consumer::picker)?1u:0u,preferences::labels(Consumer::lora)?1u:0u,
      preferences::labels(Consumer::adsb)?1u:0u,preferences::labels(Consumer::ft8)?1u:0u);write(line);
  downloads::Snapshot install_status;
  if(downloads::snapshot(&install_status)) {
    std::snprintf(line,sizeof(line),"ORC_MAP_INSTALL phase=%u busy=%u pending=%u revision=%lu\n",
        static_cast<unsigned>(install_status.phase),install_status.busy?1u:0u,install_status.pending?1u:0u,
        static_cast<unsigned long>(install_status.revision));write(line);
  }
  // Keep preview diagnostics observable even while decoding holds the worker lock.
  if(g_preview_lock&&xSemaphoreTake(g_preview_lock,0)==pdTRUE) {
    const auto pan_paint=g_preview_pan_timing.paint(),zoom_paint=g_preview_zoom_timing.paint();
    const auto pan_feedback=g_preview_pan_timing.feedback(),zoom_feedback=g_preview_zoom_timing.feedback();
    const auto pan_intervals=g_preview_pan_timing.intervals(),zoom_intervals=g_preview_zoom_timing.intervals();
    const auto pan_clear=g_preview_pan_timing.clear(),zoom_clear=g_preview_zoom_timing.clear();
    const auto pan_transfer=g_preview_pan_timing.transfer(),zoom_transfer=g_preview_zoom_timing.transfer();
    const PreviewFacts facts[2]={g_preview_facts[0],g_preview_facts[1]};
    xSemaphoreGive(g_preview_lock);
    for(int mode=0;mode<2;++mode) {
      const auto& observed=facts[mode];
      if(observed.valid) {
        std::snprintf(line,sizeof(line),"ORC_MAP_PREVIEW_FACTS mode=%s request=%lu display_target=%u rotation_raw=%d rotation_offset=%d derived_internal_rotation=%d physical=%dx%d logical=%dx%d source=%dx%d source_depth=%lu target_depth=%lu clip=%ld,%ld,%ld,%ld clipped_map_pixels=%llu copy_bbox_pixels=%llu copy_bbox_exact=%u\n",
            mode==0?"same_scale":"scaled",static_cast<unsigned long>(observed.request),observed.display_target?1u:0u,
            observed.rotation,observed.offset,observed.derived_rotation,observed.physical_w,observed.physical_h,
            observed.logical_w,observed.logical_h,observed.source_w,observed.source_h,
            static_cast<unsigned long>(observed.source_depth),static_cast<unsigned long>(observed.target_depth),
            static_cast<long>(observed.clip_x),static_cast<long>(observed.clip_y),static_cast<long>(observed.clip_w),static_cast<long>(observed.clip_h),
            static_cast<unsigned long long>(observed.clipped_map_pixels),static_cast<unsigned long long>(observed.copy_bbox_pixels),
            observed.copy_bbox_exact?1u:0u);
        write(line);
      }
      for(int metric=0;metric<5;++metric) {
        const auto& timing=metric==0?(mode==0?pan_paint:zoom_paint):
            metric==1?(mode==0?pan_feedback:zoom_feedback):metric==2?(mode==0?pan_intervals:zoom_intervals):
            metric==3?(mode==0?pan_clear:zoom_clear):(mode==0?pan_transfer:zoom_transfer);
        std::snprintf(line,sizeof(line),"ORC_MAP_PREVIEW mode=%s metric=%s samples=%u p50_us=%llu p95_us=%llu max_us=%llu lock_misses=%lu raw_includes_dwell=%u physical_touch_latency=0\n",
            mode==0?"same_scale":"scaled",metric==0?"paint_call":metric==1?"request_feedback":
                metric==2?"present_interval":metric==3?"clear_call":"transfer_call",
            static_cast<unsigned>(timing.count),static_cast<unsigned long long>(timing.p50_us),
            static_cast<unsigned long long>(timing.p95_us),static_cast<unsigned long long>(timing.max_us),
            static_cast<unsigned long>(g_preview_lock_misses.load()),metric==2?1u:0u);
        write(line);
      }
    }
  } else write("ORC_MAP_PREVIEW busy=1\n");
  FrameInfo frame;
  if(!frame_info(&frame)) {std::snprintf(line,sizeof(line),"ORC_MAP_STATUS busy=1 generation=%lu\n",static_cast<unsigned long>(g_serial.load()));write(line);return;}
  const auto& stats=frame.stats;
  std::snprintf(line,sizeof(line),"ORC_MAP_SOURCE camera_zoom=%u source_zoom=%u title=\"%s\"\n",
      frame.camera_zoom,frame.source_zoom,frame.title);write(line);
  // Read the rolling sample under the same lock used for presentation updates.
  if(xSemaphoreTake(g_lock,0)==pdTRUE) {
    const auto timing=g_update_timing.summary();
    xSemaphoreGive(g_lock);
    std::snprintf(line,sizeof(line),"ORC_MAP_INTERVALS raw_includes_dwell=1 samples=%u p50_us=%llu p95_us=%llu max_us=%llu\n",
        static_cast<unsigned>(timing.count),static_cast<unsigned long long>(timing.p50_us),
        static_cast<unsigned long long>(timing.p95_us),static_cast<unsigned long long>(timing.max_us));
    write(line);
  }
  std::snprintf(line,sizeof(line),"ORC_MAP_STATUS state=%u generation=%lu revision=%lu cached_pushes=%lu push_us=%lu frame_bytes=%u "
      "tiles=%lu missing=%lu failed=%lu features=%lu reads=%lu bytes=%llu lookup_us=%llu stream_us=%llu render_us=%llu total_us=%llu io_us=%llu\n",
      static_cast<unsigned>(frame.state),static_cast<unsigned long>(g_serial.load()),static_cast<unsigned long>(frame.revision),
      static_cast<unsigned long>(frame.cache_hits),static_cast<unsigned long>(frame.push_us),static_cast<unsigned>(g_canvas_bytes.load()),
      static_cast<unsigned long>(stats.tiles),static_cast<unsigned long>(stats.missing),static_cast<unsigned long>(stats.failed),
      static_cast<unsigned long>(stats.features),static_cast<unsigned long>(stats.reads),static_cast<unsigned long long>(stats.bytes),
      static_cast<unsigned long long>(stats.lookup_us),static_cast<unsigned long long>(stats.stream_us),
      static_cast<unsigned long long>(stats.render_us),static_cast<unsigned long long>(stats.total_us),static_cast<unsigned long long>(stats.io_us));
  write(line);
  std::snprintf(line,sizeof(line),"ORC_MAP_READ_CACHE capacity_bytes=%lu hits=%llu misses=%llu backing_reads=%llu backing_bytes=%llu\n",
      static_cast<unsigned long>(frame.read_cache_bytes),static_cast<unsigned long long>(frame.read_cache.hits),
      static_cast<unsigned long long>(frame.read_cache.misses),static_cast<unsigned long long>(frame.read_cache.backing_reads),
      static_cast<unsigned long long>(frame.read_cache.backing_bytes));
  write(line);
  std::snprintf(line,sizeof(line),"ORC_MAP_REPLAY capacity_bytes=%u retained_bytes=%u tile_hits=%lu fallbacks=%lu\n",
      static_cast<unsigned>(stats.replay_capacity),static_cast<unsigned>(stats.replay_bytes),
      static_cast<unsigned long>(stats.replay_hits),static_cast<unsigned long>(stats.replay_fallbacks));
  write(line);
  std::snprintf(line,sizeof(line),"ORC_MAP_WORKER yields=%lu wait_us=%llu included_in_total=1 tick_us=%llu\n",
      static_cast<unsigned long>(frame.worker_yields),static_cast<unsigned long long>(frame.worker_wait_us),
      static_cast<unsigned long long>(portTICK_PERIOD_MS)*1000);
  write(line);
  std::snprintf(line,sizeof(line),"ORC_MAP_DEFERRED peak_bytes=%u retained_bytes=%u capacity_failures=%lu\n",
      static_cast<unsigned>(stats.deferred_peak_bytes),static_cast<unsigned>(stats.deferred_retained_bytes),
      static_cast<unsigned long>(stats.deferred_capacity_failures));
  write(line);
  std::snprintf(line,sizeof(line),"ORC_MAP_LABELS offered=%lu drawn=%lu overlap=%lu offscreen=%lu unsupported=%lu overflow=%lu label_us=%llu layout_bytes=%u\n",
      static_cast<unsigned long>(stats.labels.offered),static_cast<unsigned long>(stats.labels.drawn),
      static_cast<unsigned long>(stats.labels.overlap),static_cast<unsigned long>(stats.labels.offscreen),
      static_cast<unsigned long>(stats.labels.unsupported_glyphs),static_cast<unsigned long>(stats.labels.pool_overflow),
      static_cast<unsigned long long>(stats.label_us),static_cast<unsigned>(sizeof(g_labels)));
  write(line);
  std::snprintf(line,sizeof(line),"ORC_MAP_MEMORY internal_free=%u internal_largest=%u dma_free=%u dma_largest=%u psram_free=%u psram_largest=%u\n",
      heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
      heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA),heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA),
      heap_caps_get_free_size(MALLOC_CAP_SPIRAM),heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
  write(line);
}
Frame draw(const orcmap::Viewport& view,lgfx::v1::LovyanGFX& target,int x,int y,FrameInfo* info,const char* preferred,bool labels) {
  if(!g_task || !g_queue || !g_lock)return Frame::unavailable;
  Request request{};request.labels=labels;request.view=view;request.lifetime=g_preview_lifetime.load();
  if(preferred && std::strlen(preferred)>=sizeof(request.preferred))return Frame::unavailable;
  text(request.preferred,preferred);
  // Only the UI writes g_requested. Queue/cancel before taking the frame lock
  // so a pan can interrupt a render that is still holding that lock.
  if(!same(request,g_requested) || g_requested.serial!=g_serial.load()) {
    request.serial=g_serial.fetch_add(1)+1;request.requested_us=clock_us();g_requested=request;
    xQueueOverwrite(g_queue,&request);
  }
  request=g_requested; // Preserve the first acceptance timestamp across repaint/lock retries.
  if(xSemaphoreTake(g_lock,0)!=pdTRUE){draw_preview(request,target,x,y,info);return Frame::pending;}
  if(g_cached && g_ready.serial==g_serial.load() && same(request,g_ready)) {
    if(g_frame.state==Frame::ready) {
      int32_t clip_x,clip_y,clip_w,clip_h;
      target.getClipRect(&clip_x,&clip_y,&clip_w,&clip_h);
      const uint64_t started=clock_us();g_canvas.pushSprite(&target,x,y);
      notify_native_display_dirty(target,x,y,request.view.width_px,request.view.height_px,
                                  clip_x,clip_y,clip_w,clip_h);
      g_frame.push_us=static_cast<uint32_t>(clock_us()-started);++g_frame.cache_hits;
      g_update_timing.present(clock_us(),g_frame.revision);
    }
    if(info)*info=g_frame;
    const Frame result=g_frame.state;xSemaphoreGive(g_lock);return result;
  }
  xSemaphoreGive(g_lock);draw_preview(request,target,x,y,info);return Frame::pending;
}
} // namespace orcsdr::maps
