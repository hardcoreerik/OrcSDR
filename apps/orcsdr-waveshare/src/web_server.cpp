#include "web_server.hpp"

#include "web_api.hpp"
#include "web_ui_page.h"
#include "web_ui_fm.h"
#include "fm_pcm.hpp"
#include "rtl_sdr_v4_esp.h"

#include <Arduino.h>
#include <ETH.h>
#include <Network.h>
#include <WebServer.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace orcsdr::web {
namespace {

/* Waveshare ESP32-P4-Module-DEV-KIT Ethernet (same as OrcLink waveshare-p4). */
constexpr int kEthernetPhyAddress = 1;
constexpr int kEthernetMdc = 31;
constexpr int kEthernetMdio = 52;
constexpr int kEthernetReset = 51;

WebServer server(80);
bool eth_started = false;
bool eth_link = false;
bool eth_has_ip = false;
bool http_started = false;
char ip_text[16] = "0.0.0.0";

volatile bool evt_link = false;
volatile bool evt_ip = false;
volatile bool evt_lost = false;

void on_network_event(arduino_event_id_t event) {
  switch (event) {
    case ARDUINO_EVENT_ETH_CONNECTED:
      evt_link = true;
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      evt_ip = true;
      break;
    case ARDUINO_EVENT_ETH_LOST_IP:
    case ARDUINO_EVENT_ETH_DISCONNECTED:
    case ARDUINO_EVENT_ETH_STOP:
      evt_lost = true;
      break;
    default:
      break;
  }
}

void json_escape(const char* in, char* out, size_t out_size) {
  size_t o = 0;
  for (size_t i = 0; in[i] != '\0' && o + 2 < out_size; ++i) {
    const char c = in[i];
    if (c == '"' || c == '\\') {
      if (o + 3 >= out_size) break;
      out[o++] = '\\';
      out[o++] = c;
    } else if (static_cast<unsigned char>(c) < 0x20) {
      continue;
    } else {
      out[o++] = c;
    }
  }
  out[o] = '\0';
}

void handle_root() {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html", kWebUiIndexHtml);
}

void handle_state() {
  Snapshot snap{};
  copy_snapshot(&snap);

  /* Build JSON in a heap buffer — aircraft + FM spectrum. */
  constexpr size_t kCap = 12288;
  char* json = static_cast<char*>(malloc(kCap));
  if (!json) {
    server.send(500, "text/plain", "oom");
    return;
  }

  char esc_product[64]{};
  char esc_status[64]{};
  char esc_mode[24]{};
  json_escape(snap.product, esc_product, sizeof(esc_product));
  json_escape(snap.status, esc_status, sizeof(esc_status));
  json_escape(snap.mode, esc_mode, sizeof(esc_mode));

  int n = snprintf(
      json, kCap,
      "{"
      "\"mode\":\"%s\","
      "\"status\":\"%s\","
      "\"product\":\"%s\","
      "\"ip\":\"%s\","
      "\"eth_link\":%s,"
      "\"rtl_ready\":%s,"
      "\"streaming\":%s,"
      "\"frequency_hz\":%u,"
      "\"sample_rate_sps\":%u,"
      "\"effective_sps\":%u,"
      "\"iq_drops\":%u,"
      "\"free_heap\":%u,"
      "\"adsb\":{"
      "\"live\":%s,"
      "\"total_messages\":%u,"
      "\"total_crc_ok\":%u,"
      "\"preambles\":%u,"
      "\"candidate_frames\":%u,"
      "\"df17\":%u,"
      "\"mag_min\":%u,"
      "\"mag_max\":%u,"
      "\"message_rate\":%.2f,"
      "\"strongest_signal_dbfs\":%.1f,"
      "\"aircraft_count\":%u,"
      "\"revision\":%u,"
      "\"aircraft\":[",
      esc_mode, esc_status, esc_product, snap.ip,
      snap.eth_link ? "true" : "false", snap.rtl_ready ? "true" : "false",
      snap.streaming ? "true" : "false", snap.frequency_hz, snap.sample_rate_sps,
      snap.effective_sps, snap.iq_drops, snap.free_heap,
      snap.live ? "true" : "false", snap.total_messages, snap.total_crc_ok,
      snap.adsb_preambles, snap.adsb_frames, snap.adsb_df17, snap.adsb_mag_min,
      snap.adsb_mag_max, static_cast<double>(snap.message_rate),
      static_cast<double>(snap.strongest_signal_dbfs), snap.aircraft_count,
      snap.revision);

  for (uint8_t i = 0; i < snap.aircraft_count && n > 0 && static_cast<size_t>(n) + 400 < kCap;
       ++i) {
    const Aircraft& a = snap.aircraft[i];
    char call[16]{};
    char reg[16]{};
    char type[64]{};
    char owner[64]{};
    json_escape(a.callsign, call, sizeof(call));
    json_escape(a.registration, reg, sizeof(reg));
    json_escape(a.type, type, sizeof(type));
    json_escape(a.owner, owner, sizeof(owner));
    n += snprintf(
        json + n, kCap - static_cast<size_t>(n),
        "%s{"
        "\"icao\":%u,"
        "\"callsign\":\"%s\","
        "\"registration\":\"%s\","
        "\"type\":\"%s\","
        "\"owner\":\"%s\","
        "\"altitude_ft\":%d,"
        "\"speed_kts\":%d,"
        "\"heading_deg\":%d,"
        "\"vertical_rate_fpm\":%d,"
        "\"latitude\":%.5f,"
        "\"longitude\":%.5f,"
        "\"signal_dbfs\":%.1f,"
        "\"has_callsign\":%s,"
        "\"has_altitude\":%s,"
        "\"has_speed\":%s,"
        "\"has_heading\":%s,"
        "\"has_vertical_rate\":%s,"
        "\"has_position\":%s,"
        "\"messages\":%u,"
        "\"age_ms\":%u"
        "}",
        i ? "," : "", a.icao, call, reg, type, owner, a.altitude_ft, a.speed_kts,
        a.heading_deg, a.vertical_rate_fpm, static_cast<double>(a.latitude),
        static_cast<double>(a.longitude), static_cast<double>(a.signal_dbfs),
        a.has_callsign ? "true" : "false", a.has_altitude ? "true" : "false",
        a.has_speed ? "true" : "false", a.has_heading ? "true" : "false",
        a.has_vertical_rate ? "true" : "false", a.has_position ? "true" : "false",
        a.messages, a.age_ms);
  }

  if (n > 0 && static_cast<size_t>(n) + 32 < kCap) {
    n += snprintf(json + n, kCap - static_cast<size_t>(n),
                  "]},"
                  "\"fm\":{"
                  "\"signal_dbfs\":%.1f,"
                  "\"pcm_underruns\":%u,"
                  "\"pcm_overruns\":%u,"
                  "\"pcm_available\":%u,"
                  "\"pcm_sequence\":%u,"
                  "\"pcm_rate_hz\":%u,"
                  "\"spectrum\":[",
                  static_cast<double>(snap.fm_signal_dbfs), snap.pcm_underruns,
                  snap.pcm_overruns, snap.pcm_available, snap.pcm_sequence,
                  static_cast<unsigned>(orcsdr::fm::kPcmRateHz));
  }
  const uint8_t bins = snap.fm_spectrum_bins ? snap.fm_spectrum_bins : 64;
  for (uint8_t i = 0; i < bins && n > 0 && static_cast<size_t>(n) + 24 < kCap; ++i) {
    n += snprintf(json + n, kCap - static_cast<size_t>(n), "%s%.4f", i ? "," : "",
                  static_cast<double>(snap.fm_spectrum[i]));
  }
  if (n > 0 && static_cast<size_t>(n) + 8 < kCap) {
    n += snprintf(json + n, kCap - static_cast<size_t>(n), "]}}");
  }

  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
  free(json);
}

void handle_fm_page() {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html", kWebUiFmHtml);
}

void handle_audio() {
  size_t max_samples = 4096;
  if (server.hasArg("max")) {
    max_samples = static_cast<size_t>(constrain(server.arg("max").toInt(), 256, 8192));
  }
  int16_t* pcm = static_cast<int16_t*>(malloc(max_samples * sizeof(int16_t)));
  if (!pcm) {
    server.send(500, "text/plain", "oom");
    return;
  }
  const size_t count = orcsdr::fm::pull_pcm(pcm, max_samples);
  const size_t bytes = 16 + count * sizeof(int16_t);
  uint8_t* packet = static_cast<uint8_t*>(malloc(bytes));
  if (!packet) {
    free(pcm);
    server.send(500, "text/plain", "oom");
    return;
  }
  packet[0] = 'P';
  packet[1] = 'C';
  packet[2] = 'M';
  packet[3] = '1';
  auto put_u32 = [](uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v & 0xff);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xff);
    p[2] = static_cast<uint8_t>((v >> 16) & 0xff);
    p[3] = static_cast<uint8_t>((v >> 24) & 0xff);
  };
  put_u32(packet + 4, orcsdr::fm::kPcmRateHz);
  put_u32(packet + 8, orcsdr::fm::pcm_sequence());
  put_u32(packet + 12, static_cast<uint32_t>(count));
  if (count) std::memcpy(packet + 16, pcm, count * sizeof(int16_t));
  free(pcm);

  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.setContentLength(bytes);
  server.send(200, "application/octet-stream", "");
  server.sendContent(reinterpret_cast<const char*>(packet), bytes);
  free(packet);
}

