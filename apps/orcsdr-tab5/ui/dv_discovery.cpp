#include "dv_discovery.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace orcsdr::dv {

namespace {
constexpr size_t kNone = static_cast<size_t>(-1);
constexpr uint32_t kNearFreqHz = 6250u;
uint32_t abs_diff_hz(uint32_t a, uint32_t b) noexcept { return a >= b ? (a - b) : (b - a); }
uint8_t freq_band(uint32_t hz) noexcept { return hz < 300000000u ? 1u : (hz < 600000000u ? 2u : 3u); }
uint8_t rank_tier(const SystemRec& s, bool has_tg) noexcept {
  if (s.suspected_ghost) return 3u;
  if ((s.role_flags & 2u) != 0u || s.voice_count > 0u || has_tg) return 0u;
  if ((s.role_flags & 1u) != 0u) return 2u;
  return 1u;
}
}

void Discovery::clear() noexcept {
  systems_n_ = tg_n_ = sub_n_ = 0;
  next_uid_ = 1; seq_ = 0; ++revision_; ++structure_;
}

size_t Discovery::find_system(uint8_t protocol, uint32_t frequency_hz, uint8_t area, AreaKind kind) const noexcept {
  size_t near_idx = kNone;
  uint32_t best_diff = kNearFreqHz + 1u;
  for (size_t i = 0; i < systems_n_; ++i) {
    const SystemRec& s = systems_[i];
    if (s.protocol == protocol && s.area == area && s.area_kind == kind) {
      if (s.frequency_hz == frequency_hz) return i;
      const uint32_t d = abs_diff_hz(s.frequency_hz, frequency_hz);
      if (d <= kNearFreqHz && d < best_diff) { best_diff = d; near_idx = i; }
    }
  }
  return near_idx;
}

void Discovery::drop_system(size_t index) noexcept {
  const uint16_t uid = systems_[index].uid;
  for (size_t i = index + 1; i < systems_n_; ++i) systems_[i - 1] = systems_[i];
  --systems_n_;
  size_t w = 0;
  for (size_t i = 0; i < tg_n_; ++i) if (tgs_[i].system_uid != uid) tgs_[w++] = tgs_[i];
  tg_n_ = w; w = 0;
  for (size_t i = 0; i < sub_n_; ++i) if (subs_[i].system_uid != uid) subs_[w++] = subs_[i];
  sub_n_ = w;
}

size_t Discovery::add_system(const Observation& o) noexcept {
  if (systems_n_ == kMaxSystems) {                 // evict the least recently seen system and everything under it
    size_t oldest = 0;
    for (size_t i = 1; i < systems_n_; ++i) if (systems_[i].seq < systems_[oldest].seq) oldest = i;
    drop_system(oldest);
  }
  ++structure_;
  SystemRec& s = systems_[systems_n_];
  s = {};
  s.uid = next_uid_++;
  if (next_uid_ == 0) next_uid_ = 1;
  s.protocol = o.protocol; s.frequency_hz = o.frequency_hz; s.area = o.area; s.area_kind = o.area_kind;
  s.first_utc = o.utc;
  return systems_n_++;
}

