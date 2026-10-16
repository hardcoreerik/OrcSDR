#include "acars_dashboard_model.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "acars_demo_data.hpp"

namespace orcsdr::acars_ui {
namespace {

// ---- Regional presets. Sources: acarsdec README (USA primary 131.550, secondary 130.025; Europe primary
// 131.725, secondary 131.525), SigIDWiki / RadioReference / Airframes ACARS pages (130.450, 131.125, 130.425,
// 129.125 as US/Canada channels; 131.450 Japan). Assignments vary by operator and change over time: these are
// starting points for a listener, not a guarantee that traffic exists. Labels never claim traffic.
constexpr Preset kNorthAmerica[] = {
    {131550000u, "131.550 PRIMARY", "acarsdec README (USA primary); widely listed worldwide"},
    {130025000u, "130.025 SECONDARY", "acarsdec README (USA secondary)"},
    {129125000u, "129.125 US/CA", "SigIDWiki, RadioReference (US/Canada); Airframes lists it global"},
    {130450000u, "130.450 US/CA", "RadioReference, WiNRADiO; busy-hub overflow per some sources"},
    {131125000u, "131.125 US", "SigIDWiki, RadioReference"},
    {130425000u, "130.425 US", "SigIDWiki, RadioReference"},
};
constexpr Preset kEurope[] = {
    {131725000u, "131.725 PRIMARY", "acarsdec README (Europe primary); Wavecom"},
    {131525000u, "131.525 SECONDARY", "acarsdec README (Europe secondary); Wavecom"},
    {131825000u, "131.825 EXTRA", "Wavecom, UDXF table (least corroborated)"},
    {131550000u, "131.550 WORLDWIDE", "SigIDWiki, RadioReference (worldwide common channel)"},
};
constexpr Preset kAsiaPacific[] = {
    {131550000u, "131.550 PRIMARY", "SigIDWiki, RadioReference (Asia/Pacific, worldwide)"},
    {131450000u, "131.450 JAPAN", "SigIDWiki, RadioReference (Japan primary)"},
};
static_assert(sizeof(kNorthAmerica) / sizeof(Preset) <= kChannelRows, "preset list exceeds visible rows");
static_assert(sizeof(kEurope) / sizeof(Preset) <= kChannelRows, "preset list exceeds visible rows");
static_assert(sizeof(kAsiaPacific) / sizeof(Preset) <= kChannelRows, "preset list exceeds visible rows");

constexpr char kKeyRegion[] = "acars_region";
constexpr char kKeyPreset[] = "acars_preset";
constexpr char kKeyFormat[] = "acars_fmt";
constexpr char kKeyRaw[] = "acars_raw";
constexpr char kKeyCorr[] = "acars_corr";
constexpr char kKeyFilter[] = "acars_filter";
constexpr char kKeySort[] = "acars_sort";
constexpr char kKeyCustom[] = "acars_custom";

bool printable(char c) { return c >= 0x20 && c < 0x7f; }

// Copies up to `count` bytes of a decoder field into a NUL-terminated buffer, stopping at NUL and
// replacing any non-printable byte so the field is always safe to hand to a font renderer.
void copy_field(char* out, size_t capacity, const char* in, size_t count) {
  size_t n = 0;
  for (; n < count && n + 1 < capacity && in[n] != '\0'; ++n) out[n] = printable(in[n]) ? in[n] : '?';
  out[n] = '\0';
}

void trim_trailing_space(char* s) {
  size_t n = std::strlen(s);
  while (n > 0 && s[n - 1] == ' ') s[--n] = '\0';
}

}  // namespace

uint32_t pack(const Capabilities& c) {
  return (c.live_reception ? 1u : 0u) | (c.multi_channel ? 2u : 0u) | (c.archive ? 4u : 0u) |
         (c.signal_measurements ? 8u : 0u) | (c.application_decoding ? 16u : 0u) |
         (c.message_reassembly ? 32u : 0u) | (c.adsb_correlation ? 64u : 0u);
}

Capabilities unpack(uint32_t bits) {
  Capabilities c;
  c.live_reception = bits & 1u;
  c.multi_channel = bits & 2u;
  c.archive = bits & 4u;
  c.signal_measurements = bits & 8u;
  c.application_decoding = bits & 16u;
  c.message_reassembly = bits & 32u;
  c.adsb_correlation = bits & 64u;
  return c;
}

const char* region_name(Region region) {
  switch (region) {
    case Region::north_america: return "NORTH AMERICA";
    case Region::europe: return "EUROPE";
    case Region::asia_pacific: return "ASIA-PACIFIC";
    default: return "?";
  }
}

size_t preset_count(Region region) {
  switch (region) {
    case Region::north_america: return sizeof(kNorthAmerica) / sizeof(Preset);
    case Region::europe: return sizeof(kEurope) / sizeof(Preset);
    case Region::asia_pacific: return sizeof(kAsiaPacific) / sizeof(Preset);
    default: return 0;
  }
}

const Preset& preset(Region region, size_t index) {
  const size_t count = preset_count(region);
  if (count == 0) return kNorthAmerica[0];
  index = std::min(index, count - 1);
  switch (region) {
    case Region::europe: return kEurope[index];
    case Region::asia_pacific: return kAsiaPacific[index];
    default: return kNorthAmerica[index];
  }
}

Preferences load_preferences(const Store& store) {
  Preferences p;
  if (store.get_u8) {
    const uint8_t region = store.get_u8(kKeyRegion, 0);
    p.region = region < static_cast<uint8_t>(Region::count) ? static_cast<Region>(region) : Region::north_america;
    const uint8_t index = store.get_u8(kKeyPreset, 0);
    p.preset = index < preset_count(p.region) ? index : 0;
    const uint8_t format = store.get_u8(kKeyFormat, 0);
    p.format = format < static_cast<uint8_t>(TextFormat::count) ? static_cast<TextFormat>(format) : TextFormat::escaped;
    const uint8_t filter = store.get_u8(kKeyFilter, 0);
    p.filter = filter < static_cast<uint8_t>(MessageFilter::count) ? static_cast<MessageFilter>(filter) : MessageFilter::all;
    const uint8_t sort = store.get_u8(kKeySort, 0);
    p.sort = sort < static_cast<uint8_t>(AircraftSort::count) ? static_cast<AircraftSort>(sort) : AircraftSort::recent;
  }
  if (store.get_u32) {
    const uint32_t hz = store.get_u32(kKeyCustom, 0);
    p.custom_hz = (hz >= kCustomMinHz && hz <= kCustomMaxHz && hz % kCustomStepHz == 0) ? hz : 0;
  }
  if (store.get_bool) {
    p.show_raw_text = store.get_bool(kKeyRaw, true);
    p.highlight_corrected = store.get_bool(kKeyCorr, true);
  }
  return p;
}

void save_preferences(const Store& store, const Preferences& p) {
  if (store.put_u8) {
    store.put_u8(kKeyRegion, static_cast<uint8_t>(p.region));
    store.put_u8(kKeyPreset, p.preset);
    store.put_u8(kKeyFormat, static_cast<uint8_t>(p.format));
    store.put_u8(kKeyFilter, static_cast<uint8_t>(p.filter));
    store.put_u8(kKeySort, static_cast<uint8_t>(p.sort));
  }
  if (store.put_u32) store.put_u32(kKeyCustom, p.custom_hz);
  if (store.put_bool) {
    store.put_bool(kKeyRaw, p.show_raw_text);
    store.put_bool(kKeyCorr, p.highlight_corrected);
  }
}

size_t sanitize_preview(char* out, size_t capacity, const char* text, size_t length, bool* truncated) {
  if (truncated) *truncated = false;
  if (!out || capacity == 0) return 0;
  size_t n = 0;
  for (size_t i = 0; i < length && text; ++i) {
    if (n + 1 >= capacity) {
      if (truncated) *truncated = true;
      break;
    }
    const char c = text[i];
    out[n++] = printable(c) ? c : (c == '\r' || c == '\n' || c == '\t') ? ' ' : '.';
  }
  out[n] = '\0';
  return n;
}

size_t escape_text(char* out, size_t capacity, const char* text, size_t length, bool* truncated) {
  if (truncated) *truncated = false;
  if (!out || capacity == 0) return 0;
  size_t n = 0;
  for (size_t i = 0; i < length && text; ++i) {
    const char c = text[i];
    char piece[8];
    size_t len;
    if (printable(c)) {
      piece[0] = c;
      len = 1;
    } else if (c == '\r') {
      len = static_cast<size_t>(std::snprintf(piece, sizeof(piece), "<CR>"));
    } else if (c == '\n') {
      len = static_cast<size_t>(std::snprintf(piece, sizeof(piece), "<LF>"));
    } else if (c == '\t') {
      len = static_cast<size_t>(std::snprintf(piece, sizeof(piece), "<TAB>"));
    } else {
      len = static_cast<size_t>(std::snprintf(piece, sizeof(piece), "<%02X>", static_cast<unsigned>(static_cast<uint8_t>(c))));
    }
    if (n + len + 1 > capacity) {
      if (truncated) *truncated = true;
      break;
    }
    std::memcpy(out + n, piece, len);
    n += len;
  }
  out[n] = '\0';
  return n;
}

void normalise_tail(char* out, size_t capacity, const char* tail7) {
  if (!out || capacity == 0) return;
  char tmp[8];
  copy_field(tmp, sizeof(tmp), tail7 ? tail7 : "", 7);
  const char* start = tmp;
  while (*start == '.' || *start == ' ') ++start;
  std::snprintf(out, capacity, "%s", start);
  trim_trailing_space(out);
}

const char* end_marker_name(uint8_t marker) {
  switch (marker) {
    case 0x03: return "ETX";
    case 0x17: return "ETB";
    default: return "UNKNOWN";
  }
}

Model::Model() = default;

// In place, member by member: a Ring is ~20 KB, far too big to build as a temporary on a task stack.
void Model::clear_ring(Ring& ring) {
  for (auto& r : ring.records) r = MessageRecord{};
  for (auto& a : ring.aircraft) a = AircraftRecord{};
  ring.next_seq = 1;
  ring.total = ring.corrected = 0;
  ring.held = ring.head = ring.aircraft_used = 0;
  ring.stats = acars::Stats{};
  ring.stats_available = false;
}

void Model::reset() {
  queue_head_.store(0);
  queue_tail_.store(0);
  queue_drops_.store(0);
  stats_seq_.store(0);
  for (auto& w : stats_words_) w.store(0);
  receiver_state_.store(static_cast<uint8_t>(ReceiverState::disconnected));
  caps_bits_.store(0);
  clear_ring(live_);
  clear_ring(demo_store_);
  message_offset_ = aircraft_offset_ = 0;
  demo_ = false;
  tuned_hz_ = 0;
  tune_ = TuneStatus::unavailable;
  seen_stats_seq_ = 0;
  seen_receiver_ = ReceiverState::disconnected;
  seen_caps_ = 0;
  seen_drops_ = 0;
  bump_status();
  bump_messages();
}

// ---------------------------------------------------------------- producer side
bool Model::post_message(const acars::Message& message, uint32_t uptime_ms, uint32_t utc_epoch) {
  const uint32_t head = queue_head_.load(std::memory_order_relaxed);
  const uint32_t tail = queue_tail_.load(std::memory_order_acquire);
  if (head - tail >= kQueueCapacity) {
    queue_drops_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  Event& slot = queue_[head % kQueueCapacity];
  slot.message = message;
  slot.uptime_ms = uptime_ms;
  slot.utc_epoch = utc_epoch;
  queue_head_.store(head + 1, std::memory_order_release);
  return true;
}

void Model::post_stats(const acars::Stats& s) {
  const uint32_t seq = stats_seq_.load(std::memory_order_relaxed);
  stats_seq_.store(seq + 1, std::memory_order_relaxed);   // odd: write in progress
  std::atomic_thread_fence(std::memory_order_release);
  const uint32_t words[7] = {s.sync_candidates, s.crc_ok, s.corrected, s.crc_rejected,
                             s.framing_rejected, s.overflows, s.bits};
  for (size_t i = 0; i < 7; ++i) stats_words_[i].store(words[i], std::memory_order_relaxed);
  stats_seq_.store(seq + 2, std::memory_order_release);
}

void Model::post_receiver_state(ReceiverState state) {
  receiver_state_.store(static_cast<uint8_t>(state), std::memory_order_release);
}

void Model::post_capabilities(const Capabilities& caps) { caps_bits_.store(pack(caps), std::memory_order_release); }

// ---------------------------------------------------------------- records
MessageRecord Model::make_record(const acars::Message& m, uint32_t uptime_ms, uint32_t utc_epoch) {
  MessageRecord r;
  r.source = SourceKind::live;
  r.integrity = m.corrected ? Integrity::crc_corrected : Integrity::crc_verified;
  r.corrected_bits = m.corrected_bits;
  r.time_valid = utc_epoch != 0;
  r.utc_epoch = utc_epoch;
  r.uptime_ms = uptime_ms;
  r.has_text = m.has_text;
  r.end_marker = m.end_marker;
  r.mode = printable(m.mode) ? m.mode : '?';
  r.acknowledgement = printable(m.acknowledgement) ? m.acknowledgement : '?';
  r.block_id = printable(m.block_id) ? m.block_id : '?';
  copy_field(r.label, sizeof(r.label), m.label, 2);
  normalise_tail(r.tail, sizeof(r.tail), m.tail);
  copy_field(r.flight, sizeof(r.flight), m.flight, 6);
  trim_trailing_space(r.flight);
  r.frequency_hz = m.frequency_hz;
  r.raw_bytes = m.raw_bytes;
  r.text_length = static_cast<uint16_t>(std::min<size_t>(m.text_length, sizeof(r.text)));
  std::memcpy(r.text, m.text, r.text_length);
  sanitize_preview(r.preview, sizeof(r.preview), r.text, r.text_length);
  return r;
}

void Model::store_record(Ring& ring, const MessageRecord& record) {
  MessageRecord& slot = ring.records[ring.head];
  slot = record;
  slot.seq = ring.next_seq++;
  ring.head = (ring.head + 1) % kHistoryCapacity;
  if (ring.held < kHistoryCapacity) ++ring.held;
  ++ring.total;
  if (slot.integrity == Integrity::crc_corrected) ++ring.corrected;

  AircraftRecord* a = nullptr;
  for (size_t i = 0; i < ring.aircraft_used; ++i)
    if (std::strcmp(ring.aircraft[i].tail, slot.tail) == 0) { a = &ring.aircraft[i]; break; }
  if (!a) {
    if (ring.aircraft_used < kAircraftTable) {
      a = &ring.aircraft[ring.aircraft_used++];
    } else {                                   // evict the least recently seen aircraft
      a = &ring.aircraft[0];
      for (size_t i = 1; i < kAircraftTable; ++i)
        if (ring.aircraft[i].history_seq[0] < a->history_seq[0]) a = &ring.aircraft[i];
    }
    *a = AircraftRecord{};
    std::snprintf(a->tail, sizeof(a->tail), "%s", slot.tail);
  }
  a->source = slot.source;
  ++a->message_count;
  if (slot.integrity == Integrity::crc_corrected) ++a->corrected_count;
  if (slot.flight[0]) std::snprintf(a->flight, sizeof(a->flight), "%s", slot.flight);
  std::snprintf(a->last_label, sizeof(a->last_label), "%s", slot.label);
  a->last_seen_ms = slot.uptime_ms;
  a->last_utc = slot.utc_epoch;
  a->time_valid = slot.time_valid;
  const size_t keep = std::min<size_t>(a->history_count, kAircraftHistory - 1);
  for (size_t i = keep; i > 0; --i) a->history_seq[i] = a->history_seq[i - 1];
  a->history_seq[0] = slot.seq;
  a->history_count = static_cast<uint8_t>(keep + 1);
}

int Model::channel_index_for(uint32_t hz) const {
  if (hz == 0) return -1;
  const size_t n = preset_count(prefs_.region);
  for (size_t i = 0; i < n; ++i)
    if (preset(prefs_.region, i).frequency_hz == hz) return static_cast<int>(i);
  return -1;
}

// ---------------------------------------------------------------- UI task
void Model::set_store(const Store& store) { store_ = store; }

void Model::load_preferences() {
  prefs_ = acars_ui::load_preferences(store_);
  bump_status();
  bump_messages();
}

void Model::set_device_state(int32_t battery_percent, bool sound_enabled) {
  battery_ = battery_percent;
  sound_ = sound_enabled;
  // Not drawn by the dashboard (the shared header shows them), so they never bump the revision: a battery
  // percent wobbling would otherwise repaint the whole screen. The snapshot is patched in place.
  snapshot_.battery_percent = battery_percent;
  snapshot_.sound_enabled = sound_enabled;
}

void Model::service() {
  uint32_t tail = queue_tail_.load(std::memory_order_relaxed);
  const uint32_t head = queue_head_.load(std::memory_order_acquire);
  bool any = false;
  while (tail != head) {
    const Event& e = queue_[tail % kQueueCapacity];
    MessageRecord r = make_record(e.message, e.uptime_ms, e.utc_epoch);
    r.channel_index = static_cast<int8_t>(channel_index_for(r.frequency_hz));
    store_record(live_, r);
    if (live_sink_) live_sink_(live_sink_context_, live_.records[(live_.head + kHistoryCapacity - 1) % kHistoryCapacity]);
    ++tail;
    any = true;
  }
  if (any) {
    queue_tail_.store(tail, std::memory_order_release);
    bump_messages();
  }

  // Seqlock read, bounded: if the producer is mid-write (or was preempted mid-write) keep the previous
  // stats and try again on the next tick instead of spinning.
  uint32_t s1 = 0, s2 = 1;
  uint32_t words[7];
  for (int attempt = 0; attempt < 4 && (s1 != s2 || (s1 & 1u)); ++attempt) {
    s1 = stats_seq_.load(std::memory_order_acquire);
    for (size_t i = 0; i < 7; ++i) words[i] = stats_words_[i].load(std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_acquire);
    s2 = stats_seq_.load(std::memory_order_relaxed);
  }
  if (s1 == s2 && !(s1 & 1u) && s1 != seen_stats_seq_ && s1 != 0) {
    seen_stats_seq_ = s1;
    const acars::Stats before = live_.stats;
    const bool was_available = live_.stats_available;
    live_.stats = {words[0], words[1], words[2], words[3], words[4], words[5], words[6]};
    live_.stats_available = true;
    // The demodulated-bit counter moves with every sample block, i.e. several times a second, while nothing else
    // changes. Bumping the revision for it would repaint whole pages for no visible reason (the flicker the user
    // saw). A bits-only change patches the snapshot in place; the one view that shows it asks for it separately.
    const bool only_bits = was_available && before.sync_candidates == live_.stats.sync_candidates &&
                           before.crc_ok == live_.stats.crc_ok && before.corrected == live_.stats.corrected &&
                           before.crc_rejected == live_.stats.crc_rejected &&
                           before.framing_rejected == live_.stats.framing_rejected &&
                           before.overflows == live_.stats.overflows;
    if (only_bits) {
      if (!demo_) snapshot_.stats.bits = live_.stats.bits;
    } else {
      bump_status();
    }
  }
  const ReceiverState rs = static_cast<ReceiverState>(receiver_state_.load(std::memory_order_acquire));
  const uint32_t caps = caps_bits_.load(std::memory_order_acquire);
  const uint32_t drops = queue_drops_.load(std::memory_order_relaxed);
  if (rs != seen_receiver_ || caps != seen_caps_ || drops != seen_drops_) {
    seen_receiver_ = rs;
    seen_caps_ = caps;
    seen_drops_ = drops;
    bump_status();
  }
}

bool Model::set_demo(bool enabled, DemoScenario scenario) {
  if (enabled == demo_ && (!enabled || scenario == scenario_)) return false;
  clear_ring(demo_store_);                       // demo content never survives a mode change
  message_offset_ = aircraft_offset_ = 0;
  demo_ = enabled;
  scenario_ = scenario;
  demo_receiver_ = ReceiverState::running;
  if (enabled) acars_demo::populate(*this, scenario);
  bump_status();
  bump_messages();
  return true;
}

bool Model::persist_and_bump() {
  if (store_.put_u8 || store_.put_bool || store_.put_u32) save_preferences(store_, prefs_);
  bump_status();
  return true;
}

bool Model::set_region(Region region) {
  if (region >= Region::count || region == prefs_.region) return false;
  prefs_.region = region;
  prefs_.preset = 0;
  prefs_.custom_hz = 0;
  if (tuned_hz_ != preset(region, 0).frequency_hz && tune_ != TuneStatus::unavailable) tune_ = TuneStatus::pending;
  bump_messages();
  return persist_and_bump();
}

bool Model::set_preset(size_t index) {
  if (index >= preset_count(prefs_.region) || (index == prefs_.preset && prefs_.custom_hz == 0)) return false;
  prefs_.preset = static_cast<uint8_t>(index);
  prefs_.custom_hz = 0;
  if (tuned_hz_ != preset(prefs_.region, index).frequency_hz && tune_ != TuneStatus::unavailable)
    tune_ = TuneStatus::pending;
  return persist_and_bump();
}

bool Model::set_text_format(TextFormat format) {
  if (format >= TextFormat::count || format == prefs_.format) return false;
  prefs_.format = format;
  return persist_and_bump();
}

bool Model::set_show_raw_text(bool show) {
  if (show == prefs_.show_raw_text) return false;
  prefs_.show_raw_text = show;
  return persist_and_bump();
}

bool Model::set_highlight_corrected(bool on) {
  if (on == prefs_.highlight_corrected) return false;
  prefs_.highlight_corrected = on;
  return persist_and_bump();
}

bool Model::set_filter(MessageFilter filter) {
  if (filter >= MessageFilter::count || filter == prefs_.filter) return false;
  prefs_.filter = filter;
  message_offset_ = 0;
  bump_messages();
  return persist_and_bump();
}

bool Model::set_sort(AircraftSort sort) {
  if (sort >= AircraftSort::count || sort == prefs_.sort) return false;
  prefs_.sort = sort;
  aircraft_offset_ = 0;
  bump_messages();
  return persist_and_bump();
}

bool Model::set_custom_frequency(uint32_t hz) {
  if (hz != 0 && (hz < kCustomMinHz || hz > kCustomMaxHz || hz % kCustomStepHz != 0)) return false;
  if (hz == prefs_.custom_hz) return false;
  prefs_.custom_hz = hz;
  if (tune_ != TuneStatus::unavailable) tune_ = TuneStatus::pending;
  return persist_and_bump();
}

bool Model::scroll_messages(int rows) {
  const long target = static_cast<long>(message_offset_) + rows;
  const size_t next = target < 0 ? 0 : static_cast<size_t>(target);
  const Snapshot& s = snapshot();
  const size_t limit = s.matching_count > kMessageRows ? s.matching_count - kMessageRows : 0;
  const size_t clamped = std::min(next, limit);
  if (clamped == message_offset_) return false;
  message_offset_ = clamped;
  bump_messages();
  return true;
}

bool Model::scroll_aircraft(int rows) {
  const long target = static_cast<long>(aircraft_offset_) + rows;
  const size_t next = target < 0 ? 0 : static_cast<size_t>(target);
  const Snapshot& s = snapshot();
  const size_t limit = s.aircraft_total > kAircraftRows ? s.aircraft_total - kAircraftRows : 0;
  const size_t clamped = std::min(next, limit);
  if (clamped == aircraft_offset_) return false;
  aircraft_offset_ = clamped;
  bump_messages();
  return true;
}

uint32_t Model::neighbor_seq(uint32_t seq, bool newer) const {
  if (!demo_ && archive_ && seq >= 0x80000000u) return archive_->neighbor(seq, newer);
  const Ring& ring = active();
  uint32_t best = 0;
  for (size_t i = 0; i < ring.held; ++i) {
    const MessageRecord& m = ring.records[i];
    if (prefs_.filter == MessageFilter::corrected_only && m.integrity != Integrity::crc_corrected) continue;
    if (newer && m.seq > seq && (best == 0 || m.seq < best)) best = m.seq;
    if (!newer && m.seq < seq && m.seq > best) best = m.seq;
  }
  return best;
}

size_t Model::archive_query(const ArchiveQuery& q, size_t offset, const MessageRecord** out, size_t capacity,
                            size_t* total) const {
  if (total) *total = 0;
  if (!demo_ && archive_) return archive_->query(q, offset, out, capacity, total);
  if (!demo_ || !out) return 0;
  size_t matched = 0, written = 0;
  for (size_t k = 0; k < demo_store_.held; ++k) {
    const MessageRecord& m = demo_store_.records[(demo_store_.head + kHistoryCapacity - 1 - k) % kHistoryCapacity];
    if (q.tail[0] && std::strcmp(q.tail, m.tail) != 0) continue;
    if (q.label[0] && std::strcmp(q.label, m.label) != 0) continue;
    if (matched++ < offset) continue;
    if (written < capacity) out[written++] = &m;
  }
  if (total) *total = matched;
  return written;
}

bool Model::archive_cycle_tail(char* tail) const {
  if (!demo_ && archive_) return archive_->cycle_tail(tail);
  if (!demo_ || !tail) return false;
  const Ring& r = demo_store_;
  // Aircraft with no address field cannot be selected by tail; they are skipped (they match "any").
  size_t next = r.aircraft_used;                 // index after the current tail, or aircraft_used = none
  size_t start = 0;
  if (tail[0]) {
    for (size_t i = 0; i < r.aircraft_used; ++i)
      if (std::strcmp(r.aircraft[i].tail, tail) == 0) { start = i + 1; break; }
  }
  for (size_t i = start; i < r.aircraft_used; ++i)
    if (r.aircraft[i].tail[0]) { next = i; break; }
  if (next < r.aircraft_used) std::snprintf(tail, 8, "%s", r.aircraft[next].tail);
  else tail[0] = '\0';
  return true;
}

bool Model::archive_cycle_label(char* label) const {
  if (!demo_ && archive_) return archive_->cycle_label(label);
  if (!demo_ || !label) return false;
  const Ring& r = demo_store_;
  char seen[kHistoryCapacity][3];
  size_t n = 0;
  for (size_t k = 0; k < r.held; ++k) {            // oldest first, first appearance order
    const MessageRecord& m = r.records[(r.head + kHistoryCapacity - r.held + k) % kHistoryCapacity];
    bool dup = false;
    for (size_t i = 0; i < n && !dup; ++i) dup = std::strcmp(seen[i], m.label) == 0;
    if (!dup) std::snprintf(seen[n++], 3, "%s", m.label);
  }
  if (n == 0) { label[0] = '\0'; return false; }
  if (!label[0]) { std::snprintf(label, 3, "%s", seen[0]); return true; }
  for (size_t i = 0; i < n; ++i)
    if (std::strcmp(seen[i], label) == 0) {
      if (i + 1 < n) std::snprintf(label, 3, "%s", seen[i + 1]);
      else label[0] = '\0';
      return true;
    }
  std::snprintf(label, 3, "%s", seen[0]);
  return true;
}

void Model::report_tuned(uint32_t frequency_hz, bool ok) {
  tuned_hz_ = ok ? frequency_hz : 0;
  tune_ = ok ? TuneStatus::tuned : TuneStatus::failed;
  bump_status();
}

void Model::clear_tune() {
  if (tune_ == TuneStatus::unavailable && tuned_hz_ == 0) return;
  tuned_hz_ = 0;
  tune_ = TuneStatus::unavailable;
  bump_status();
}

bool Model::inject_demo(const MessageRecord& record) {
  if (!demo_) return false;
  MessageRecord r = record;
  r.source = SourceKind::demo;
  r.channel_index = static_cast<int8_t>(channel_index_for(r.frequency_hz));
  store_record(demo_store_, r);
  return true;
}

void Model::inject_demo_stats(const acars::Stats& stats) {
  if (!demo_) return;
  demo_store_.stats = stats;
  demo_store_.stats_available = true;
}

void Model::inject_demo_receiver(ReceiverState state) {
  if (demo_) demo_receiver_ = state;
}

const MessageRecord* Model::find_message(uint32_t seq) const {
  if (seq == 0) return nullptr;
  const Ring& ring = active();
  for (size_t i = 0; i < ring.held; ++i)
    if (ring.records[i].seq == seq) return &ring.records[i];
  if (!demo_ && archive_) return archive_->find(seq);   // a block opened from the ARCHIVE tab
  return nullptr;
}

void Model::set_archive(const ArchiveSource* archive) {
  archive_ = archive;
  bump_status();
}

void Model::set_live_sink(LiveSink sink, void* context) {
  live_sink_ = sink;
  live_sink_context_ = context;
}

size_t Model::aircraft_messages(const char* tail, const MessageRecord** out, size_t capacity) const {
  if (!tail || !out) return 0;
  const Ring& ring = active();
  for (size_t i = 0; i < ring.aircraft_used; ++i) {
    if (std::strcmp(ring.aircraft[i].tail, tail) != 0) continue;
    size_t n = 0;
    for (size_t k = 0; k < ring.aircraft[i].history_count && n < capacity; ++k) {
      const MessageRecord* m = find_message(ring.aircraft[i].history_seq[k]);
      if (m) out[n++] = m;                      // evicted references are skipped, not invented
    }
    return n;
  }
  return 0;
}

void Model::rebuild() {
  Snapshot& s = snapshot_;
  const Ring& ring = active();
  s.source = demo_ ? SourceKind::demo : SourceKind::live;
  s.scenario = scenario_;
  s.receiver = demo_ ? demo_receiver_ : seen_receiver_;
  s.caps = unpack(seen_caps_);
  s.prefs = prefs_;
  s.stats = ring.stats;
  s.stats_available = ring.stats_available;
  s.queue_drops = demo_ ? 0 : seen_drops_;
  s.messages_held = static_cast<uint32_t>(ring.held);
  s.messages_total = ring.total;
  s.corrected_total = ring.corrected;
  s.custom_active = prefs_.custom_hz != 0;
  s.requested_hz = s.custom_active ? prefs_.custom_hz : preset(prefs_.region, prefs_.preset).frequency_hz;
  s.tuned_hz = demo_ ? 0 : tuned_hz_;
  s.tune = (demo_ || !s.caps.live_reception) ? TuneStatus::unavailable : tune_ == TuneStatus::unavailable ? TuneStatus::pending : tune_;
  s.battery_percent = battery_;
  s.sound_enabled = sound_;
  s.archive_available = !demo_ && archive_ != nullptr;     // a real saved archive; the synthetic DEMO store does not count
  s.archive_count = s.archive_available ? archive_->count() : 0;
  if (s.archive_available) s.caps.archive = true;

  // Messages: newest first, filter applied.
  s.message_count = 0;
  s.matching_count = 0;
  for (size_t k = 0; k < ring.held; ++k) {
    const MessageRecord& m = ring.records[(ring.head + kHistoryCapacity - 1 - k) % kHistoryCapacity];
    if (prefs_.filter == MessageFilter::corrected_only && m.integrity != Integrity::crc_corrected) continue;
    ++s.matching_count;
  }
  const size_t message_limit = s.matching_count > kMessageRows ? s.matching_count - kMessageRows : 0;
  if (message_offset_ > message_limit) message_offset_ = message_limit;
  s.message_offset = message_offset_;
  size_t matched = 0;
  for (size_t k = 0; k < ring.held && s.message_count < kMessageRows; ++k) {
    const MessageRecord& m = ring.records[(ring.head + kHistoryCapacity - 1 - k) % kHistoryCapacity];
    if (prefs_.filter == MessageFilter::corrected_only && m.integrity != Integrity::crc_corrected) continue;
    if (matched++ < message_offset_) continue;
    s.messages[s.message_count++] = m;
  }
  for (size_t i = s.message_count; i < kMessageRows; ++i) s.messages[i] = MessageRecord{};

  // Aircraft: order the table by the chosen sort, then take the top rows.
  size_t order[kAircraftTable];
  const size_t used = ring.aircraft_used;
  for (size_t i = 0; i < used; ++i) order[i] = i;
  auto before = [&](size_t a, size_t b) {
    const AircraftRecord& x = ring.aircraft[a];
    const AircraftRecord& y = ring.aircraft[b];
    if (prefs_.sort == AircraftSort::most_messages && x.message_count != y.message_count)
      return x.message_count > y.message_count;
    return x.history_seq[0] > y.history_seq[0];
  };
  for (size_t i = 1; i < used; ++i) {           // insertion sort: at most 16 entries
    const size_t v = order[i];
    size_t j = i;
    for (; j > 0 && before(v, order[j - 1]); --j) order[j] = order[j - 1];
    order[j] = v;
  }
  s.aircraft_total = used;
  const size_t aircraft_limit = used > kAircraftRows ? used - kAircraftRows : 0;
  if (aircraft_offset_ > aircraft_limit) aircraft_offset_ = aircraft_limit;
  s.aircraft_offset = aircraft_offset_;
  s.aircraft_count = std::min(used - aircraft_offset_, kAircraftRows);
  for (size_t i = 0; i < kAircraftRows; ++i)
    s.aircraft[i] = i < s.aircraft_count ? ring.aircraft[order[aircraft_offset_ + i]] : AircraftRecord{};

  // Channels: the region's presets. Only the request is known; nothing is "receiving".
  const size_t n = preset_count(prefs_.region);
  s.channel_count = n;
  for (size_t i = 0; i < kChannelRows; ++i) {
    ChannelRecord c;
    if (i < n) {
      const Preset& p = preset(prefs_.region, i);
      c.frequency_hz = p.frequency_hz;
      std::snprintf(c.label, sizeof(c.label), "%s", p.label);
      c.selected = i == prefs_.preset;
      for (size_t k = 0; k < ring.held; ++k)
        if (ring.records[k].frequency_hz == p.frequency_hz) ++c.decoded_messages;
    }
    s.channels[i] = c;
  }
  s.messages_revision = messages_revision_;
  s.status_revision = status_revision_;
  s.revision = ++revision_;
  dirty_ = false;
}

const Snapshot& Model::snapshot() {
  if (dirty_) rebuild();
  return snapshot_;
}

bool model_self_check() {
  char buf[16];
  const char text[] = {'A', 0, '\r', 0x03, 'z'};
  bool truncated = false;
  if (sanitize_preview(buf, sizeof(buf), text, sizeof(text), &truncated) != 5 || truncated) return false;
  if (std::strcmp(buf, "A. .z") != 0) return false;   // NUL -> '.', CR -> ' ', 0x03 -> '.'
  for (int r = 0; r < static_cast<int>(Region::count); ++r) {
    const Region region = static_cast<Region>(r);
    if (preset_count(region) == 0 || preset_count(region) > kChannelRows) return false;
  }
  return pack(unpack(0x7f)) == 0x7f && pack(Capabilities{}) == 0;
}

}  // namespace orcsdr::acars_ui