void handle_location() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "POST only");
    return;
  }
  const String body = server.arg("plain");
  /* Minimal parse: "latitude":1.2, "longitude":-3.4, "radar_range_nm":25 */
  double lat = 0, lon = 0;
  int range = 25;
  const char* s = body.c_str();
  const char* p = strstr(s, "\"latitude\"");
  if (p) lat = strtod(strchr(p, ':') + 1, nullptr);
  p = strstr(s, "\"longitude\"");
  if (p) lon = strtod(strchr(p, ':') + 1, nullptr);
  p = strstr(s, "\"radar_range_nm\"");
  if (p) range = atoi(strchr(p, ':') + 1);
  const bool ok = lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0 && range >= 5 &&
                  range <= 250;
  if (!ok) {
    server.send(400, "application/json", "{\"ok\":false}");
    return;
  }
  set_receiver_location(true, lat, lon, static_cast<uint16_t>(range));
  Serial.printf("WEB_LOCATION lat=%.7f lon=%.7f range_nm=%d\n", lat, lon, range);
  server.send(200, "application/json", "{\"ok\":true}");
}

/* Mode changes are requested via a one-slot flag read by main. */
volatile uint8_t g_mode_request = 0;  // 0=none, 1=ADSB, 2=FM, 3=WX
volatile uint32_t g_freq_request_hz = 0;