bool Discovery::record(const Observation& o) noexcept {
  if (o.evidence < 4 || o.frequency_hz == 0) return false;
  size_t si = find_system(o.protocol, o.frequency_hz, o.area, o.area_kind);
  if (si == kNone) si = add_system(o);
  SystemRec& s = systems_[si];
  s.seq = ++seq_;
  ++s.count;
  if (o.evidence > s.best_evidence) s.best_evidence = o.evidence;
  if (o.slot == 1) s.slots_seen |= 1; else if (o.slot == 2) s.slots_seen |= 2;
  if (o.utc) { if (!s.first_utc) s.first_utc = o.utc; s.last_utc = o.utc; }
  if (o.frequency_hz != s.frequency_hz && o.level_milli > s.peak_level_milli) {
    s.frequency_hz = o.frequency_hz;
  }
  if (o.level_milli > s.peak_level_milli) s.peak_level_milli = o.level_milli;
  const uint8_t eff_role = static_cast<uint8_t>(o.role_flags | (o.evidence >= 5 ? 2u : 0u));
  s.role_flags = static_cast<uint8_t>(s.role_flags | eff_role);
  if (eff_role & 2u) ++s.voice_count;
  if (eff_role & 1u) ++s.control_count;
  if (o.has_sys) {
    s.has_sys = true;
    s.sys_model = o.sys_model;
    s.sys_code = o.sys_code;
    s.net_id = o.net_id;
    s.site_id = o.site_id;
    // Link all non-ghost systems sharing the same verified Net ID in the same frequency band under the lowest UID.
    if (o.net_id > 0u) {
      const uint8_t band = freq_band(s.frequency_hz);
      uint16_t min_uid = s.uid;
      for (size_t i = 0; i < systems_n_; ++i) {
        const SystemRec& other = systems_[i];
        if (!other.suspected_ghost && other.protocol == s.protocol && other.has_sys &&
            other.net_id == s.net_id && freq_band(other.frequency_hz) == band && other.uid < min_uid) {
          min_uid = other.uid;
        }
      }
      for (size_t i = 0; i < systems_n_; ++i) {
        SystemRec& other = systems_[i];
        if (!other.suspected_ghost && other.protocol == s.protocol && other.has_sys &&
            other.net_id == s.net_id && freq_band(other.frequency_hz) == band) {
          other.network_uid = min_uid;
        }
      }
    }
    // Ghost / image guard: flag weak duplicates of an existing stronger system with the exact same (sys_code, area) >6.25 kHz away.
    for (size_t i = 0; i < systems_n_; ++i) {
      if (i == si) continue;
      SystemRec& other = systems_[i];
      if (other.protocol == s.protocol && other.area_kind == s.area_kind && other.area == s.area &&
          other.has_sys && other.sys_code == s.sys_code && abs_diff_hz(other.frequency_hz, s.frequency_hz) > kNearFreqHz &&
          other.peak_level_milli > 0u && s.peak_level_milli > 0u) {
        if (static_cast<uint32_t>(s.peak_level_milli) * 3u <= static_cast<uint32_t>(other.peak_level_milli)) {
          s.suspected_ghost = true;
          other.suspected_ghost = false;
        } else if (static_cast<uint32_t>(other.peak_level_milli) * 3u <= static_cast<uint32_t>(s.peak_level_milli)) {
          other.suspected_ghost = true;
          s.suspected_ghost = false;
        }
      }
    }
  }
  const uint16_t uid = s.uid;
  if (o.evidence >= 5) {
    if (o.has_destination && o.has_source && s.network_uid == 0u) {
      // Link a voice/data channel to an existing control group if both share the exact same verified (TG, Source ID) on the same area/CC.
      for (size_t i = 0; i < systems_n_; ++i) {
        if (i == si) continue;
        const SystemRec& other = systems_[i];
        if (!other.suspected_ghost && other.protocol == s.protocol && other.area_kind == s.area_kind &&
            other.area == s.area) {
          bool matched_sub = false;
          for (size_t u = 0; u < sub_n_; ++u) {
            if (subs_[u].system_uid == other.uid && subs_[u].id == o.source_id &&
                subs_[u].has_last_destination && subs_[u].last_destination == o.destination_id) {
              matched_sub = true;
              break;
            }
          }
          if (matched_sub) {
            s.network_uid = other.network_uid ? other.network_uid : other.uid;
            if (other.network_uid == 0u) systems_[i].network_uid = other.uid;
            break;
          }
        }
      }
    }
    if (o.has_destination) {
      size_t ti = kNone;
      for (size_t i = 0; i < tg_n_; ++i) if (tgs_[i].system_uid == uid && tgs_[i].id == o.destination_id) { ti = i; break; }
      if (ti == kNone) {
        if (tg_n_ == kMaxTalkgroups) {
          size_t oldest = 0;
          for (size_t i = 1; i < tg_n_; ++i) if (tgs_[i].seq < tgs_[oldest].seq) oldest = i;
          for (size_t i = oldest + 1; i < tg_n_; ++i) tgs_[i - 1] = tgs_[i];
          --tg_n_;
        }
        ++structure_;
        ti = tg_n_++;
        tgs_[ti] = {}; tgs_[ti].system_uid = uid; tgs_[ti].id = o.destination_id; tgs_[ti].first_utc = o.utc;
      }
      TalkgroupRec& t = tgs_[ti];
      t.group = o.group_call; t.seq = seq_; ++t.count;
      if (o.utc) { if (!t.first_utc) t.first_utc = o.utc; t.last_utc = o.utc; }
    }
    if (o.has_source) {
      size_t ui = kNone;
      for (size_t i = 0; i < sub_n_; ++i) if (subs_[i].system_uid == uid && subs_[i].id == o.source_id) { ui = i; break; }
      if (ui == kNone) {
        if (sub_n_ == kMaxSubscribers) {
          size_t oldest = 0;
          for (size_t i = 1; i < sub_n_; ++i) if (subs_[i].seq < subs_[oldest].seq) oldest = i;
          for (size_t i = oldest + 1; i < sub_n_; ++i) subs_[i - 1] = subs_[i];
          --sub_n_;
        }
        ++structure_;
        ui = sub_n_++;
        subs_[ui] = {}; subs_[ui].system_uid = uid; subs_[ui].id = o.source_id; subs_[ui].first_utc = o.utc;
      }
      SubscriberRec& u = subs_[ui];
      u.seq = seq_; ++u.count;
      if (o.has_destination) { u.last_destination = o.destination_id; u.has_last_destination = true; }
      if (o.utc) { if (!u.first_utc) u.first_utc = o.utc; u.last_utc = o.utc; }
    }
  }
  ++revision_;
  return true;
}

