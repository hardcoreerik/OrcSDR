// SPDX-License-Identifier: AGPL-3.0-or-later
// Standalone ESP32-P4 synthetic qualification firmware; no USB/radio/SD APIs.
#include "orcsdr_dmr_probe/handoff.hpp"
#include "orcsdr_dmr_probe/pattern.hpp"
#include "orcsdr_dmr/d1_bridge.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <atomic>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <new>
#include <memory>

namespace p = orcsdr::dmr::device_probe;
namespace d = orcsdr::dmr::d1;
namespace {
constexpr const char* kTag = "DMR_DEVICE_PROBE";
#ifdef CONFIG_DMR_PROBE_PSRAM
constexpr bool kUsePsram = true;
#else
constexpr bool kUsePsram = false;
#endif
#ifdef CONFIG_DMR_PROBE_FORCE_OVERLOAD
constexpr bool kForceOverload = true;
#else
constexpr bool kForceOverload = false;
#endif
using Attachment = p::Attachment<d::ComplexSample,d::integration::Receiver>;
Attachment* g_probe = nullptr;
p::Pattern* g_pattern = nullptr;
std::atomic<bool> g_producer_done{false};
std::atomic<bool> g_worker_done{false};
std::atomic<std::uint32_t> g_worker_stack_min{UINT32_MAX};
std::atomic<std::uint32_t> g_offer_max_us{0};
std::atomic<std::uint32_t> g_offer_total_us{0};
std::atomic<std::uint32_t> g_processed_blocks{0};
std::atomic<std::uint32_t> g_worker_max_us{0};
std::atomic<std::uint32_t> g_worker_total_us{0};
std::atomic<std::uint32_t> g_worker_over_deadline{0};
std::atomic<std::uint32_t> g_worker_p99_bucket[9]{};
constexpr std::uint32_t kBucketBoundaries[] = {250,500,1000,2000,3000,4000,5000,10000};
std::uint32_t free_internal() { return static_cast<std::uint32_t>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)); }
std::uint32_t free_psram() { return static_cast<std::uint32_t>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)); }
void update_max(std::atomic<std::uint32_t>& target,std::uint32_t value) noexcept {
  auto x=target.load(std::memory_order_relaxed);
  while(x<value&&!target.compare_exchange_weak(x,value,std::memory_order_relaxed)){}
}
void update_min(std::atomic<std::uint32_t>& target,std::uint32_t value) noexcept {
  auto x=target.load(std::memory_order_relaxed);
  while(x>value&&!target.compare_exchange_weak(x,value,std::memory_order_relaxed)){}
}
std::uint32_t bucket_us(std::uint32_t us) noexcept {
  for(std::uint32_t i=0;i<8;++i) if(us<=kBucketBoundaries[i]) return i;
  return 8;
}
void worker_task(void*) {
  bool injected=false;
  while(!g_producer_done.load(std::memory_order_acquire)||g_probe->queue().counters().queued_blocks) {
    if (kForceOverload && !injected &&
        g_processed_blocks.load(std::memory_order_relaxed)>static_cast<std::uint32_t>(CONFIG_DMR_PROBE_SECONDS*90)) {
      injected=true;
      vTaskDelay(pdMS_TO_TICKS(100));
    }
    if(!g_probe->queue().counters().queued_blocks){vTaskDelay(pdMS_TO_TICKS(1));continue;}
    const auto start=esp_timer_get_time();
    if(!g_probe->drain_one()){continue;}
    const auto elapsed=esp_timer_get_time()-start;
    const auto us=static_cast<std::uint32_t>(elapsed>UINT32_MAX?UINT32_MAX:elapsed);
    g_processed_blocks.fetch_add(1,std::memory_order_relaxed);
    g_worker_total_us.fetch_add(us,std::memory_order_relaxed); // 120s max * 48k < 2^32 typical
    update_max(g_worker_max_us,us);
    if(us>5000)g_worker_over_deadline.fetch_add(1,std::memory_order_relaxed);
    g_worker_p99_bucket[bucket_us(us)].fetch_add(1,std::memory_order_relaxed);
    if((g_processed_blocks.load(std::memory_order_relaxed)&127U)==0U) {
      // ESP-IDF reports task stack high-water in BYTES, not vanilla FreeRTOS words.
      update_min(g_worker_stack_min,static_cast<std::uint32_t>(uxTaskGetStackHighWaterMark(nullptr)));
    }
  }
  update_min(g_worker_stack_min,static_cast<std::uint32_t>(uxTaskGetStackHighWaterMark(nullptr)));
  g_worker_done.store(true,std::memory_order_release);
  vTaskDelete(nullptr);
}
void* allocate(std::size_t n) {
  const auto cap=(kUsePsram ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL)|MALLOC_CAP_8BIT;
  return heap_caps_aligned_alloc(64,n,cap);
}
} // namespace
extern "C" void app_main() {
  ESP_LOGI(kTag,"PROBE_ONLY: synthetic 48 kS/s IQ, no tuner/USB/microSD/dashboard changes");
  ESP_LOGI(kTag,"IDF target ESP32-P4; runtime=%d s tickHz=%u",
           CONFIG_DMR_PROBE_SECONDS,static_cast<unsigned>(configTICK_RATE_HZ));
  if(configTICK_RATE_HZ!=1000) { ESP_LOGE(kTag,"NEED_1MS_TICK: qualification refused");return; }
  const auto internal_before=free_internal(),external_before=free_psram();
  void* probe_memory=allocate(sizeof(Attachment));
  void* pattern_memory=allocate(sizeof(p::Pattern));
  if(!probe_memory||!pattern_memory){
    ESP_LOGE(kTag,"ALLOCATION_FAILED: attachment=%zu pattern=%zu selected=%s",
             sizeof(Attachment),sizeof(p::Pattern), kUsePsram?"PSRAM":"internal");
    if(probe_memory)heap_caps_free(probe_memory);
    if(pattern_memory)heap_caps_free(pattern_memory);
    return;
  }
  g_probe=new(probe_memory) Attachment{};
  g_pattern=new(pattern_memory) p::Pattern{};
  if(!p::make_pattern(*g_pattern)){ESP_LOGE(kTag,"SYNTH_PATTERN_FAILED");return;}
  const auto internal_after=free_internal(),external_after=free_psram();
  ESP_LOGI(kTag,"STATIC_SIZES queue=%zu frontend=%zu protocol=%zu bridge=%zu full_attachment=%zu stimulus=%zu",
           sizeof(Attachment::Queue),sizeof(d::Frontend),sizeof(d::Decoder),
           sizeof(d::integration::Receiver),sizeof(Attachment),sizeof(p::Pattern));
  ESP_LOGI(kTag,"HEAP_START internal_before=%" PRIu32 " internal_after=%" PRIu32
               " psram_before=%" PRIu32 " psram_after=%" PRIu32 " psram_detected=%zu",
           internal_before,internal_after,external_before,external_after,esp_psram_get_size());
  const auto started=xTaskCreatePinnedToCore(worker_task,"dmr-probe-worker",20480,nullptr,5,nullptr,1);
  if(started!=pdPASS){ESP_LOGE(kTag,"WORKER_TASK_ALLOCATION_FAILED");return;}
  TickType_t tick=xTaskGetTickCount();
  const auto t0=esp_timer_get_time();
  std::uint32_t late_ticks=0,rejected=0,overload=0,expected_epoch=1;
  const std::uint32_t total_blocks=static_cast<std::uint32_t>(CONFIG_DMR_PROBE_SECONDS*200);
  for(std::uint32_t block=0;block<total_blocks;++block){
    if(block==total_blocks/3U || block==(total_blocks*2U)/3U)++expected_epoch;
    if(block==total_blocks/2U){
      // Explicit upstream missed-sample notification, no fake zero fill.
      g_probe->note_gap(p::kBlockSamples);
    } else {
      const auto offset=static_cast<std::size_t>(block%12U)*p::kBlockSamples;
      const auto offer_start=esp_timer_get_time();
      const auto offered=g_probe->offer(g_pattern->data()+offset,p::kBlockSamples,
                                        p::kRequiredRateHz,expected_epoch);
      const auto offer_elapsed=esp_timer_get_time()-offer_start;
      const auto offer_us=static_cast<std::uint32_t>(offer_elapsed>UINT32_MAX?UINT32_MAX:offer_elapsed);
      g_offer_total_us.fetch_add(offer_us,std::memory_order_relaxed);
      update_max(g_offer_max_us,offer_us);
      if(offered==p::Offer::full)++overload;
      if(offered!=p::Offer::accepted && offered!=p::Offer::full)++rejected;
    }
    const auto elapsed=esp_timer_get_time()-t0;
    if(elapsed > static_cast<std::int64_t>(block+1)*5000)++late_ticks;
    if(block%200U==199U){
      const auto queue=g_probe->queue().counters();
      ESP_LOGI(kTag,"SECOND %u queued=%" PRIu32 " peak=%" PRIu32 " dropped=%" PRIu32 " worker_blocks=%" PRIu32,
               static_cast<unsigned>((block+1)/200U),queue.queued_blocks,queue.max_queued_blocks,
               queue.dropped_samples,g_processed_blocks.load());
    }
    vTaskDelayUntil(&tick,pdMS_TO_TICKS(5));
  }
  g_producer_done.store(true,std::memory_order_release);
  const auto finish_wait=esp_timer_get_time()+5000000;
  while(!g_worker_done.load(std::memory_order_acquire) && esp_timer_get_time()<finish_wait)
    vTaskDelay(pdMS_TO_TICKS(10));
  if(!g_worker_done.load(std::memory_order_acquire)){
    ESP_LOGE(kTag,"WORKER_STALLED: no qualification results; leave receiver state allocated");
    return;
  }
  const auto elapsed_us=esp_timer_get_time()-t0;
  const auto c=g_probe->queue().counters();
  const auto f=g_probe->receiver().frontend_stats();
  const auto b=g_probe->receiver().stats();
  const auto blocks=g_processed_blocks.load();
  const auto total=g_worker_total_us.load();
  const auto p99_target=(blocks*99U+99U)/100U;
  std::uint32_t acc=0,p99_upper=UINT32_MAX;
  for(std::uint32_t i=0;i<9;++i){
    acc+=g_worker_p99_bucket[i].load();
    if(acc>=p99_target){p99_upper=i<8?kBucketBoundaries[i]:UINT32_MAX;break;}
  }
  ESP_LOGI(kTag,"RESULT device=ESP32-P4 input_rate=48000 duration_s=%d elapsed_us=%" PRId64
      " accepted_samples=%" PRIu32 " decoded_input_samples=%" PRIu64 " dropped_samples=%" PRIu32
      " queue_peak_blocks=%" PRIu32 " producer_late_ticks=%" PRIu32
      " producer_full_calls=%" PRIu32 " producer_rejected=%" PRIu32,
      CONFIG_DMR_PROBE_SECONDS,elapsed_us,c.accepted_samples,g_probe->processed_samples(),c.dropped_samples,
      c.max_queued_blocks,late_ticks,overload,rejected);
  ESP_LOGI(kTag,"PRODUCER_OFFER avg_us=%" PRIu32 " max_us=%" PRIu32 " (bounded copy only, no USB context)",
       blocks?g_offer_total_us.load()/blocks:0U,g_offer_max_us.load());
  const auto worker_busy_permille=elapsed_us>0 ? static_cast<std::uint32_t>(
      (static_cast<std::uint64_t>(total)*1000U)/static_cast<std::uint64_t>(elapsed_us)) : 0U;
  const auto worker_stack_free=g_worker_stack_min.load();
  const auto worker_stack_used=worker_stack_free<=20480U?20480U-worker_stack_free:UINT32_MAX;
  ESP_LOGI(kTag,"WORKER blocks=%" PRIu32 " avg_us=%" PRIu32 " max_us=%" PRIu32
      " p99_bucket_upper_us=%" PRIu32 " over_5000us=%" PRIu32 " worker_busy_permille=%" PRIu32
      " discontinuities=%" PRIu32 " processing_errors=%" PRIu32,
      blocks,blocks?total/blocks:0U,g_worker_max_us.load(),p99_upper,
      g_worker_over_deadline.load(),worker_busy_permille,
      g_probe->discontinuities(),g_probe->processing_errors());
  ESP_LOGI(kTag,"DMR_FRONTEND locks=%" PRIu64 " prerolls=%" PRIu64 " incomplete_preroll=%" PRIu64
      " DMR_BRIDGE protocol_events=%" PRIu64 " validated_identities=%" PRIu64,
      f.locks,f.full_preroll_acquisitions,f.incomplete_preroll_rejections,b.protocol_events,b.validated_identities);
  ESP_LOGI(kTag,"MEMORY internal_before=%" PRIu32 " internal_after=%" PRIu32 " internal_free_end=%" PRIu32
      " internal_min_free=%zu psram_before=%" PRIu32 " psram_after=%" PRIu32
      " psram_free_end=%" PRIu32 " psram_min_free=%zu worker_stack_highwater_bytes=%" PRIu32
      " worker_stack_est_used_bytes=%" PRIu32,
      internal_before,internal_after,free_internal(),
      heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
      external_before,external_after,free_psram(),
      heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT),worker_stack_free,worker_stack_used);
  const bool success=c.dropped_samples==p::kBlockSamples && rejected==0 && overload==0 &&
      late_ticks==0 &&
      g_probe->processing_errors()==0 && g_probe->processed_samples()==c.accepted_samples &&
      g_worker_over_deadline.load()==0 && g_worker_done.load();
  ESP_LOGI(kTag,"QUALIFICATION_SYNTHETIC %s; real USB/SDR concurrency NOT MEASURED; capture source NOT ATTACHED",
           success&&!kForceOverload?"PASS_SYNTHETIC_ONLY":"FAIL_OR_OVERLOAD");
  // Isolated CPU probes after the sustained worker has stopped. These are
  // separate load cases, NOT additive decomposition of the integrated bridge.
  void* front_mem=allocate(sizeof(d::Frontend));
  void* proto_mem=allocate(sizeof(d::Decoder));
  if(!front_mem||!proto_mem) {
    ESP_LOGW(kTag,"ISOLATED_CPU_BENCH NOT_MEASURED (allocation failed)");
  } else {
    auto* frontend=new(front_mem) d::Frontend{};
    auto* protocol=new(proto_mem) d::Decoder{};
    p::DibitPattern hard{};
    p::make_dibit_pattern(hard);
    const auto frontend_start=esp_timer_get_time();
    std::uint32_t frontend_consumed=0,frontend_symbols=0;
    bool frontend_ok=true;
    while(frontend_consumed<48000U) {
      const auto offset=frontend_consumed%static_cast<std::uint32_t>(p::kPatternSamples);
      const auto ask=static_cast<std::size_t>((p::kPatternSamples-offset)<8U ? (p::kPatternSamples-offset):8U);
      d::Symbol sym{};
      const auto result=frontend->process(g_pattern->data()+offset,ask,&sym,1);
      if(result.consumed==0||result.consumed>ask||result.produced>1){frontend_ok=false;break;}
      frontend_consumed+=static_cast<std::uint32_t>(result.consumed);
      frontend_symbols+=static_cast<std::uint32_t>(result.produced);
    }
    const auto frontend_elapsed=esp_timer_get_time()-frontend_start;
    const auto protocol_start=esp_timer_get_time();
    bool protocol_ok=true;
    for(std::uint32_t i=0;i<4800U;++i) {
      const auto d=hard[i%hard.size()];
      if(!protocol->feed(&d,1,nullptr,nullptr)){protocol_ok=false;break;}
    }
    const auto protocol_elapsed=esp_timer_get_time()-protocol_start;
    ESP_LOGI(kTag,"ISOLATED_CPU_BENCH frontend_48k_us=%" PRId64
         " frontend_samples=%" PRIu32 " symbols=%" PRIu32 " frontend_ok=%u"
         " protocol_4800dibits_us=%" PRId64 " protocol_ok=%u"
         " (no concurrent USB; not additive bridge cost)",
         frontend_elapsed,frontend_consumed,frontend_symbols,unsigned(frontend_ok),
         protocol_elapsed,unsigned(protocol_ok));
    frontend->~Frontend();protocol->~Decoder();
  }
  if(front_mem)heap_caps_free(front_mem);
  if(proto_mem)heap_caps_free(proto_mem);
  std::destroy_at(g_probe);std::destroy_at(g_pattern);
  heap_caps_free(probe_memory);heap_caps_free(pattern_memory);
}
