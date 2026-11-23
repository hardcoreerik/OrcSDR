// POCSAG screen: the part of OrcSDR's main.cpp that owns the pager state (message ring, CAPCODE identity table, settings, the
// "FIND PAGERS" channel scan), driven by the node's decode events instead of an on-board decoder.
#include <M5Unified.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "data_screens.hpp"
#include "pocsag_dashboard.hpp"
#include "pocsag_store.hpp"

namespace {

using namespace orcsdr;
using namespace dscreens;

constexpr uint32_t kFallbackHz = 152007500;
// OrcSDR's generic seed list (US paging-only channels, FCC Part 22); a user's own list lives in /orcsdr/pocsag_scan.cfg on the device.
constexpr uint32_t kCandidates[] = {152007500, 152240000, 152480000, 152840000, 157450000, 157740000, 158100000, 158460000, 158700000};
constexpr size_t kCandidateCount = sizeof(kCandidates) / sizeof(kCandidates[0]);
constexpr uint32_t kDwellMs = 4000;
constexpr size_t kStore = 64;

pocsag::Settings g_settings;
pocsag::Snapshot g_snap;
pocsag_store::Table g_ids;
pocsag::DisplayMessage g_ring[kStore];
size_t g_write = 0, g_count = 0;
bool g_receiving = false, g_entered = false;
uint32_t g_last_publish = 0, g_revision = 0;
char g_request[160];
bool g_request_pending = false;

// scan
bool g_scanning = false;
size_t g_scan_index = 0;
uint32_t g_scan_until = 0;
uint32_t g_cand_valid[kCandidateCount], g_cand_corrected[kCandidateCount], g_cand_messages[kCandidateCount];
uint32_t g_live_valid = 0, g_live_corrected = 0, g_live_messages = 0;

void request_start(uint32_t hz) {
  char options[48];
  const char* baud = g_settings.baud_bps == 512 ? "512" : g_settings.baud_bps == 1200 ? "1200" : g_settings.baud_bps == 2400 ? "2400" : "auto";
  const char* pol = g_settings.polarity_mode == 1 ? "normal" : g_settings.polarity_mode == 2 ? "inverted" : "auto";
  snprintf(options, sizeof options, "baud=%s;polarity=%s", baud, pol);
  snprintf(g_request, sizeof g_request, "{\"op\":\"start\",\"hz\":%u,\"options\":\"%s\"}", (unsigned)hz, options);
  g_request_pending = true;
}

void publish(uint32_t now) {
  pocsag::Snapshot s{};
  const size_t n = std::min(g_count, pocsag::kRecentMessageCapacity);
  for (size_t i = 0; i < n; ++i) s.messages[s.message_count++] = g_ring[(g_write + kStore - 1 - i) % kStore];
  const size_t ids = std::min(g_ids.count(), pocsag::kIdentityCapacity);
  for (size_t i = 0; i < ids; ++i) {
    const auto* id = g_ids.at(i);
    if (!id) continue;
    auto& out = s.identities[s.identity_count++];
    out.capcode = id->capcode;
    snprintf(out.alias, sizeof out.alias, "%s", id->alias);
    snprintf(out.group, sizeof out.group, "%s", id->group);
    out.watched = id->watched;
    out.muted = id->muted;
    out.hit_count = id->hit_count;
    out.last_seen_ms = id->last_seen_ms;
  }
  s.decoder_stats = g_snap.decoder_stats;
  s.receiving = g_receiving;
  s.frequency_hz = g_settings.frequency_hz;
  s.candidate_count = kCandidateCount;
  if (g_scanning) {
    s.scanning = true;
    s.scan_index = g_scan_index;
    s.scan_count = kCandidateCount;
    s.scan_frequency_hz = kCandidates[g_scan_index];
  }
  s.revision = ++g_revision;
  pocsag::set_live_snapshot(s);
  g_last_publish = now;
}

void start_scan(uint32_t now) {
  g_scanning = true;
  g_scan_index = 0;
  memset(g_cand_valid, 0, sizeof g_cand_valid);
  memset(g_cand_corrected, 0, sizeof g_cand_corrected);
  memset(g_cand_messages, 0, sizeof g_cand_messages);
  g_scan_until = now + kDwellMs;
  request_start(kCandidates[0]);
}

void finish_scan() {
  g_scanning = false;
  size_t best = 0;
  bool any_valid = false;
  for (size_t i = 0; i < kCandidateCount; ++i) {
    if (g_cand_valid[i] > 0 && (!any_valid || g_cand_valid[i] > g_cand_valid[best])) {
      best = i;
      any_valid = true;
    } else if (!any_valid && g_cand_corrected[i] > g_cand_corrected[best]) best = i;
  }
  const bool found = any_valid || g_cand_corrected[best] > 0;
  if (found) g_settings.frequency_hz = kCandidates[best];
  request_start(g_settings.frequency_hz);
}

void enter(uint32_t now) {
  if (!g_settings.frequency_hz) g_settings.frequency_hz = kFallbackHz;
  g_entered = true;
  publish(now);
  pocsag::enter(g_settings);
  request_start(g_settings.frequency_hz);
}

void leave() {
  g_entered = false;
  g_scanning = false;
  pocsag::leave();
}

void update(uint32_t now) {
  if (g_scanning && now >= g_scan_until) {
    g_cand_valid[g_scan_index] = g_live_valid;
    g_cand_corrected[g_scan_index] = g_live_corrected;
    g_cand_messages[g_scan_index] = g_live_messages;
    if (++g_scan_index >= kCandidateCount) finish_scan();
    else {
      g_scan_until = now + kDwellMs;
      g_live_valid = g_live_corrected = g_live_messages = 0;
      request_start(kCandidates[g_scan_index]);
    }
  }
  if (now - g_last_publish >= 500) publish(now);
  pocsag::update();
}

Result touch(int x, int y, bool pressed, uint32_t now) {
  Result r;
  if (!pressed) return r;
  const pocsag::Action a = pocsag::handle_touch(x, y);
  switch (a) {
    case pocsag::Action::exit: r.exit = true; break;
    case pocsag::Action::settings_changed:
      g_settings = pocsag::settings();
      request_start(g_settings.frequency_hz);
      break;
    case pocsag::Action::scan_requested: start_scan(now); break;
    case pocsag::Action::scan_cancelled:
      g_scanning = false;
      request_start(g_settings.frequency_hz);
      break;
    case pocsag::Action::none: break;
  }
  if (a != pocsag::Action::none) publish(now);
  return r;
}

pocsag::MessageType kind_of(const char* k) {
  return !strcmp(k, "alpha") ? pocsag::MessageType::alpha : !strcmp(k, "numeric") ? pocsag::MessageType::numeric :
         !strcmp(k, "tone") ? pocsag::MessageType::tone_only : pocsag::MessageType::unknown;
}

void event(const char* line, uint32_t now) {
  char type[16] = {};
  str(line, "type", type, sizeof type);
  if (!strcmp(type, "message")) {
    pocsag::DisplayMessage& m = g_ring[g_write];
    m = {};
    m.timestamp_ms = now;
    m.capcode = (uint32_t)num(line, "capcode");
    m.function = (uint8_t)num(line, "function");
    char kind[12] = {};
    str(line, "kind", kind, sizeof kind);
    m.type = kind_of(kind);
    m.baud = (uint16_t)num(line, "baud");
    m.inverted = flag(line, "inverted");
    m.truncated = flag(line, "truncated");
    m.corrected_bits = (uint16_t)num(line, "corrected_bits");
    m.uncorrectable_words = (uint16_t)num(line, "uncorrectable_words");
    m.text_length = (uint16_t)str(line, "text", m.text, sizeof m.text);
    g_write = (g_write + 1) % kStore;
    if (g_count < kStore) ++g_count;
    g_ids.record_hit(m.capcode, now);
    ++g_live_messages;
    publish(now);
  } else if (!strcmp(type, "stats")) {
    pocsag::Stats& s = g_snap.decoder_stats;
    s = {};
    s.lock = static_cast<pocsag::LockState>((int)num(line, "lock"));
    s.detected_baud = (uint16_t)num(line, "detected_baud");
    s.inverted = flag(line, "inverted");
    s.codewords_total = (uint32_t)num(line, "codewords_total");
    s.codewords_valid = (uint32_t)num(line, "codewords_valid");
    s.codewords_corrected = (uint32_t)num(line, "codewords_corrected");
    s.corrected_bit_count = (uint32_t)num(line, "corrected_bit_count");
    s.codewords_uncorrectable = (uint32_t)num(line, "codewords_uncorrectable");
    s.parity_failures = (uint32_t)num(line, "parity_failures");
    s.batches_synced = (uint32_t)num(line, "batches_synced");
    s.sync_losses = (uint32_t)num(line, "sync_losses");
    s.messages_decoded = (uint32_t)num(line, "messages_decoded");
    s.messages_alpha = (uint32_t)num(line, "messages_alpha");
    s.messages_numeric = (uint32_t)num(line, "messages_numeric");
    s.messages_tone_only = (uint32_t)num(line, "messages_tone_only");
    s.messages_truncated = (uint32_t)num(line, "messages_truncated");
    s.fsk_deviation_hz = (float)num(line, "fsk_deviation_hz");
    s.frequency_offset_hz = (float)num(line, "frequency_offset_hz");
    if (const char* soft = field(line, "soft")) {
      const char* p = soft + 1;
      for (size_t i = 0; i < pocsag::Stats::kSoftSymbolCapacity && *p && *p != ']'; ++i) {
        s.soft_symbols[i] = (float)atof(p);
        p = strchr(p, ',');
        if (!p) break;
        ++p;
      }
    }
    s.soft_symbol_write = (size_t)num(line, "soft_write");
    s.soft_symbol_count = (size_t)num(line, "soft_count");
    char fec[pocsag::Stats::kFecHistoryCapacity + 4] = {};
    str(line, "fec", fec, sizeof fec);
    for (size_t i = 0; i < pocsag::Stats::kFecHistoryCapacity && fec[i]; ++i) s.fec_history[i] = (uint8_t)(fec[i] - '0');
    s.fec_history_write = (size_t)num(line, "fec_write");
    s.fec_history_count = (size_t)num(line, "fec_count");
    g_live_valid = s.codewords_valid;
    g_live_corrected = s.codewords_corrected;
    g_live_messages = s.messages_decoded;
  }
}

void set_num(const char* key, double v) {
  if (!strcmp(key, "receiving")) g_receiving = v != 0;
}

const char* take_request() {
  if (!g_request_pending) return nullptr;
  g_request_pending = false;
  return g_request;
}

}  // namespace

namespace dscreens {
extern const Screen kPocsag = {dashboards::Id::pocsag, "pocsag", enter, leave, update, touch, event, set_num, take_request, nullptr};
}