size_t Discovery::talkgroups_of(uint16_t uid) const noexcept {
  size_t n = 0; for (size_t i = 0; i < tg_n_; ++i) n += tgs_[i].system_uid == uid; return n;
}
size_t Discovery::subscribers_of(uint16_t uid) const noexcept {
  size_t n = 0; for (size_t i = 0; i < sub_n_; ++i) n += subs_[i].system_uid == uid; return n;
}
size_t Discovery::nth_newest_talkgroup(uint16_t uid, size_t n) const noexcept {
  uint32_t above = 0xffffffffu;
  size_t pick = kNone;
  for (size_t k = 0; k <= n; ++k) {
    pick = kNone; uint32_t best = 0;
    for (size_t i = 0; i < tg_n_; ++i)
      if (tgs_[i].system_uid == uid && tgs_[i].seq < above && (pick == kNone || tgs_[i].seq > best)) { pick = i; best = tgs_[i].seq; }
    if (pick == kNone) return kNone;
    above = best;
  }
  return pick;
}
size_t Discovery::nth_newest_subscriber(uint16_t uid, size_t n) const noexcept {
  uint32_t above = 0xffffffffu;
  size_t pick = kNone;
  for (size_t k = 0; k <= n; ++k) {
    pick = kNone; uint32_t best = 0;
    for (size_t i = 0; i < sub_n_; ++i)
      if (subs_[i].system_uid == uid && subs_[i].seq < above && (pick == kNone || subs_[i].seq > best)) { pick = i; best = subs_[i].seq; }
    if (pick == kNone) return kNone;
    above = best;
  }
  return pick;
}

size_t Discovery::nth_newest_system(size_t n) const noexcept {
  uint32_t above = 0xffffffffu;
  size_t pick = kNone;
  for (size_t k = 0; k <= n; ++k) {
    pick = kNone; uint32_t best = 0;
    for (size_t i = 0; i < systems_n_; ++i)
      if (systems_[i].seq < above && (pick == kNone || systems_[i].seq > best)) { pick = i; best = systems_[i].seq; }
    if (pick == kNone) return kNone;
    above = best;
  }
  return pick;
}

size_t Discovery::nth_ranked_system(size_t n) const noexcept {
  if (n >= systems_n_) return kNone;
  size_t idx[kMaxSystems]{};
  uint8_t tier[kMaxSystems]{};
  for (size_t i = 0; i < systems_n_; ++i) {
    idx[i] = i;
    tier[i] = rank_tier(systems_[i], talkgroups_of(systems_[i].uid) > 0u);
  }
  for (size_t i = 1; i < systems_n_; ++i) {
    const size_t key = idx[i];
    size_t j = i;
    while (j > 0) {
      const size_t prev = idx[j - 1];
      bool better = false;
      if (tier[key] != tier[prev]) better = tier[key] < tier[prev];
      else if (systems_[key].seq != systems_[prev].seq) better = systems_[key].seq > systems_[prev].seq;
      else better = systems_[key].voice_count > systems_[prev].voice_count;
      if (!better) break;
      idx[j] = prev;
      --j;
    }
    idx[j] = key;
  }
  return idx[n];
}

size_t Discovery::network_member_count(uint16_t network_uid) const noexcept {
  if (network_uid == 0u) return 0u;
  size_t n = 0;
  for (size_t i = 0; i < systems_n_; ++i) {
    if (!systems_[i].suspected_ghost && systems_[i].network_uid == network_uid) ++n;
  }
  return n;
}

