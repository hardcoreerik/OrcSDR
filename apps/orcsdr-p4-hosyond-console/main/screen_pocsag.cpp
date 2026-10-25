// POCSAG pager dashboard (apps/orcsdr-tab5/ui/pocsag_dashboard.cpp and pocsag_decoder_core.cpp, unchanged) on
// the P4 console. The decoder takes the 960 kS/s IQ directly; messages are kept in a bounded RAM ring.
// Pager discovery scan, the SD archive and identity editing are not available on this target yet.
#include <algorithm>
#include <cstring>
#include "audio_service.hpp"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "home_app.hpp"
#include "iq_tap.hpp"
#include "pocsag_dashboard.hpp"
#include "pocsag_decoder_core.hpp"
#include "pocsag_store.hpp"
#include "radio_service.hpp"
#include "screens.hpp"

namespace {
namespace pg = orcsdr::pocsag;
constexpr uint32_t kDefaultHz = 152007500;
constexpr size_t kRing = 24;

struct Stored {
  bool used = false;
  uint64_t timestamp_ms = 0;
  pg::Message msg;
};

pg::Decoder* g_decoder = nullptr;
orcsdr::pocsag_store::Table* g_table = nullptr;
Stored* g_ring = nullptr;
size_t g_write = 0, g_count = 0;
uint32_t g_revision = 0, g_ui_revision = 0;
portMUX_TYPE g_lock = portMUX_INITIALIZER_UNLOCKED;
bool g_started = false;

uint64_t now_ms() { return static_cast<uint64_t>(esp_timer_get_time() / 1000); }

void on_message(const pg::Message& m, void*) {
  portENTER_CRITICAL(&g_lock);
  Stored& s = g_ring[g_write];
  s.used = true;
  s.timestamp_ms = now_ms();
  s.msg = m;
  g_write = (g_write + 1) % kRing;
  if (g_count < kRing) ++g_count;
  g_table->record_hit(m.capcode, s.timestamp_ms);
  ++g_revision;
  portEXIT_CRITICAL(&g_lock);
}

void consume(const uint8_t* iq, size_t bytes, void*) { g_decoder->process_cu8(iq, bytes, on_message, nullptr); }

void apply_settings() {
  const pg::Settings& s = pg::settings();
  pg::Baud baud = pg::Baud::auto_detect;
  if (s.baud_bps == 512) baud = pg::Baud::b512;
  else if (s.baud_bps == 1200) baud = pg::Baud::b1200;
  else if (s.baud_bps == 2400) baud = pg::Baud::b2400;
  g_decoder->configure(baud, s.polarity_mode == 1 ? pg::Polarity::normal : s.polarity_mode == 2 ? pg::Polarity::inverted : pg::Polarity::auto_detect);
  if (s.frequency_hz) home_app::request_frequency(s.frequency_hz);
}

void publish(bool force) {
  static pg::Snapshot* snap = nullptr;
  if (!snap) snap = static_cast<pg::Snapshot*>(heap_caps_calloc(1, sizeof(pg::Snapshot), MALLOC_CAP_SPIRAM));
  if (!snap) return;
  if (!force && g_revision == g_ui_revision) {
    // Still refresh the decoder statistics and tuned frequency for the SIGNAL view.
  }
  *snap = pg::Snapshot{};
  portENTER_CRITICAL(&g_lock);
  const size_t count = std::min(g_count, pg::kRecentMessageCapacity);
  for (size_t i = 0; i < count; ++i) {
    const Stored& st = g_ring[(g_write + kRing - 1 - i) % kRing];
    if (!st.used) continue;
    pg::DisplayMessage& out = snap->messages[snap->message_count++];
    out.timestamp_ms = st.timestamp_ms;
    out.capcode = st.msg.capcode;
    out.function = st.msg.function;
    out.type = st.msg.type;
    out.baud = st.msg.baud;
    out.inverted = st.msg.inverted;
    out.truncated = st.msg.truncated;
    out.corrected_bits = st.msg.corrected_bits;
    out.uncorrectable_words = st.msg.uncorrectable_words;
    std::memcpy(out.text, st.msg.text, sizeof(out.text));
    out.text_length = st.msg.text_length;
  }
  const size_t ids = std::min(g_table->count(), pg::kIdentityCapacity);
  for (size_t i = 0; i < ids; ++i) {
    const auto* id = g_table->at(i);
    if (!id) continue;
    pg::IdentitySummary& o = snap->identities[snap->identity_count++];
    o.capcode = id->capcode;
    std::snprintf(o.alias, sizeof(o.alias), "%s", id->alias);
    std::snprintf(o.group, sizeof(o.group), "%s", id->group);
    o.watched = id->watched;
    o.muted = id->muted;
    o.hit_count = id->hit_count;
    o.last_seen_ms = id->last_seen_ms;
  }
  portEXIT_CRITICAL(&g_lock);
  snap->decoder_stats = g_decoder->stats();
  snap->receiving = orcsdr::radio::snapshot().receiver_state == 2;
  snap->frequency_hz = home_app::status().frequency_hz;
  snap->candidate_count = 0;
  snap->revision = ++g_ui_revision;
  pg::set_live_snapshot(*snap);
}

void enter() {
  if (!g_started) {
    g_decoder = static_cast<pg::Decoder*>(heap_caps_malloc(sizeof(pg::Decoder), MALLOC_CAP_SPIRAM));
    new (g_decoder) pg::Decoder();
    g_table = static_cast<orcsdr::pocsag_store::Table*>(heap_caps_malloc(sizeof(orcsdr::pocsag_store::Table), MALLOC_CAP_SPIRAM));
    new (g_table) orcsdr::pocsag_store::Table();
    g_ring = static_cast<Stored*>(heap_caps_calloc(kRing, sizeof(Stored), MALLOC_CAP_SPIRAM));
    g_started = g_decoder && g_table && g_ring;
    if (!g_started) {
      home_app::request_home();
      return;
    }
  }
  pg::Settings s;
  s.frequency_hz = kDefaultHz;
  pg::enter(s);
  g_decoder->reset();
  apply_settings();
  audio_service::set_active(false);
  iq_tap::start("pocsag", consume, nullptr);
  publish(true);
}
void leave() {
  iq_tap::stop();
  pg::leave();
}
void tick() {
  publish(false);
  pg::update();
}

void tap(int x, int y) {
  switch (pg::handle_touch(x, y)) {
    case pg::Action::settings_changed: apply_settings(); break;
    case pg::Action::exit: home_app::request_home(); break;
    default: break;  // pager discovery scan: not available on this target yet
  }
}
}  // namespace

namespace screens {
const Screen& pocsag_screen() {
  static const Screen s{orcsdr::dashboards::Id::pocsag, "POCSAG", enter, leave, tick, tap, nullptr, nullptr};
  return s;
}
}  // namespace screens
