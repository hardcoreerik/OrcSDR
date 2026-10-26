// ADS-B dashboard (apps/orcsdr-tab5/ui/adsb_dashboard.cpp and adsb_decoder.cpp, unchanged) on the P4 console. The
// decoder takes the 2.4 MS/s IQ on the 1090 MHz tune; aircraft are tracked in a bounded table (32) and expire after 60 s.
// FAA/airline metadata (registration, type, owner) comes from the SD catalog on the Tab5 and is not available here yet.
#include <algorithm>
#include <cstring>
#include "adsb_dashboard.hpp"
#include "adsb_decoder.hpp"
#include "audio_service.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "home_app.hpp"
#include "iq_tap.hpp"
#include "radio_service.hpp"
#include "screens.hpp"

namespace {
namespace ad = orcsdr::adsb;
namespace rx = orcsdr::adsb_rx;
constexpr size_t kTracks = 32;
constexpr uint32_t kAdsbHz = 1090000000;

struct Track {
  bool used = false;
  uint32_t icao = 0;
  char callsign[9] = {};
  bool has_callsign = false;
  int altitude_ft = 0;
  bool has_altitude = false;
  int speed_kts = 0;
  bool has_speed = false;
  int heading_deg = 0;
  bool has_heading = false;
  int vertical_rate_fpm = 0;
  bool has_vertical_rate = false;
  double latitude = 0, longitude = 0;
  bool has_position = false;
  rx::Frame even_cpr, odd_cpr;
  uint32_t even_cpr_ms = 0, odd_cpr_ms = 0, last_seen_ms = 0;
  uint16_t signal = 0;
};

rx::Decoder* g_decoder = nullptr;
Track* g_tracks = nullptr;
portMUX_TYPE g_lock = portMUX_INITIALIZER_UNLOCKED;
uint32_t g_messages = 0;
uint32_t g_count_last = 0;
uint32_t g_last_sample_ms = 0, g_last_sample_messages = 0;
float g_rate = 0;
uint32_t g_ui_revision = 0;
ad::Settings g_settings;
bool g_started = false;

uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

void on_frame(const rx::Frame& frame, void*) {
  const uint32_t now = now_ms();
  rx::Frame even{}, odd{};
  bool decode_position = false, use_odd = false;
  portENTER_CRITICAL(&g_lock);
  Track* track = nullptr;
  Track* oldest = &g_tracks[0];
  for (size_t i = 0; i < kTracks; ++i) {
    Track& c = g_tracks[i];
    if (c.used && c.icao == frame.icao) {
      track = &c;
      break;
    }
    if (!c.used) oldest = &c;
    else if (oldest->used && c.last_seen_ms < oldest->last_seen_ms) oldest = &c;
  }
  if (!track) {
    track = oldest;
    *track = Track{};
    track->used = true;
    track->icao = frame.icao;
  }
  track->last_seen_ms = now;
  track->signal = frame.signal;
  if (frame.has_callsign) {
    std::snprintf(track->callsign, sizeof(track->callsign), "%.8s", frame.callsign);
    track->has_callsign = true;
  }
  if (frame.has_altitude) {
    track->altitude_ft = frame.altitude_ft;
    track->has_altitude = true;
  }
  if (frame.has_speed) {
    track->speed_kts = frame.speed_kts;
    track->has_speed = true;
  }
  if (frame.has_heading) {
    track->heading_deg = frame.heading_deg;
    track->has_heading = true;
  }
  if (frame.has_vertical_rate) {
    track->vertical_rate_fpm = frame.vertical_rate_fpm;
    track->has_vertical_rate = true;
  }
  if (frame.has_cpr) {
    if (frame.cpr_odd) {
      track->odd_cpr = frame;
      track->odd_cpr_ms = now;
    } else {
      track->even_cpr = frame;
      track->even_cpr_ms = now;
    }
    const uint32_t age = track->even_cpr_ms > track->odd_cpr_ms ? track->even_cpr_ms - track->odd_cpr_ms : track->odd_cpr_ms - track->even_cpr_ms;
    if (track->even_cpr_ms && track->odd_cpr_ms && age <= 10000u) {
      even = track->even_cpr;
      odd = track->odd_cpr;
      use_odd = track->odd_cpr_ms > track->even_cpr_ms;
      decode_position = true;
    }
  }
  ++g_messages;
  portEXIT_CRITICAL(&g_lock);
  if (decode_position) {
    double lat = 0, lon = 0;
    if (rx::decode_global_cpr(even, odd, use_odd, &lat, &lon)) {
      portENTER_CRITICAL(&g_lock);
      if (track->used && track->icao == frame.icao) {
        track->latitude = lat;
        track->longitude = lon;
        track->has_position = true;
      }
      portEXIT_CRITICAL(&g_lock);
    }
  }
}

void consume(const uint8_t* iq, size_t bytes, void*) { g_decoder->process_cu8(iq, bytes, on_frame, nullptr); }

void publish() {
  const uint32_t now = now_ms();
  if (now - g_last_sample_ms < 1000u) return;
  static ad::Snapshot* snap = nullptr;
  if (!snap) snap = static_cast<ad::Snapshot*>(heap_caps_calloc(1, sizeof(ad::Snapshot), MALLOC_CAP_SPIRAM));
  if (!snap) return;
  uint32_t messages;
  uint8_t count = 0;
  *snap = ad::Snapshot{};
  portENTER_CRITICAL(&g_lock);
  for (size_t i = 0; i < kTracks; ++i) {
    Track& t = g_tracks[i];
    if (t.used && now - t.last_seen_ms > 60000u) t = Track{};
    if (!t.used) continue;
    ++count;
    if (snap->visible_count >= ad::kVisibleAircraft) continue;
    ad::Aircraft& a = snap->aircraft[snap->visible_count++];
    a.icao = t.icao;
    std::snprintf(a.callsign, sizeof(a.callsign), "%.8s", t.callsign);
    a.altitude_ft = t.altitude_ft;
    a.speed_kts = t.speed_kts;
    a.heading_deg = t.heading_deg;
    a.vertical_rate_fpm = t.vertical_rate_fpm;
    a.latitude = static_cast<float>(t.latitude);
    a.longitude = static_cast<float>(t.longitude);
    a.has_callsign = t.has_callsign;
    a.has_altitude = t.has_altitude;
    a.has_speed = t.has_speed;
    a.has_heading = t.has_heading;
    a.has_vertical_rate = t.has_vertical_rate;
    a.has_position = t.has_position;
  }
  messages = g_messages;
  portEXIT_CRITICAL(&g_lock);
  g_rate = (messages - g_last_sample_messages) * 1000.0f / static_cast<float>(now - g_last_sample_ms);
  g_last_sample_messages = messages;
  g_last_sample_ms = now;
  snap->total_messages = messages;
  snap->aircraft_count = count;
  snap->message_rate = g_rate;
  snap->strongest_signal_dbfs = orcsdr::radio::level_dbfs();
  snap->effective_sps = orcsdr::radio::snapshot().effective_sps;
  snap->sound_enabled = false;
  snap->revision = ++g_ui_revision;
  ad::set_live_snapshot(*snap);
}

void enter() {
  if (!g_started) {
    g_decoder = static_cast<rx::Decoder*>(heap_caps_malloc(sizeof(rx::Decoder), MALLOC_CAP_SPIRAM));
    g_tracks = static_cast<Track*>(heap_caps_calloc(kTracks, sizeof(Track), MALLOC_CAP_SPIRAM));
    if (!g_decoder || !g_tracks) {
      home_app::request_home();
      return;
    }
    new (g_decoder) rx::Decoder();
    g_started = true;
  }
  g_decoder->reset();
  std::memset(static_cast<void*>(g_tracks), 0, kTracks * sizeof(Track));
  g_messages = 0;
  g_last_sample_ms = now_ms();
  g_last_sample_messages = 0;
  home_app::request_frequency(kAdsbHz);
  orcsdr::radio::set_sample_rate_override(2400000);  // the decoder is built for 2.4 MS/s
  audio_service::set_active(false);
  g_settings = ad::Settings{};
  g_settings.gain_supported = orcsdr::radio::gain().available;
  g_settings.gain_auto_supported = orcsdr::radio::gain().auto_available;
  g_settings.gain_auto = orcsdr::radio::gain().auto_mode;
  g_settings.gain_tenth_db = static_cast<int16_t>(orcsdr::radio::gain().tenth_db);
  ad::enter(g_settings);
  iq_tap::start("adsb", consume, nullptr, 384 * 1024, 1, 4, 8192);
  publish();
}
void leave() {
  iq_tap::stop();
  orcsdr::radio::set_sample_rate_override(0);
  ad::leave();
}
void log_stats() {
  static uint32_t last_ms = 0;
  const uint32_t now = now_ms();
  if (now - last_ms < 10000) return;
  last_ms = now;
  const rx::Stats& st = g_decoder->stats();
  uint8_t aircraft = 0;
  portENTER_CRITICAL(&g_lock);
  for (size_t i = 0; i < kTracks; ++i) aircraft += g_tracks[i].used ? 1 : 0;
  portEXIT_CRITICAL(&g_lock);
  ESP_LOGI("ADSB", "preambles=%u frames=%u df17=%u crc_ok=%u aircraft=%u messages=%u level=%.1f dBFS gain=%s %.1f dB blocks=%u dropped=%u",
           unsigned(st.preambles), unsigned(st.frames), unsigned(st.df17), unsigned(st.crc_ok), unsigned(aircraft), unsigned(g_messages),
           double(orcsdr::radio::level_dbfs()), orcsdr::radio::gain().auto_mode ? "auto" : "manual", orcsdr::radio::gain().tenth_db / 10.0,
           unsigned(iq_tap::blocks()), unsigned(iq_tap::dropped()));
}

void tick() {
  log_stats();
  publish();
  ad::update();
}

void tap(int x, int y) {
  switch (ad::handle_touch(x, y)) {
    case ad::Action::gain_auto: orcsdr::radio::set_gain_auto(); break;
    case ad::Action::gain_tenth_db: orcsdr::radio::set_gain_manual(ad::gain_tenth_db()); break;
    case ad::Action::open_data_settings: home_app::request_open(orcsdr::dashboards::Id::settings); break;
    case ad::Action::exit: home_app::request_home(); break;
    default: break;  // ATC listen/resume and location-based radar: not available on this target yet
  }
}
}  // namespace

namespace screens {
const Screen& adsb_screen() {
  static const Screen s{orcsdr::dashboards::Id::adsb, "ADSB", enter, leave, tick, tap, nullptr, nullptr};
  return s;
}
}  // namespace screens