bool Discovery::link_granted_channel(uint8_t protocol, uint32_t control_hz, uint32_t grant_hz) noexcept {
  if (control_hz == 0u || grant_hz == 0u || abs_diff_hz(control_hz, grant_hz) <= kNearFreqHz) return false;
  size_t ctrl_idx = kNone, grant_idx = kNone;
  for (size_t i = 0; i < systems_n_; ++i) {
    if (systems_[i].protocol != protocol || systems_[i].suspected_ghost) continue;
    if (abs_diff_hz(systems_[i].frequency_hz, control_hz) <= kNearFreqHz) ctrl_idx = i;
    else if (abs_diff_hz(systems_[i].frequency_hz, grant_hz) <= kNearFreqHz) grant_idx = i;
  }
  if (ctrl_idx == kNone || grant_idx == kNone) return false;
  const uint16_t net_uid = systems_[ctrl_idx].network_uid ? systems_[ctrl_idx].network_uid : systems_[ctrl_idx].uid;
  systems_[ctrl_idx].network_uid = net_uid;
  if (systems_[grant_idx].network_uid != net_uid) {
    systems_[grant_idx].network_uid = net_uid;
    ++revision_;
  }
  return true;
}

// Text form, oldest first within each table so a reload keeps the recency order:
//   DVDB2
//   S uid proto freq kind area ev slots first last count role has_sys model sys_code net_id site_id voice_count control_count peak_level network_uid ghost
//   T uid id group first last count
//   U uid id has_dst dst first last count
size_t Discovery::serialize(char* out, size_t cap) const noexcept {
  size_t w = 0;
  auto put = [&](const char* fmt, auto... a) -> bool {
    if (w >= cap) return false;
    const int n = std::snprintf(out + w, cap - w, fmt, a...);
    if (n < 0 || static_cast<size_t>(n) >= cap - w) return false;
    w += static_cast<size_t>(n);
    return true;
  };
  if (cap < 7) return 0;
  std::memcpy(out, "DVDB2\n", 6); w = 6;
  auto order = [](const auto* arr, size_t n, size_t* idx) {
    for (size_t i = 0; i < n; ++i) idx[i] = i;
    for (size_t i = 1; i < n; ++i) {            // insertion sort by seq ascending
      size_t k = idx[i]; size_t j = i;
      while (j > 0 && arr[idx[j - 1]].seq > arr[k].seq) { idx[j] = idx[j - 1]; --j; }
      idx[j] = k;
    }
  };
  size_t idx[kMaxSubscribers];
  order(systems_, systems_n_, idx);
  for (size_t k = 0; k < systems_n_; ++k) {
    const SystemRec& s = systems_[idx[k]];
    if (!put("S %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u\n",
             s.uid, s.protocol, s.frequency_hz, static_cast<unsigned>(s.area_kind), s.area,
             s.best_evidence, s.slots_seen, s.first_utc, s.last_utc, s.count,
             s.role_flags, s.has_sys ? 1u : 0u, s.sys_model, s.sys_code, s.net_id, s.site_id,
             s.voice_count, s.control_count, s.peak_level_milli, s.network_uid, s.suspected_ghost ? 1u : 0u))
      return 0;
  }
  order(tgs_, tg_n_, idx);
  for (size_t k = 0; k < tg_n_; ++k) {
    const TalkgroupRec& t = tgs_[idx[k]];
    if (!put("T %u %u %u %u %u %u\n", t.system_uid, t.id, t.group ? 1u : 0u, t.first_utc, t.last_utc, t.count)) return 0;
  }
  order(subs_, sub_n_, idx);
  for (size_t k = 0; k < sub_n_; ++k) {
    const SubscriberRec& u = subs_[idx[k]];
    if (!put("U %u %u %u %u %u %u %u\n", u.system_uid, u.id, u.has_last_destination ? 1u : 0u, u.last_destination, u.first_utc, u.last_utc, u.count))
      return 0;
  }
  return w;
}

