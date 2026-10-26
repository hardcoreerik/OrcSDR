// LORA / MESH dashboard (apps/orcsdr-tab5/ui/lora_dashboard.cpp, lora_native_decoder.cpp, lora_preamble_detector.cpp and
// lora_channel_control.cpp, unchanged) on the P4 console. This adapter carries the capture logic the Tab5 keeps in main.cpp:
// pre-roll ring, preamble / energy trigger, a 4 s IQ capture handed to the native decoder task, and Meshtastic node and
// packet tracking. SD logging, the node history file and the gain guard are not available on this target yet.
#include <algorithm>
#include <atomic>
#include <cstring>
#include "audio_service.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "home_app.hpp"
#include "lora_channel_control.hpp"
#include "lora_dashboard.hpp"
#include "lora_native_decoder.hpp"
#include "lora_preamble_detector.hpp"
#include "nvs_store.hpp"
#include "radio_service.hpp"
#include "screens.hpp"
#include "ui_support.hpp"

namespace {
namespace lr = orcsdr::lora;
namespace ln = orcsdr::lora_native;
constexpr char TAG[] = "LORA";

constexpr uint32_t kRate = 960000;
constexpr size_t kRecBytes = static_cast<size_t>(kRate) * 2u * 4u;  // a 4 s capture
constexpr size_t kPreRollBytes = kRate / 2u;                        // 250 ms of CU8 IQ
constexpr size_t kQuietTailBytes = kRate / 2u;
constexpr size_t kPreambleMinCaptureBytes = static_cast<size_t>(kRate) * 2u * 2u;
constexpr uint32_t kPreambleQuietSamples = kRate / 3u;
constexpr size_t kCandidateAfterTriggerSamples = static_cast<size_t>(kRate) * 3u / 4u;
constexpr float kTriggerMarginDb = 9.0f, kTriggerHysteresisDb = 3.0f, kTriggerCeilingDbfs = -6.0f;
constexpr float kFloorSlowAlpha = 0.995f, kFloorFastAlpha = 0.9f, kFloorFastFallDb = 3.0f;
constexpr uint32_t kSustainedHighMs = 4000;
constexpr size_t kPacketRing = 16, kNodeRing = 16;

struct DisplayPacket {
  uint32_t sender = 0, destination = 0, packet_id = 0, received_ms = 0, received_utc = 0;
  int32_t latitude_e7 = INT32_MAX, longitude_e7 = INT32_MAX;
  int16_t signal_tenths = INT16_MAX, snr_tenths = INT16_MAX;
  uint16_t port = 0;
  bool encrypted = false;
  char short_name[8] = {}, long_name[32] = {}, text[ln::kPacketTextBytes] = {};
};
struct NodePosition {
  uint32_t node = 0, received_ms = 0;
  int32_t latitude_e7 = INT32_MAX, longitude_e7 = INT32_MAX;
  int16_t signal_tenths = INT16_MAX, snr_tenths = INT16_MAX;
  char name[32] = {};
};
struct DecodeWork {
  uint8_t* iq;
  size_t bytes;
  bool automatic;
  uint32_t sequence, trigger_offset_samples;
};

// buffers (PSRAM, allocated on first use and kept)
uint8_t *g_rec = nullptr, *g_dec = nullptr, *g_pre = nullptr;
size_t g_pre_write = 0, g_pre_fill = 0;
std::atomic<bool> g_rec_active{false}, g_rec_ready{false}, g_rec_auto{false}, g_decode_busy{false}, g_decoder_ready{false};
std::atomic<size_t> g_rec_write{0};
uint32_t g_rec_started_ms = 0, g_rec_trigger_offset = 0, g_rec_sequence = 0;
std::atomic<uint32_t> g_sequence{0}, g_drops{0}, g_rf_events{0};
QueueHandle_t g_queue = nullptr;
// detector
float g_noise_floor = -90.0f;
int g_noise_samples = 0;
bool g_armed = false, g_drop_latched = false;
uint32_t g_high_since_ms = 0, g_quiet_tail = 0;
std::atomic<float> g_noise_dbfs{-90.0f}, g_trigger_dbfs{-75.0f};
std::atomic<bool> g_relearn{false};
// results
portMUX_TYPE g_lock = portMUX_INITIALIZER_UNLOCKED;
DisplayPacket g_packets[kPacketRing];
NodePosition g_nodes[kNodeRing];
std::atomic<uint32_t> g_messages{0}, g_crc_ok{0}, g_encrypted{0}, g_failures{0};
uint32_t g_favorite = 0;
uint8_t g_selected = 0;
std::atomic<uint8_t> g_sf{11};
std::atomic<uint32_t> g_bw{250000};
bool g_started = false;
uint32_t g_config_hz = 906875000;
char g_profile[24] = "LONGFAST";
char g_region[24] = "US";
uint32_t g_survey_restore = 0;

uint32_t now_ms() { return ui_support::now_ms(); }

bool ensure_buffers() {
  if (!g_rec) g_rec = static_cast<uint8_t*>(heap_caps_malloc(kRecBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!g_dec) g_dec = static_cast<uint8_t*>(heap_caps_malloc(kRecBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!g_pre) g_pre = static_cast<uint8_t*>(heap_caps_malloc(kPreRollBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  (void)orcsdr::lora_preamble::configure(g_sf.load());
  return g_rec && g_dec && g_pre;
}

void reset_detector() {
  g_pre_write = g_pre_fill = 0;
  g_noise_floor = -90.0f;
  g_noise_samples = 0;
  g_armed = false;
  g_drop_latched = false;
  g_noise_dbfs = -90.0f;
  g_trigger_dbfs = -75.0f;
  orcsdr::lora_preamble::reset();
}

void pre_roll_append(const uint8_t* iq, size_t bytes) {
  if (!g_pre || !iq || !bytes) return;
  if (bytes >= kPreRollBytes) {
    std::memcpy(g_pre, iq + bytes - kPreRollBytes, kPreRollBytes);
    g_pre_write = 0;
    g_pre_fill = kPreRollBytes;
    return;
  }
  const size_t first = std::min(bytes, kPreRollBytes - g_pre_write);
  std::memcpy(g_pre + g_pre_write, iq, first);
  if (first < bytes) std::memcpy(g_pre, iq + first, bytes - first);
  g_pre_write = (g_pre_write + bytes) % kPreRollBytes;
  g_pre_fill = std::min(kPreRollBytes, g_pre_fill + bytes);
}

size_t copy_pre_roll() {
  if (!g_rec || !g_pre || !g_pre_fill) return 0;
  if (g_pre_fill < kPreRollBytes) {
    std::memcpy(g_rec, g_pre, g_pre_fill);
    return g_pre_fill;
  }
  const size_t tail = kPreRollBytes - g_pre_write;
  std::memcpy(g_rec, g_pre + g_pre_write, tail);
  if (g_pre_write) std::memcpy(g_rec + tail, g_pre, g_pre_write);
  return kPreRollBytes;
}

void rec_begin(bool automatic, size_t initial_bytes) {
  g_rec_ready = false;
  g_rec_auto = automatic;
  g_rec_write = initial_bytes;
  g_rec_sequence = automatic ? g_sequence.fetch_add(1) + 1 : 0;
  g_rec_started_ms = now_ms();
  g_rec_trigger_offset = static_cast<uint32_t>(initial_bytes / 2);
  g_quiet_tail = 0;
  g_rec_active = true;
}

bool queue_auto_decode() {
  if (!g_rec_ready.load() || !g_rec_auto.load() || !g_dec || !g_queue) return false;
  bool expected = false;
  if (!g_decode_busy.compare_exchange_strong(expected, true)) return false;
  const size_t bytes = g_rec_write.load();
  uint8_t* work_iq = g_rec;  // hand the finished buffer to the decoder, capture continues in the other one
  g_rec = g_dec;
  g_dec = work_iq;
  const DecodeWork work{work_iq, bytes, true, g_rec_sequence, g_rec_trigger_offset};
  g_rec_ready = false;
  g_rec_auto = false;
  g_rec_write = 0;
  if (xQueueSend(g_queue, &work, 0) == pdTRUE) return true;
  g_decode_busy = false;
  g_failures.fetch_add(1);
  g_drops.fetch_add(1);
  return false;
}

void rec_finish() {
  g_rec_active = false;
  g_rec_ready = true;
  if (g_rec_auto.load()) {
    (void)queue_auto_decode();
  } else if (g_decoder_ready.load() && g_queue) {
    const DecodeWork work{g_rec, g_rec_write.load(), false, 0, 0};
    g_decode_busy = true;
    if (xQueueSend(g_queue, &work, 0) != pdTRUE) {
      g_decode_busy = false;
      g_failures.fetch_add(1);
      g_rec_ready = false;
      g_rec_write = 0;
    }
  } else {
    g_failures.fetch_add(1);
    g_rec_ready = false;
    g_rec_write = 0;
  }
}

void rec_append(const uint8_t* iq, size_t bytes) {
  size_t written = g_rec_write.load();
  if (written >= kRecBytes) return;
  const size_t count = std::min(bytes, kRecBytes - written);
  std::memcpy(g_rec + written, iq, count);
  written += count;
  g_rec_write = written;
  if (written == kRecBytes) rec_finish();
}

void store_packet(const DisplayPacket& input) {
  DisplayPacket packet = input;
  if (!packet.received_ms) packet.received_ms = now_ms();
  if (!packet.received_utc) packet.received_utc = ui_support::utc_epoch();
  portENTER_CRITICAL(&g_lock);
  if (packet.packet_id) {
    for (const auto& e : g_packets)
      if (e.received_ms && e.sender == packet.sender && e.packet_id == packet.packet_id) {
        portEXIT_CRITICAL(&g_lock);
        return;
      }
  }
  size_t replace = kPacketRing;
  if (packet.port == 3) {  // position update: replace the sender's previous one
    for (size_t i = 0; i < kPacketRing; ++i)
      if (g_packets[i].received_ms && g_packets[i].sender == packet.sender && g_packets[i].port == 3) {
        replace = i;
        break;
      }
  }
  const size_t shift = replace < kPacketRing ? replace : kPacketRing - 1;
  std::memmove(&g_packets[1], &g_packets[0], sizeof(g_packets[0]) * shift);
  g_packets[0] = packet;
  if (packet.sender) {
    size_t idx = 0;
    while (idx < kNodeRing && g_nodes[idx].node != packet.sender) ++idx;
    const bool known = idx < kNodeRing;
    if (!known) idx = kNodeRing - 1;
    NodePosition node = known ? g_nodes[idx] : NodePosition{};
    node.node = packet.sender;
    node.received_ms = packet.received_ms;
    if (packet.signal_tenths != INT16_MAX) node.signal_tenths = packet.signal_tenths;
    if (packet.snr_tenths != INT16_MAX) node.snr_tenths = packet.snr_tenths;
    if (packet.latitude_e7 != INT32_MAX && packet.longitude_e7 != INT32_MAX) {
      node.latitude_e7 = packet.latitude_e7;
      node.longitude_e7 = packet.longitude_e7;
    }
    if (packet.short_name[0]) std::snprintf(node.name, sizeof(node.name), "%.7s", packet.short_name);
    else if (packet.long_name[0]) std::snprintf(node.name, sizeof(node.name), "%.31s", packet.long_name);
    if (idx > 0) std::memmove(&g_nodes[1], &g_nodes[0], sizeof(g_nodes[0]) * idx);
    g_nodes[0] = node;
  }
  portEXIT_CRITICAL(&g_lock);
  g_messages.fetch_add(1);
}

void decode_task(void*) {
  ln::Packet* decoded = ln::task_packets();
  ln::Stats* stats = ln::task_stats();
  for (;;) {
    DecodeWork work{};
    if (xQueueReceive(g_queue, &work, portMAX_DELAY) != pdTRUE) continue;
    *stats = ln::Stats{};
    const size_t candidate_samples =
        work.automatic ? std::min<size_t>(work.bytes / 2, work.trigger_offset_samples + kCandidateAfterTriggerSamples) : 0;
    const ln::Config config{nullptr, 0, candidate_samples, false};
    const size_t count = ln::decode_capture(work.iq, work.bytes, kRate, g_sf.load(), g_bw.load(),
                                            home_app::status().frequency_hz, config, decoded, ln::kMaxPacketsPerCapture, stats);
    g_crc_ok.fetch_add(stats->crc_ok);
    g_encrypted.fetch_add(stats->encrypted);
    if (stats->preambles == 0) g_relearn = true;
    if (!stats->ready) g_failures.fetch_add(1);
    for (size_t i = 0; i < count; ++i) {
      DisplayPacket p;
      std::snprintf(p.text, sizeof(p.text), "%s", decoded[i].text);
      p.sender = decoded[i].sender;
      p.destination = decoded[i].destination;
      p.packet_id = decoded[i].packet_id;
      p.received_ms = now_ms();
      p.latitude_e7 = decoded[i].latitude_e7;
      p.longitude_e7 = decoded[i].longitude_e7;
      p.port = decoded[i].port;
      p.encrypted = decoded[i].encrypted;
      std::snprintf(p.short_name, sizeof(p.short_name), "%s", decoded[i].short_name);
      std::snprintf(p.long_name, sizeof(p.long_name), "%s", decoded[i].long_name);
      store_packet(p);
    }
    ESP_LOGI(TAG, "decode: %u packets, preambles=%u crc_ok=%u header_failures=%u in %u ms", unsigned(count), unsigned(stats->preambles),
             unsigned(stats->crc_ok), unsigned(stats->header_failures), unsigned(stats->decode_millis));
    g_decode_busy = false;
    (void)queue_auto_decode();
  }
}

bool decoder_start() {
  if (g_decoder_ready.load()) return true;
  if (!ln::initialize() || !ln::task_packets() || !ln::task_stats()) {
    ESP_LOGE(TAG, "native decoder workspace allocation failed: needs %u bytes, PSRAM free %u (largest block %u)", unsigned(ln::psram_bytes()),
             unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)), unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));
    return false;
  }
  // Priority 0 (the idle task's): the decode is CPU-bound for tens of seconds and never yields, so at any higher priority
  // the idle task would be starved and the task watchdog would fire; at 0 the two share the core by time slicing.
  g_queue = xQueueCreate(1, sizeof(DecodeWork));
  if (!g_queue || xTaskCreatePinnedToCore(decode_task, "lora_native", 12288, nullptr, 0, nullptr, 1) != pdPASS) return false;
  g_decoder_ready = true;
  return true;
}

// Runs on the receiver task for every 960 kS/s block.
void iq_offer(const uint8_t* iq, size_t bytes) {
  if (!iq || !bytes || !g_pre || !g_rec) return;
  if (orcsdr::lora_channel::survey_active()) return;
  // Block level, dBFS (single precision: the FPU has no double).
  float acc = 0.0f;
  const size_t count = std::min<size_t>(bytes, 4096);
  for (size_t i = 0; i < count; ++i) {
    const float v = (static_cast<float>(iq[i]) - 127.5f) * (1.0f / 127.5f);
    acc += v * v;
  }
  const float rms = std::sqrt(acc / static_cast<float>(count));
  const float level = rms > 1e-6f ? 20.0f * std::log10(rms) : -120.0f;

  if (g_relearn.exchange(false)) {
    g_noise_floor = level;
    g_noise_samples = 0;
    g_armed = false;
    g_drop_latched = false;
  }
  pre_roll_append(iq, bytes);
  const bool preamble_mode = orcsdr::lora_preamble::ready();
  orcsdr::lora_preamble::Result preamble{};
  if (preamble_mode) preamble = orcsdr::lora_preamble::feed(iq, bytes);
  if (g_rec_active.load()) {
    rec_append(iq, bytes);
    if (g_rec_active.load() && g_rec_auto.load()) {
      if (preamble_mode) {
        if (g_rec_write.load() >= kPreambleMinCaptureBytes && preamble.quiet_samples >= kPreambleQuietSamples) rec_finish();
      } else {
        const float quiet_below = g_trigger_dbfs.load() - kTriggerHysteresisDb;
        g_quiet_tail = level < quiet_below ? g_quiet_tail + static_cast<uint32_t>(bytes) : 0;
        if (g_quiet_tail >= kQuietTailBytes) rec_finish();
      }
    }
    return;
  }
  if (!g_decoder_ready.load()) return;
  if (g_rec_ready.load()) {
    const float trigger = g_trigger_dbfs.load();
    const bool burst = preamble_mode ? preamble.fired : level >= trigger;
    if (burst && !g_drop_latched) {
      g_drop_latched = true;
      g_drops.fetch_add(1);
    } else if (preamble_mode ? !preamble.fired && level < trigger - kTriggerHysteresisDb : level < trigger - kTriggerHysteresisDb) {
      g_drop_latched = false;
    }
    return;
  }
  if (preamble_mode && preamble.fired) {
    const size_t pre = copy_pre_roll();
    g_rf_events.fetch_add(1);
    rec_begin(true, pre);
    return;
  }
  if (g_noise_samples == 0) g_noise_floor = level;
  if (g_noise_samples < 12) {
    g_noise_floor = 0.85f * g_noise_floor + 0.15f * level;
    ++g_noise_samples;
    g_noise_dbfs = g_noise_floor;
    return;
  }
  const float trigger = std::clamp(g_noise_floor + kTriggerMarginDb, -78.0f, kTriggerCeilingDbfs);
  g_noise_dbfs = g_noise_floor;
  g_trigger_dbfs = trigger;
  if (level < trigger - kTriggerHysteresisDb) {
    g_drop_latched = false;
    g_armed = true;
    g_high_since_ms = 0;
    const float alpha = level < g_noise_floor - kFloorFastFallDb ? kFloorFastAlpha : kFloorSlowAlpha;
    g_noise_floor = alpha * g_noise_floor + (1.0f - alpha) * level;
    return;
  }
  const uint32_t t = now_ms();
  if (g_high_since_ms == 0) {
    g_high_since_ms = t;
  } else if (t - g_high_since_ms >= kSustainedHighMs) {  // a level that stays high is a new floor, not a burst
    g_noise_floor = level;
    g_noise_samples = 0;
    g_armed = false;
    g_high_since_ms = 0;
    return;
  }
  if (preamble_mode || !g_armed || level < trigger) return;
  g_armed = false;
  const size_t pre = copy_pre_roll();
  g_rf_events.fetch_add(1);
  rec_begin(true, pre);
}

// ---- channel ----
void apply_channel_selection() {
  const auto& sel = orcsdr::lora_channel::selection();
  g_config_hz = sel.frequency_hz;
  g_sf = 11;
  g_bw = orcsdr::lora_channel::kLongFastBandwidthHz;
  std::snprintf(g_profile, sizeof(g_profile), "LONGFAST");
  std::snprintf(g_region, sizeof(g_region), "%.23s", orcsdr::lora_channel::region(sel.region_index).code);
}

void select_channel(size_t region_index, uint16_t slot) {
  orcsdr::NvsStore store;
  if (!store.begin("orcsdr_lora", false)) return;
  const bool ok = orcsdr::lora_channel::choose(region_index, slot, store);
  store.end();
  if (!ok) return;
  apply_channel_selection();
  reset_detector();
  home_app::request_frequency(orcsdr::lora_channel::selection().frequency_hz);
}

lr::Snapshot& build() {
  static lr::Snapshot s;
  s = lr::Snapshot{};
  const auto& channel = orcsdr::lora_channel::selection();
  const home_app::Status st = home_app::status();
  const auto rs = orcsdr::radio::snapshot();
  s.frequency_hz = st.frequency_hz;
  s.span_hz = st.span_hz;
  s.sf = g_sf.load();
  s.region_index = channel.region_index;
  s.region_count = static_cast<uint8_t>(orcsdr::lora_channel::region_count());
  s.channel_slot = orcsdr::lora_channel::slot_for_frequency(channel.region_index, s.frequency_hz);
  s.channel_count = orcsdr::lora_channel::slot_count(channel.region_index);
  s.default_slot = orcsdr::lora_channel::default_slot(channel.region_index);
  s.bandwidth_hz = g_bw.load();
  s.noise_dbfs = g_noise_dbfs.load();
  s.trigger_dbfs = g_trigger_dbfs.load();
  s.relative_dbfs = orcsdr::radio::level_dbfs();
  s.decoded_frames = g_messages.load();
  s.crc_ok = g_crc_ok.load();
  s.encrypted_frames = g_encrypted.load();
  s.uptime_seconds = now_ms() / 1000u;
  s.running = rs.receiver_state == 2;
  s.driver_ready = rs.receiver_state == 2;
  s.sound_enabled = st.sound;
  s.survey_active = orcsdr::lora_channel::survey_active();
  s.survey_progress = orcsdr::lora_channel::survey_progress();
  s.survey_result_count = std::min<uint8_t>(orcsdr::lora_channel::survey_result_count(), lr::kSurveyResultCapacity);
  for (uint8_t i = 0; i < s.survey_result_count; ++i) {
    const auto r = orcsdr::lora_channel::survey_result(i);
    s.survey_results[i] = {r.frequency_hz, r.level_dbfs};
  }
  s.iq_recording = g_rec_active.load();
  s.iq_ready = g_rec_ready.load();
  s.iq_busy = g_decode_busy.load();
  s.battery_percent = -1;
  s.native_decoder_ready = g_decoder_ready.load() && s.bandwidth_hz == 250000;
  s.key_loaded = false;
  std::snprintf(s.profile, sizeof(s.profile), "%s", g_profile);
  std::snprintf(s.region, sizeof(s.region), "%s", g_region);
  std::snprintf(s.log_status, sizeof(s.log_status), "SD LOG UNAVAILABLE");
  portENTER_CRITICAL(&g_lock);
  for (const auto& p : g_nodes) {
    if (!p.node || s.node_count >= lr::kNodeCapacity) continue;
    lr::Node& n = s.nodes[s.node_count++];
    n.id = p.node;
    n.seen_ms = p.received_ms;
    n.latitude_e7 = p.latitude_e7;
    n.longitude_e7 = p.longitude_e7;
    n.signal_tenths = p.signal_tenths;
    n.snr_tenths = p.snr_tenths;
    n.favorite = p.node == g_favorite;
    std::snprintf(n.name, sizeof(n.name), "%.31s", p.name);
  }
  for (const auto& p : g_packets) {
    if (!p.received_ms || s.event_count >= lr::kEventCapacity) continue;
    lr::Event& e = s.events[s.event_count++];
    e.sender = p.sender;
    e.destination = p.destination;
    e.packet_id = p.packet_id;
    e.received_ms = p.received_ms;
    e.received_utc = p.received_utc;
    e.latitude_e7 = p.latitude_e7;
    e.longitude_e7 = p.longitude_e7;
    e.signal_tenths = p.signal_tenths;
    e.snr_tenths = p.snr_tenths;
    e.port = p.port;
    e.encrypted = p.encrypted;
    e.verified = p.sender != 0;
    std::snprintf(e.text, sizeof(e.text), "%.111s", p.text);
  }
  portEXIT_CRITICAL(&g_lock);
  s.selected_node = std::min<uint8_t>(g_selected, s.node_count == 0 ? 0 : static_cast<uint8_t>(s.node_count - 1));
  s.revision = s.decoded_frames;
  s.revision ^= static_cast<uint32_t>(s.event_count) << 24;
  s.revision ^= static_cast<uint32_t>(s.node_count) << 16;
  s.revision ^= static_cast<uint32_t>(s.selected_node) << 8;
  s.revision ^= s.survey_active ? 1u : 0u;
  s.revision ^= s.survey_progress << 20;
  s.revision ^= s.iq_recording ? 4u : 0u;
  s.revision ^= s.iq_ready ? 8u : 0u;
  s.revision ^= s.iq_busy ? 16u : 0u;
  s.revision ^= g_favorite;
  return s;
}

void enter() {
  {
    orcsdr::NvsStore store;
    if (store.begin("orcsdr_lora", true)) {
      orcsdr::lora_channel::load(store);
      store.end();
    }
  }
  apply_channel_selection();
  audio_service::set_active(false);
  ui_support::header_reset();
  // The decoder's own workspace first (it needs the biggest contiguous blocks), then the capture buffers.
  if (!decoder_start() || !ensure_buffers()) ESP_LOGE(TAG, "LoRa decode path unavailable (memory)");
  reset_detector();
  home_app::request_frequency(g_config_hz);
  orcsdr::radio::set_aux_iq_sink(iq_offer);
  lr::enter(build());
}
void leave() {
  orcsdr::radio::set_aux_iq_sink(nullptr);
  g_rec_active = false;
  g_rec_ready = false;
  g_rec_write = 0;
  if (orcsdr::lora_channel::survey_active()) (void)orcsdr::lora_channel::cancel_survey();
  lr::leave();
}

void tick() {
  const uint32_t now = now_ms();
  // channel survey
  const auto step = orcsdr::lora_channel::service_survey(now, true, orcsdr::radio::level_dbfs());
  if (step.frequency_hz) home_app::request_frequency(step.frequency_hz);
  // an automatic capture that stalls (radio stopped or retuned) is abandoned
  static uint32_t last_progress_ms = 0;
  static size_t last_write = 0;
  const size_t w = g_rec_write.load();
  if (!g_rec_active.load() || w != last_write) {
    last_write = w;
    last_progress_ms = now;
  } else if (g_rec_auto.load() && now - last_progress_ms > 1500) {
    g_rec_active = false;
    g_rec_auto = false;
    g_rec_ready = false;
    g_rec_write = 0;
    g_drops.fetch_add(1);
  }
  lr::update(build());
  ui_support::header_service();
}

void tap(int x, int y) {
  if (ui_support::handle_header_touch(x, y, true)) return;
  const lr::Action a = lr::handle_touch(x, y);
  const auto& sel = orcsdr::lora_channel::selection();
  switch (a.kind) {
    case lr::ActionKind::select_view:
      if (a.value < static_cast<uint32_t>(lr::View::count)) lr::show_documentation_view(static_cast<lr::View>(a.value), build());
      break;
    case lr::ActionKind::select_node:
      if (a.value < lr::kNodeCapacity) g_selected = static_cast<uint8_t>(a.value);
      break;
    case lr::ActionKind::toggle_favorite: {
      const lr::Snapshot& s = build();
      if (s.node_count) {
        const uint32_t id = s.nodes[s.selected_node].id;
        g_favorite = g_favorite == id ? 0 : id;
      }
      break;
    }
    case lr::ActionKind::filter_next: lr::toggle_filter(); break;
    case lr::ActionKind::toggle_packet_details: lr::toggle_packet_details(); break;
    case lr::ActionKind::center_map: lr::center_on_selected(); break;
    case lr::ActionKind::follow_node: lr::toggle_follow_node(); break;
    case lr::ActionKind::scan_toggle:
      if (orcsdr::lora_channel::survey_active()) home_app::request_frequency(orcsdr::lora_channel::cancel_survey());
      else orcsdr::lora_channel::start_survey(g_config_hz, now_ms());
      break;
    case lr::ActionKind::record_iq_toggle:
      if (g_rec_active.load()) {
        if (!g_rec_auto.load()) rec_finish();
      } else if (!g_rec_ready.load() && !g_decode_busy.load() && g_decoder_ready.load()) {
        rec_begin(false, 0);
      }
      break;
    case lr::ActionKind::clear_events:
      portENTER_CRITICAL(&g_lock);
      for (auto& p : g_packets) p = DisplayPacket{};
      portEXIT_CRITICAL(&g_lock);
      break;
    case lr::ActionKind::open_channels: lr::open_channel_picker(); break;
    case lr::ActionKind::region_previous:
      select_channel((sel.region_index + orcsdr::lora_channel::region_count() - 1) % orcsdr::lora_channel::region_count(), 0);
      break;
    case lr::ActionKind::region_next: select_channel((sel.region_index + 1) % orcsdr::lora_channel::region_count(), 0); break;
    case lr::ActionKind::region_select:
      if (a.value >= 1 && a.value <= orcsdr::lora_channel::region_count()) select_channel(a.value - 1, 0);
      break;
    case lr::ActionKind::channel_previous:
      select_channel(sel.region_index, sel.slot <= 1 ? orcsdr::lora_channel::slot_count(sel.region_index) : sel.slot - 1);
      break;
    case lr::ActionKind::channel_next:
      select_channel(sel.region_index, sel.slot >= orcsdr::lora_channel::slot_count(sel.region_index) ? 1 : sel.slot + 1);
      break;
    case lr::ActionKind::channel_select: select_channel(sel.region_index, static_cast<uint16_t>(a.value)); break;
    case lr::ActionKind::open_settings: home_app::request_open(orcsdr::dashboards::Id::settings); break;
    case lr::ActionKind::exit_home: home_app::request_home(); break;
    default: break;  // logging / export / mark point / snapshot: need the SD card
  }
}

void spectrum(const float* levels, size_t first, size_t visible, float floor) { lr::draw_spectrum(levels, first, visible, floor); }
}  // namespace

namespace screens {
const Screen& lora_screen() {
  static const Screen s{orcsdr::dashboards::Id::lora, "LORA", enter, leave, tick, tap, nullptr, spectrum};
  return s;
}
}  // namespace screens