void handle_mode() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "POST only");
    return;
  }
  const String body = server.arg("plain");
  if (body.indexOf("ADSB") >= 0) g_mode_request = 1;
  else if (body.indexOf("\"FM\"") >= 0 || body.indexOf(":\"FM") >= 0) g_mode_request = 2;
  else if (body.indexOf("WX") >= 0) g_mode_request = 3;
  else {
    server.send(400, "application/json", "{\"ok\":false}");
    return;
  }
  Serial.printf("WEB_MODE_REQUEST id=%u\n", g_mode_request);
  server.send(200, "application/json", "{\"ok\":true}");
}

void handle_freq() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "POST only");
    return;
  }
  const String body = server.arg("plain");
  const char* p = strstr(body.c_str(), "frequency_hz");
  if (!p) {
    server.send(400, "application/json", "{\"ok\":false}");
    return;
  }
  const char* colon = strchr(p, ':');
  if (!colon) {
    server.send(400, "application/json", "{\"ok\":false}");
    return;
  }
  const uint32_t hz = static_cast<uint32_t>(strtoul(colon + 1, nullptr, 10));
  if (hz < RTL_SDR_V4_ESP_FREQ_MIN_HZ || hz > RTL_SDR_V4_ESP_FREQ_MAX_HZ) {
    server.send(400, "application/json", "{\"ok\":false}");
    return;
  }
  g_freq_request_hz = hz;
  Serial.printf("WEB_FREQ_REQUEST frequency_hz=%u\n", hz);
  server.send(200, "application/json", "{\"ok\":true}");
}