bool Discovery::parse(const char* text, size_t length) noexcept {
  if (!text || length < 6) return false;
  const bool v1 = std::strncmp(text, "DVDB1\n", 6) == 0;
  const bool v2 = std::strncmp(text, "DVDB2\n", 6) == 0;
  if (!v1 && !v2) return false;
  // Two passes over the text: pass 0 validates everything (syntax, ranges, capacities, that every talkgroup/subscriber names an
  // earlier system) without touching *this; pass 1 stores. No scratch copy of the tables (the Tab5 boot RAM pool has no room for one).
  for (int pass = 0; pass < 2; ++pass) {
    const bool store = pass == 1;
    size_t sn = 0, tn = 0, un = 0, pos = 6;
    uint16_t uids[kMaxSystems]{};
    uint16_t next = 1; uint32_t seq = 0;
    while (pos < length) {
      const char* line = text + pos;
      const char* nl = static_cast<const char*>(std::memchr(line, '\n', length - pos));
      if (!nl) return false;                   // truncated final line: reject the whole file
      char buf[192];
      const size_t n = static_cast<size_t>(nl - line);
      if (n == 0 || n >= sizeof(buf)) return false;
      std::memcpy(buf, line, n); buf[n] = 0;
      pos += n + 1;
      unsigned long v[21]{};
      const char kind = buf[0];
      const size_t need = kind == 'S' ? (v2 ? 21u : 10u) : kind == 'T' ? 6u : kind == 'U' ? 7u : 0u;
      if (!need || buf[1] != ' ') return false;
      char* p = buf + 2;
      for (size_t i = 0; i < need; ++i) {
        char* end = nullptr;
        if (*p < '0' || *p > '9') return false;
        v[i] = std::strtoul(p, &end, 10);
        if (end == p) return false;
        p = end;
        if (i + 1 < need) { if (*p != ' ') return false; ++p; } else if (*p != 0) return false;
      }
      if (kind == 'S') {
        if (sn == kMaxSystems || v[0] == 0 || v[0] > 0xffff || v[1] > 255 || v[3] > 2 || v[4] > 255 || v[5] > 255 || v[6] > 3) return false;
        if (v2 && (v[10] > 255 || v[11] > 1 || v[12] > 3 || v[13] > 0x3fff || v[14] > 0xffff || v[15] > 0xffff || v[18] > 0xffff || v[19] > 0xffff || v[20] > 1)) return false;
        for (size_t i = 0; i < sn; ++i) if (uids[i] == v[0]) return false;     // duplicate uid
        uids[sn] = static_cast<uint16_t>(v[0]);
        if (store) {
          SystemRec& s = systems_[sn];
          s = {}; s.uid = static_cast<uint16_t>(v[0]); s.protocol = static_cast<uint8_t>(v[1]); s.frequency_hz = static_cast<uint32_t>(v[2]);
          s.area_kind = static_cast<AreaKind>(v[3]); s.area = static_cast<uint8_t>(v[4]); s.best_evidence = static_cast<uint8_t>(v[5]);
          s.slots_seen = static_cast<uint8_t>(v[6]); s.first_utc = static_cast<uint32_t>(v[7]); s.last_utc = static_cast<uint32_t>(v[8]);
          s.count = static_cast<uint32_t>(v[9]); s.seq = ++seq;
          if (v2) {
            s.role_flags = static_cast<uint8_t>(v[10]); s.has_sys = v[11] != 0; s.sys_model = static_cast<uint8_t>(v[12]);
            s.sys_code = static_cast<uint16_t>(v[13]); s.net_id = static_cast<uint16_t>(v[14]); s.site_id = static_cast<uint16_t>(v[15]);
            s.voice_count = static_cast<uint32_t>(v[16]); s.control_count = static_cast<uint32_t>(v[17]);
            s.peak_level_milli = static_cast<uint16_t>(v[18]); s.network_uid = static_cast<uint16_t>(v[19]); s.suspected_ghost = v[20] != 0;
          } else if (s.best_evidence >= 5) {
            s.role_flags = 2u;
          }
        }
        if (v[0] >= next) next = static_cast<uint16_t>(v[0] + 1);
        ++sn;
      } else {
        const bool is_t = kind == 'T';
        if ((is_t ? tn == kMaxTalkgroups : un == kMaxSubscribers) || v[0] == 0 || v[0] > 0xffff) return false;
        bool known = false;
        for (size_t i = 0; i < sn; ++i) known |= uids[i] == v[0];
        if (!known) return false;
        if (store) {
          if (is_t) {
            TalkgroupRec& t = tgs_[tn];
            t = {}; t.system_uid = static_cast<uint16_t>(v[0]); t.id = static_cast<uint32_t>(v[1]); t.group = v[2] != 0;
            t.first_utc = static_cast<uint32_t>(v[3]); t.last_utc = static_cast<uint32_t>(v[4]); t.count = static_cast<uint32_t>(v[5]); t.seq = ++seq;
          } else {
            SubscriberRec& u = subs_[un];
            u = {}; u.system_uid = static_cast<uint16_t>(v[0]); u.id = static_cast<uint32_t>(v[1]); u.has_last_destination = v[2] != 0;
            u.last_destination = static_cast<uint32_t>(v[3]); u.first_utc = static_cast<uint32_t>(v[4]); u.last_utc = static_cast<uint32_t>(v[5]);
            u.count = static_cast<uint32_t>(v[6]); u.seq = ++seq;
          }
        }
        if (is_t) ++tn; else ++un;
      }
    }
    if (store) { systems_n_ = sn; tg_n_ = tn; sub_n_ = un; seq_ = seq; next_uid_ = next; ++revision_; ++structure_; }
  }
  consolidate();
  return true;
}

