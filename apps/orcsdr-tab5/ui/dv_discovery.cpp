#include "dv_discovery.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace orcsdr::dv {

namespace {
constexpr size_t kNone = static_cast<size_t>(-1);
}

void Discovery::clear() noexcept {
  systems_n_ = tg_n_ = sub_n_ = 0;
  next_uid_ = 1; seq_ = 0; ++revision_; ++structure_;
}

size_t Discovery::find_system(uint8_t protocol, uint32_t frequency_hz, uint8_t area, AreaKind kind) const noexcept {
  for (size_t i = 0; i < systems_n_; ++i) {
    const SystemRec& s = systems_[i];
    if (s.protocol == protocol && s.frequency_hz == frequency_hz && s.area == area && s.area_kind == kind) return i;
  }
  return kNone;
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
  const uint16_t uid = s.uid;
  if (o.evidence >= 5) {
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

// Text form, oldest first within each table so a reload keeps the recency order:
//   DVDB1
//   S uid proto freq kind area ev slots first last count
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
  std::memcpy(out, "DVDB1\n", 6); w = 6;
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
    if (!put("S %u %u %u %u %u %u %u %u %u %u\n", s.uid, s.protocol, s.frequency_hz, static_cast<unsigned>(s.area_kind), s.area,
             s.best_evidence, s.slots_seen, s.first_utc, s.last_utc, s.count)) return 0;
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
  if (!text || length < 6 || std::strncmp(text, "DVDB1\n", 6) != 0) return false;
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
      char buf[160];
      const size_t n = static_cast<size_t>(nl - line);
      if (n == 0 || n >= sizeof(buf)) return false;
      std::memcpy(buf, line, n); buf[n] = 0;
      pos += n + 1;
      unsigned long v[10]{};
      const char kind = buf[0];
      const size_t need = kind == 'S' ? 10 : kind == 'T' ? 6 : kind == 'U' ? 7 : 0;
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
        for (size_t i = 0; i < sn; ++i) if (uids[i] == v[0]) return false;     // duplicate uid
        uids[sn] = static_cast<uint16_t>(v[0]);
        if (store) {
          SystemRec& s = systems_[sn];
          s = {}; s.uid = static_cast<uint16_t>(v[0]); s.protocol = static_cast<uint8_t>(v[1]); s.frequency_hz = static_cast<uint32_t>(v[2]);
          s.area_kind = static_cast<AreaKind>(v[3]); s.area = static_cast<uint8_t>(v[4]); s.best_evidence = static_cast<uint8_t>(v[5]);
          s.slots_seen = static_cast<uint8_t>(v[6]); s.first_utc = static_cast<uint32_t>(v[7]); s.last_utc = static_cast<uint32_t>(v[8]);
          s.count = static_cast<uint32_t>(v[9]); s.seq = ++seq;
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
  return true;
}

}  // namespace orcsdr::dv