/* 1=start, 2=stop */
volatile uint8_t g_stream_request = 0;

void handle_stream_control() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "POST only");
    return;
  }
  const String body = server.arg("plain");
  if (body.indexOf("start") >= 0 || body.indexOf("START") >= 0) g_stream_request = 1;
  else if (body.indexOf("stop") >= 0 || body.indexOf("STOP") >= 0) g_stream_request = 2;
  else {
    server.send(400, "application/json", "{\"ok\":false}");
    return;
  }
  Serial.printf("WEB_STREAM_REQUEST id=%u\n", g_stream_request);
  server.send(200, "application/json", "{\"ok\":true}");
}

void handle_not_found() { server.send(404, "text/plain", "not found"); }

void start_http_if_needed() {
  if (http_started) return;
  server.on("/", HTTP_GET, handle_root);
  server.on("/index.html", HTTP_GET, handle_root);
  server.on("/fm", HTTP_GET, handle_fm_page);
  server.on("/fm.html", HTTP_GET, handle_fm_page);
  server.on("/api/state", HTTP_GET, handle_state);
  server.on("/api/audio", HTTP_GET, handle_audio);
  server.on("/api/location", HTTP_POST, handle_location);
  server.on("/api/mode", HTTP_POST, handle_mode);
  server.on("/api/freq", HTTP_POST, handle_freq);
  server.on("/api/stream", HTTP_POST, handle_stream_control);
  server.onNotFound(handle_not_found);
  server.begin();
  http_started = true;
  Serial.println("HTTP_SERVER started port=80 paths=/ /fm /api/state /api/audio /api/stream");
}

}  // namespace

uint8_t take_mode_request() {
  const uint8_t v = g_mode_request;
  g_mode_request = 0;
  return v;
}

uint32_t take_freq_request() {
  const uint32_t v = g_freq_request_hz;
  g_freq_request_hz = 0;
  return v;
}

uint8_t take_stream_request() {
  const uint8_t v = g_stream_request;
  g_stream_request = 0;
  return v;
}

bool begin_network_and_http() {
  Network.onEvent(on_network_event);
  eth_started = ETH.begin(ETH_PHY_IP101, kEthernetPhyAddress, kEthernetMdc, kEthernetMdio,
                          kEthernetReset, EMAC_CLK_EXT_IN);
  Serial.printf("ETH_INIT started=%s phy=IP101 mdc=%d mdio=%d reset=%d\n",
                eth_started ? "true" : "false", kEthernetMdc, kEthernetMdio, kEthernetReset);
  return eth_started;
}

void poll_network_and_http() {
  if (evt_lost) {
    evt_lost = false;
    eth_link = false;
    eth_has_ip = false;
    strlcpy(ip_text, "0.0.0.0", sizeof(ip_text));
    Serial.println("ETH_STATUS link=false");
  }
  if (evt_link) {
    evt_link = false;
    eth_link = true;
    Serial.println("ETH_STATUS link=true dhcp=pending");
  }
  if (evt_ip) {
    evt_ip = false;
    eth_link = true;
    eth_has_ip = true;
    const IPAddress ip = ETH.localIP();
    snprintf(ip_text, sizeof(ip_text), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    Serial.printf("ETH_STATUS link=true ip=%s speed_mbps=%u\n", ip_text, ETH.linkSpeed());
    start_http_if_needed();
  }

  if (http_started) server.handleClient();
}

const char* local_ip() { return ip_text; }
bool ethernet_link_up() { return eth_link; }
bool http_ready() { return http_started && eth_has_ip; }

}  // namespace orcsdr::web