void Discovery::consolidate() noexcept {
  bool changed = false;
  // 1. Merge +-6.25 kHz near-frequency duplicates with identical (protocol, area_kind, area).
  for (size_t i = 0; i < systems_n_; ++i) {
    for (size_t j = i + 1; j < systems_n_;) {
      SystemRec& a = systems_[i];
      SystemRec& b = systems_[j];
      if (a.protocol == b.protocol && a.area_kind == b.area_kind && a.area == b.area &&
          abs_diff_hz(a.frequency_hz, b.frequency_hz) <= kNearFreqHz) {
        const bool keep_b = (b.peak_level_milli > a.peak_level_milli) ||
                            (b.peak_level_milli == a.peak_level_milli && b.count > a.count);
        const size_t keep_idx = keep_b ? j : i;
        const size_t drop_idx = keep_b ? i : j;
        SystemRec& keep = systems_[keep_idx];
        const SystemRec drop = systems_[drop_idx];
        const uint16_t keep_uid = keep.uid;
        const uint16_t drop_uid = drop.uid;
        keep.count += drop.count;
        keep.voice_count += drop.voice_count;
        keep.control_count += drop.control_count;
        keep.slots_seen = static_cast<uint8_t>(keep.slots_seen | drop.slots_seen);
        keep.role_flags = static_cast<uint8_t>(keep.role_flags | drop.role_flags);
        if (drop.best_evidence > keep.best_evidence) keep.best_evidence = drop.best_evidence;
        if (drop.first_utc && (!keep.first_utc || drop.first_utc < keep.first_utc)) keep.first_utc = drop.first_utc;
        if (drop.last_utc > keep.last_utc) keep.last_utc = drop.last_utc;
        if (drop.seq > keep.seq) keep.seq = drop.seq;
        if (drop.peak_level_milli > keep.peak_level_milli) keep.peak_level_milli = drop.peak_level_milli;
        if (!keep.has_sys && drop.has_sys) {
          keep.has_sys = true; keep.sys_model = drop.sys_model; keep.sys_code = drop.sys_code;
          keep.net_id = drop.net_id; keep.site_id = drop.site_id;
        }
        keep.suspected_ghost = keep.suspected_ghost && drop.suspected_ghost;
        // Remap talkgroups from drop_uid to keep_uid, deduplicating by id.
        for (size_t t = 0; t < tg_n_;) {
          if (tgs_[t].system_uid != drop_uid) { ++t; continue; }
          size_t dup = kNone;
          for (size_t k = 0; k < tg_n_; ++k) {
            if (tgs_[k].system_uid == keep_uid && tgs_[k].id == tgs_[t].id) { dup = k; break; }
          }
          if (dup != kNone) {
            tgs_[dup].count += tgs_[t].count;
            if (tgs_[t].last_utc > tgs_[dup].last_utc) tgs_[dup].last_utc = tgs_[t].last_utc;
            if (tgs_[t].seq > tgs_[dup].seq) tgs_[dup].seq = tgs_[t].seq;
            for (size_t m = t + 1; m < tg_n_; ++m) tgs_[m - 1] = tgs_[m];
            --tg_n_;
          } else {
            tgs_[t].system_uid = keep_uid;
            ++t;
          }
        }
        // Remap subscribers from drop_uid to keep_uid, deduplicating by id.
        for (size_t u = 0; u < sub_n_;) {
          if (subs_[u].system_uid != drop_uid) { ++u; continue; }
          size_t dup = kNone;
          for (size_t k = 0; k < sub_n_; ++k) {
            if (subs_[k].system_uid == keep_uid && subs_[k].id == subs_[u].id) { dup = k; break; }
          }
          if (dup != kNone) {
            subs_[dup].count += subs_[u].count;
            if (subs_[u].last_utc > subs_[dup].last_utc) {
              subs_[dup].last_utc = subs_[u].last_utc;
              if (subs_[u].has_last_destination) {
                subs_[dup].last_destination = subs_[u].last_destination;
                subs_[dup].has_last_destination = true;
              }
            }
            if (subs_[u].seq > subs_[dup].seq) subs_[dup].seq = subs_[u].seq;
            for (size_t m = u + 1; m < sub_n_; ++m) subs_[m - 1] = subs_[m];
            --sub_n_;
          } else {
            subs_[u].system_uid = keep_uid;
            ++u;
          }
        }
        if (keep_b) systems_[i] = keep;
        for (size_t m = j + 1; m < systems_n_; ++m) systems_[m - 1] = systems_[m];
        --systems_n_;
        changed = true;
        continue;
      }
      ++j;
    }
  }
  // 2. Suppress same-frequency glitched-CC 1-2 hit noise when a strong real system exists on the same frequency.
  for (size_t j = 0; j < systems_n_; ++j) {
    SystemRec& s = systems_[j];
    if (s.suspected_ghost || s.count > 2u || s.voice_count > 0u || s.has_sys ||
        talkgroups_of(s.uid) > 0u || subscribers_of(s.uid) > 0u) continue;
    for (size_t i = 0; i < systems_n_; ++i) {
      if (i == j) continue;
      const SystemRec& real = systems_[i];
      if (!real.suspected_ghost && real.protocol == s.protocol &&
          abs_diff_hz(real.frequency_hz, s.frequency_hz) <= kNearFreqHz &&
          (real.count >= 10u || real.voice_count > 0u || talkgroups_of(real.uid) > 0u)) {
        s.suspected_ghost = true;
        changed = true;
        break;
      }
    }
  }
  // 3. Retroactive Net ID grouping (same frequency band: VHF with VHF, UHF with UHF).
  for (size_t i = 0; i < systems_n_; ++i) {
    const SystemRec& a = systems_[i];
    if (a.suspected_ghost || !a.has_sys || a.net_id == 0u) continue;
    const uint8_t band = freq_band(a.frequency_hz);
    uint16_t min_uid = a.uid;
    for (size_t j = 0; j < systems_n_; ++j) {
      const SystemRec& b = systems_[j];
      if (!b.suspected_ghost && b.protocol == a.protocol && b.has_sys && b.net_id == a.net_id &&
          freq_band(b.frequency_hz) == band && b.uid < min_uid) {
        min_uid = b.uid;
      }
    }
    for (size_t j = 0; j < systems_n_; ++j) {
      SystemRec& b = systems_[j];
      if (!b.suspected_ghost && b.protocol == a.protocol && b.has_sys && b.net_id == a.net_id &&
          freq_band(b.frequency_hz) == band && b.network_uid != min_uid) {
        b.network_uid = min_uid;
        changed = true;
      }
    }
  }
  // 4. Retroactive shared (TG, Unit) linking across channels with the same (protocol, area_kind, area).
  for (size_t i = 0; i < systems_n_; ++i) {
    if (systems_[i].suspected_ghost) continue;
    for (size_t j = i + 1; j < systems_n_; ++j) {
      if (systems_[j].suspected_ghost) continue;
      if (systems_[i].protocol != systems_[j].protocol || systems_[i].area_kind != systems_[j].area_kind ||
          systems_[i].area != systems_[j].area) continue;
      bool shared = false;
      for (size_t u1 = 0; u1 < sub_n_ && !shared; ++u1) {
        if (subs_[u1].system_uid != systems_[i].uid || !subs_[u1].has_last_destination) continue;
        for (size_t u2 = 0; u2 < sub_n_; ++u2) {
          if (subs_[u2].system_uid == systems_[j].uid && subs_[u2].id == subs_[u1].id &&
              subs_[u2].has_last_destination && subs_[u2].last_destination == subs_[u1].last_destination) {
            shared = true;
            break;
          }
        }
      }
      if (shared) {
        const uint16_t n1 = systems_[i].network_uid ? systems_[i].network_uid : systems_[i].uid;
        const uint16_t n2 = systems_[j].network_uid ? systems_[j].network_uid : systems_[j].uid;
        const uint16_t target = n1 < n2 ? n1 : n2;
        for (size_t k = 0; k < systems_n_; ++k) {
          if (systems_[k].uid == n1 || systems_[k].uid == n2 ||
              systems_[k].network_uid == n1 || systems_[k].network_uid == n2) {
            if (systems_[k].network_uid != target) {
              systems_[k].network_uid = target;
              changed = true;
            }
          }
        }
      }
    }
  }
  if (changed) { ++revision_; ++structure_; }
}

bool Discovery::is_confirmed_system(const SystemRec& s) const noexcept {
  if (s.suspected_ghost) return false;
  return s.count >= 3u || s.voice_count > 0u || talkgroups_of(s.uid) > 0u || s.has_sys;
}

size_t Discovery::grouped_system_count() const noexcept {
  uint16_t seen_nets[kMaxSystems]{};
  size_t seen_n = 0;
  size_t total = 0;
  for (size_t i = 0; i < systems_n_; ++i) {
    const SystemRec& s = systems_[i];
    if (!is_confirmed_system(s)) continue;
    if (s.network_uid == 0u) {
      ++total;
    } else {
      bool dup = false;
      for (size_t k = 0; k < seen_n; ++k) if (seen_nets[k] == s.network_uid) { dup = true; break; }
      if (!dup) { seen_nets[seen_n++] = s.network_uid; ++total; }
    }
  }
  return total;
}

bool Discovery::nth_ranked_group(size_t n, GroupSummary& out) const noexcept {
  GroupSummary groups[kMaxSystems]{};
  uint32_t group_seq[kMaxSystems]{};
  uint8_t group_tier[kMaxSystems]{};
  size_t gn = 0;

  for (size_t i = 0; i < systems_n_; ++i) {
    const SystemRec& s = systems_[i];
    if (!is_confirmed_system(s)) continue;
    size_t gi = kNone;
    if (s.network_uid != 0u) {
      for (size_t k = 0; k < gn; ++k) {
        if (groups[k].network_uid == s.network_uid) { gi = k; break; }
      }
    }
    const bool has_tg = talkgroups_of(s.uid) > 0u;
    const uint8_t stier = rank_tier(s, has_tg);
    if (gi == kNone) {
      gi = gn++;
      groups[gi] = {};
      groups[gi].primary_index = i;
      groups[gi].network_uid = s.network_uid;
      groups[gi].site_count = 0;
      group_seq[gi] = s.seq;
      group_tier[gi] = stier;
    } else {
      const SystemRec& cur = systems_[groups[gi].primary_index];
      const uint8_t ctier = rank_tier(cur, talkgroups_of(cur.uid) > 0u);
      if (stier < ctier || (stier == ctier && (s.voice_count > cur.voice_count || (s.voice_count == cur.voice_count && s.count > cur.count)))) {
        groups[gi].primary_index = i;
      }
      if (s.seq > group_seq[gi]) group_seq[gi] = s.seq;
      if (stier < group_tier[gi]) group_tier[gi] = stier;
    }
    GroupSummary& g = groups[gi];
    if (g.site_count < 255u) ++g.site_count;
    g.combined_role_flags = static_cast<uint8_t>(g.combined_role_flags | s.role_flags | (has_tg ? 2u : 0u));
    g.total_heard += s.count;
    g.total_voice += s.voice_count;
    g.total_talkgroups = static_cast<uint16_t>(g.total_talkgroups + talkgroups_of(s.uid));
    g.total_subscribers = static_cast<uint16_t>(g.total_subscribers + subscribers_of(s.uid));
    if (g.site_listed < kMaxGroupSites) {
      g.site_hz[g.site_listed] = s.frequency_hz;
      g.site_role[g.site_listed] = s.role_flags;
      g.site_id[g.site_listed] = s.site_id;
      ++g.site_listed;
    }
  }
  if (n >= gn) return false;
  size_t order[kMaxSystems]{};
  for (size_t i = 0; i < gn; ++i) order[i] = i;
  for (size_t i = 1; i < gn; ++i) {
    const size_t key = order[i];
    size_t j = i;
    while (j > 0) {
      const size_t prev = order[j - 1];
      bool better = false;
      if (group_tier[key] != group_tier[prev]) better = group_tier[key] < group_tier[prev];
      else if (group_seq[key] != group_seq[prev]) better = group_seq[key] > group_seq[prev];
      else better = groups[key].total_voice > groups[prev].total_voice;
      if (!better) break;
      order[j] = prev;
      --j;
    }
    order[j] = key;
  }
  out = groups[order[n]];
  return true;
}

}  // namespace orcsdr::dv
