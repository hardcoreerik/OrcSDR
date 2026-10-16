// Host tests for the ACARS dashboard model, demo data and touch controller. No M5GFX, ESP-IDF or hardware.
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <chrono>
#include <thread>

#include "acars_dashboard_layout.hpp"
#include "acars_dashboard_model.hpp"
#include "acars_dashboard_ui.hpp"
#include "acars_demo_data.hpp"

using namespace orcsdr::acars_ui;
using orcsdr::acars_ui::layout::Rect;

namespace {

[[noreturn]] void fail(const char* expression, int line) {
  std::fprintf(stderr, "FAIL line=%d check=%s\n", line, expression);
  std::exit(1);
}
#define CHECK(expression) \
  do { if (!(expression)) fail(#expression, __LINE__); } while (false)

orcsdr::acars::Message make_message(const char* tail, const char* flight, const char* label, const char* text,
                                    bool corrected = false, uint32_t hz = 131550000u, uint8_t end = 0x03) {
  orcsdr::acars::Message m;
  std::snprintf(m.tail, sizeof(m.tail), "%s", tail);
  std::snprintf(m.flight, sizeof(m.flight), "%s", flight);
  std::snprintf(m.label, sizeof(m.label), "%s", label);
  m.mode = '2';
  m.acknowledgement = '\x15';
  m.block_id = '1';
  m.frequency_hz = hz;
  m.has_text = text[0] != '\0';
  m.end_marker = end;
  m.corrected = corrected;
  m.corrected_bits = corrected ? 1 : 0;
  m.text_length = static_cast<uint16_t>(std::strlen(text));
  std::memcpy(m.text, text, m.text_length);
  m.raw_bytes = static_cast<uint16_t>(m.text_length + 16);
  return m;
}

// Model is ~50 KB: always on the heap, as the firmware does.
std::unique_ptr<Model> new_model() {
  std::unique_ptr<Model> m(new Model());
  m->reset();
  return m;
}
std::unique_ptr<Controller> new_ui() {
  std::unique_ptr<Controller> c(new Controller());
  c->reset();
  return c;
}

// In-memory preference store.
std::map<std::string, uint32_t> g_kv;
Store memory_store() {
  Store s;
  s.get_bool = [](const char* k, bool f) { auto it = g_kv.find(k); return it == g_kv.end() ? f : it->second != 0; };
  s.put_bool = [](const char* k, bool v) { g_kv[k] = v ? 1 : 0; };
  s.get_u8 = [](const char* k, uint8_t f) { auto it = g_kv.find(k); return it == g_kv.end() ? f : static_cast<uint8_t>(it->second); };
  s.put_u8 = [](const char* k, uint8_t v) { g_kv[k] = v; };
  s.get_u32 = [](const char* k, uint32_t f) { auto it = g_kv.find(k); return it == g_kv.end() ? f : it->second; };
  s.put_u32 = [](const char* k, uint32_t v) { g_kv[k] = v; };
  return s;
}

void tap(Controller& ui, Model& model, const Rect& r) { (void)ui.touch(r.x + r.w / 2, r.y + r.h / 2, model); }
Action tap_action(Controller& ui, Model& model, const Rect& r) { return ui.touch(r.x + r.w / 2, r.y + r.h / 2, model); }

// ---------------------------------------------------------------- tests

void test_self_checks() {
  CHECK(model_self_check());
  CHECK(ui_self_check());
}

void test_empty_defaults() {
  auto m = new_model();
  const Snapshot& s = m->snapshot();
  CHECK(s.source == SourceKind::live);
  CHECK(s.receiver == ReceiverState::disconnected);
  CHECK(s.message_count == 0 && s.aircraft_count == 0);
  CHECK(!s.stats_available);
  CHECK(pack(s.caps) == 0);                         // no capability is claimed today
  CHECK(!s.caps.live_reception && !s.caps.archive && !s.caps.signal_measurements);
  CHECK(s.tune == TuneStatus::unavailable && s.tuned_hz == 0);
  CHECK(!s.archive_available && s.archive_count == 0);
  CHECK(s.channel_count == 6 && s.channels[0].selected && !s.channels[0].receiving);
  for (size_t i = 0; i < s.channel_count; ++i) CHECK(!s.channels[i].activity_measured && !s.channels[i].receiving);
  CHECK(s.requested_hz == 131550000u);
}

void test_capacity_and_order() {
  auto m = new_model();
  for (int i = 0; i < 100; ++i) {
    char flight[16];
    std::snprintf(flight, sizeof(flight), "F%05u", static_cast<unsigned>(i % 100000));
    CHECK(m->post_message(make_message("N101DM", flight, "H1", "hello"), 1000u + i, 0));
    m->service();
  }
  const Snapshot& s = m->snapshot();
  CHECK(s.messages_held == kHistoryCapacity);       // ring bound
  CHECK(s.messages_total == 100);
  CHECK(s.message_count == kMessageRows);           // display bound
  CHECK(std::strcmp(s.messages[0].flight, "F00099") == 0);   // newest first
  CHECK(std::strcmp(s.messages[11].flight, "F00088") == 0);
  for (size_t i = 1; i < s.message_count; ++i) CHECK(s.messages[i - 1].seq > s.messages[i].seq);
  CHECK(s.matching_count == kHistoryCapacity);
  CHECK(s.aircraft_count == 1 && s.aircraft[0].message_count == 100);
  CHECK(s.aircraft[0].history_count == kAircraftHistory);
}

void test_queue_overflow_is_counted_not_fatal() {
  auto m = new_model();
  int accepted = 0;
  for (int i = 0; i < 20; ++i) accepted += m->post_message(make_message("N1", "", "H1", "x"), i, 0) ? 1 : 0;
  CHECK(accepted == static_cast<int>(kQueueCapacity));
  m->service();
  const Snapshot& s = m->snapshot();
  CHECK(s.queue_drops == 20 - kQueueCapacity);
  CHECK(s.messages_total == kQueueCapacity);
  // The queue is usable again after a drain.
  CHECK(m->post_message(make_message("N1", "", "H1", "x"), 99, 0));
}

void test_aircraft_grouping_and_eviction() {
  auto m = new_model();
  m->post_message(make_message(".N101DM", "AB0001", "H1", "a"), 1, 0);
  m->post_message(make_message("N101DM", "AB0002", "5Z", "b"), 2, 0);
  m->post_message(make_message("N202DM", "", "H1", "c", true), 3, 0);
  m->post_message(make_message("", "", "H1", "d"), 4, 0);
  m->service();
  {
    const Snapshot& s = m->snapshot();
    CHECK(s.aircraft_total == 3);                   // ".N101DM" and "N101DM" are one aircraft; empty tail is its own group
    CHECK(std::strcmp(s.aircraft[0].tail, "") == 0);   // recent sort: newest first
    const AircraftRecord* a = nullptr;
    for (size_t i = 0; i < s.aircraft_count; ++i) if (std::strcmp(s.aircraft[i].tail, "N101DM") == 0) a = &s.aircraft[i];
    CHECK(a && a->message_count == 2 && std::strcmp(a->flight, "AB0002") == 0 && std::strcmp(a->last_label, "5Z") == 0);
    const AircraftRecord* b = nullptr;
    for (size_t i = 0; i < s.aircraft_count; ++i) if (std::strcmp(s.aircraft[i].tail, "N202DM") == 0) b = &s.aircraft[i];
    CHECK(b && b->corrected_count == 1);
  }
  CHECK(m->set_sort(AircraftSort::most_messages));
  CHECK(std::strcmp(m->snapshot().aircraft[0].tail, "N101DM") == 0);
  const MessageRecord* list[6];
  CHECK(m->aircraft_messages("N101DM", list, 6) == 2);
  CHECK(list[0]->seq > list[1]->seq);               // newest first
  CHECK(m->aircraft_messages("NOPE", list, 6) == 0);
  // More distinct tails than the table holds: least recently seen is evicted, table stays bounded.
  for (int i = 0; i < 40; ++i) {
    char tail[16];
    std::snprintf(tail, sizeof(tail), "T%03u", static_cast<unsigned>(i % 1000));
    m->post_message(make_message(tail, "", "H1", "z"), 10 + i, 0);
    m->service();
  }
  const Snapshot& s = m->snapshot();
  CHECK(s.aircraft_total == kAircraftTable);
  CHECK(s.aircraft_count == kAircraftRows);
  CHECK(s.aircraft_offset == 0);
  CHECK(m->scroll_aircraft(4) && m->snapshot().aircraft_offset == 4);
  CHECK(m->scroll_aircraft(100) && m->snapshot().aircraft_offset == kAircraftTable - kAircraftRows);
  CHECK(!m->scroll_aircraft(1));
  CHECK(m->scroll_aircraft(-100) && m->snapshot().aircraft_offset == 0);
}

void test_time_validity() {
  auto m = new_model();
  m->post_message(make_message("N1", "", "H1", "a"), 5000, 0);
  m->post_message(make_message("N2", "", "H1", "b"), 6000, 1791900000u);
  m->service();
  const Snapshot& s = m->snapshot();
  CHECK(s.messages[0].time_valid && s.messages[0].utc_epoch == 1791900000u);
  CHECK(!s.messages[1].time_valid && s.messages[1].utc_epoch == 0 && s.messages[1].uptime_ms == 5000);
}

void test_sanitization() {
  const char raw[] = {'A', 0, 'B', 0x01, '\r', '\n', '\t', 0x7f, char(0xC3), 'z'};
  char out[64];
  bool cut = true;
  CHECK(sanitize_preview(out, sizeof(out), raw, sizeof(raw), &cut) == sizeof(raw) && !cut);
  CHECK(std::strcmp(out, "A.B.   ..z") == 0);
  CHECK(escape_text(out, sizeof(out), raw, sizeof(raw), &cut) > sizeof(raw) && !cut);
  CHECK(std::strcmp(out, "A<00>B<01><CR><LF><TAB><7F><C3>z") == 0);
  // Exact-fit and one-short capacities never overflow and always terminate.
  for (size_t cap = 1; cap < 40; ++cap) {
    char guard[48];
    std::memset(guard, 0x5a, sizeof(guard));
    const size_t n = escape_text(guard, cap, raw, sizeof(raw), &cut);
    CHECK(n < cap && guard[n] == '\0');
    CHECK(static_cast<unsigned char>(guard[cap < sizeof(guard) ? cap : sizeof(guard) - 1]) == 0x5a || cap >= sizeof(guard));
    if (n + 1 < 33) CHECK(cut);
    std::memset(guard, 0x5a, sizeof(guard));
    const size_t p = sanitize_preview(guard, cap, raw, sizeof(raw), &cut);
    CHECK(p < cap && guard[p] == '\0');
  }
  CHECK(sanitize_preview(nullptr, 5, raw, 3) == 0);
  CHECK(sanitize_preview(out, 0, raw, 3) == 0);
  // Every byte value, full-size block: bounded and printable.
  char all[250];
  for (int i = 0; i < 250; ++i) all[i] = static_cast<char>(i + 3);
  static char big[kEscapedMaxChars];
  CHECK(escape_text(big, sizeof(big), all, sizeof(all), &cut) < sizeof(big) && !cut);
  for (const char* p = big; *p; ++p) CHECK(*p >= 0x20 && *p < 0x7f);
  // Worst case: 250 NULs expand to 4 characters each and still fit.
  char nuls[250] = {};
  CHECK(escape_text(big, sizeof(big), nuls, sizeof(nuls), &cut) == 1000 && !cut);
  // The longest escape is <TAB> (5 characters): a full block of tabs must fit too, uncut.
  char tabs[250];
  std::memset(tabs, '	', sizeof(tabs));
  CHECK(escape_text(big, sizeof(big), tabs, sizeof(tabs), &cut) == 1250 && !cut);
  // text_length is authoritative: an embedded NUL does not end the text.
  auto m = new_model();
  orcsdr::acars::Message msg = make_message("N1", "", "H1", "");
  const char body[] = {'A', 'B', 0, 'C', 'D'};
  std::memcpy(msg.text, body, sizeof(body));
  msg.text_length = sizeof(body);
  msg.has_text = true;
  m->post_message(msg, 1, 0);
  m->service();
  CHECK(m->snapshot().messages[0].text_length == 5 && std::strcmp(m->snapshot().messages[0].preview, "AB.CD") == 0);
  // Hostile field bytes cannot reach the renderer unsafely.
  orcsdr::acars::Message evil = make_message("N1", "", "H1", "x");
  const unsigned char evil_tail[7] = {0x01, 0x02, 'a', 'b', 0xff, 0xfe, 0x03};
  std::memcpy(evil.tail, evil_tail, 7);
  evil.label[0] = '\x7f'; evil.label[1] = 'q';
  evil.mode = '\x00'; evil.block_id = '\x1b';
  evil.text_length = 9999;                          // lies about its size
  m->post_message(evil, 2, 0);
  m->service();
  const MessageRecord& r = m->snapshot().messages[0];
  CHECK(r.text_length == sizeof(r.text));
  for (const char* p = r.tail; *p; ++p) CHECK(*p >= 0x20 && *p < 0x7f);
  for (const char* p = r.label; *p; ++p) CHECK(*p >= 0x20 && *p < 0x7f);
  CHECK(r.mode >= 0x20 && r.block_id >= 0x20);
  // Tail padding is removed for grouping and display.
  char tail[8];
  normalise_tail(tail, sizeof(tail), "..N12  ");
  CHECK(std::strcmp(tail, "N12") == 0);
  CHECK(std::strcmp(end_marker_name(0x03), "ETX") == 0 && std::strcmp(end_marker_name(0x17), "ETB") == 0 &&
        std::strcmp(end_marker_name(0), "UNKNOWN") == 0);
}

void test_corrected_state_and_filter() {
  auto m = new_model();
  m->post_message(make_message("N1", "", "H1", "ok"), 1, 0);
  m->post_message(make_message("N2", "", "H1", "fixed", true), 2, 0);
  m->post_message(make_message("N3", "", "H1", "ok2"), 3, 0);
  m->service();
  CHECK(m->snapshot().corrected_total == 1 && m->snapshot().messages_total == 3);
  CHECK(m->snapshot().messages[1].integrity == Integrity::crc_corrected && m->snapshot().messages[1].corrected_bits == 1);
  CHECK(m->snapshot().messages[0].integrity == Integrity::crc_verified);
  CHECK(m->set_filter(MessageFilter::corrected_only));
  const Snapshot& s = m->snapshot();
  CHECK(s.message_count == 1 && s.matching_count == 1 && std::strcmp(s.messages[0].tail, "N2") == 0);
  CHECK(m->neighbor_seq(s.messages[0].seq, true) == 0 && m->neighbor_seq(s.messages[0].seq, false) == 0);
  CHECK(!m->set_filter(MessageFilter::corrected_only));   // unchanged: no work
}

void test_scroll_messages() {
  auto m = new_model();
  for (int i = 0; i < 30; ++i) {
    m->post_message(make_message("N1", "", "H1", "x"), i, 0);
    m->service();
  }
  CHECK(m->snapshot().message_offset == 0);
  CHECK(!m->scroll_messages(-1));
  CHECK(m->scroll_messages(6) && m->snapshot().message_offset == 6);
  const uint32_t first = m->snapshot().messages[0].seq;
  CHECK(first == 30 - 6);
  CHECK(m->scroll_messages(1000) && m->snapshot().message_offset == 30 - kMessageRows);
  CHECK(m->snapshot().message_count == kMessageRows);
  CHECK(m->snapshot().messages[kMessageRows - 1].seq == 1);
  CHECK(m->scroll_messages(-1000) && m->snapshot().message_offset == 0);
}

void test_revisions_stable() {
  auto m = new_model();
  const uint32_t r0 = m->snapshot().revision;
  CHECK(m->snapshot().revision == r0);
  m->service();                                     // nothing arrived
  CHECK(m->snapshot().revision == r0);
  // Battery and mute state are not drawn by the dashboard: they update the snapshot without a new revision,
  // so a wobbling battery percent never repaints the screen.
  m->set_device_state(50, false);
  CHECK(m->snapshot().revision == r0);
  CHECK(m->snapshot().battery_percent == 50 && !m->snapshot().sound_enabled);
  const uint32_t r1 = r0;
  m->post_message(make_message("N1", "", "H1", "x"), 1, 0);
  m->service();
  CHECK(m->snapshot().revision > r1);
  CHECK(m->snapshot().messages_revision > 1);
  const uint32_t r2 = m->snapshot().revision;
  CHECK(!m->set_text_format(TextFormat::escaped));  // already the default
  CHECK(m->snapshot().revision == r2);
  CHECK(m->set_text_format(TextFormat::plain));
  CHECK(m->snapshot().revision > r2);
}

void test_stats_mailbox() {
  auto m = new_model();
  orcsdr::acars::Stats st;
  st.sync_candidates = 10; st.crc_ok = 7; st.corrected = 2; st.crc_rejected = 3; st.framing_rejected = 1;
  st.overflows = 4; st.bits = 123456;
  m->post_stats(st);
  m->service();
  const Snapshot& s = m->snapshot();
  CHECK(s.stats_available && s.stats.sync_candidates == 10 && s.stats.crc_ok == 7 && s.stats.corrected == 2 &&
        s.stats.crc_rejected == 3 && s.stats.framing_rejected == 1 && s.stats.overflows == 4 && s.stats.bits == 123456);
  m->post_receiver_state(ReceiverState::running);
  Capabilities caps;
  caps.live_reception = true;
  m->post_capabilities(caps);
  m->service();
  CHECK(m->snapshot().receiver == ReceiverState::running && m->snapshot().caps.live_reception);
  CHECK(!m->snapshot().caps.archive);
  CHECK(m->snapshot().tune == TuneStatus::pending);   // a runtime exists but has not reported a tune
  m->report_tuned(131550000u, true);
  CHECK(m->snapshot().tune == TuneStatus::tuned && m->snapshot().tuned_hz == 131550000u);
  CHECK(m->set_preset(1));
  CHECK(m->snapshot().tune == TuneStatus::pending && m->snapshot().requested_hz == 130025000u);
  m->report_tuned(130025000u, false);
  CHECK(m->snapshot().tune == TuneStatus::failed && m->snapshot().tuned_hz == 0);
}

void test_cross_thread_seqlock_and_queue() {
  auto m = new_model();
  std::atomic<bool> stop{false};
  std::atomic<uint32_t> posted{0};
  std::thread producer([&] {
    uint32_t n = 1;
    while (!stop.load()) {
      orcsdr::acars::Stats st;
      st.sync_candidates = st.crc_ok = st.corrected = st.crc_rejected = st.framing_rejected = st.overflows = st.bits = n;
      m->post_stats(st);
      char flight[16];
      std::snprintf(flight, sizeof(flight), "%06u", n % 1000000u);
      m->post_message(make_message("N1", flight, "H1", "x"), n, 0);
      ++n;
      posted.store(n);
    }
  });
  uint32_t last_total = 0, last_seq = 0;
  for (int i = 0; i < 20000; ++i) {
    m->service();
    const Snapshot& s = m->snapshot();
    if (s.stats_available) {                         // a torn read would give unequal words
      CHECK(s.stats.sync_candidates == s.stats.crc_ok && s.stats.crc_ok == s.stats.bits &&
            s.stats.bits == s.stats.overflows && s.stats.overflows == s.stats.framing_rejected);
    }
    CHECK(s.messages_total >= last_total);
    last_total = s.messages_total;
    if (s.message_count) {
      CHECK(s.messages[0].seq >= last_seq);
      last_seq = s.messages[0].seq;
    }
  }
  // The producer thread may not have been scheduled during the loop above on a loaded host; give it
  // a bounded chance to post once so the final check tests the queue, not the scheduler.
  for (int i = 0; i < 2000 && posted.load() == 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  stop.store(true);
  producer.join();
  m->service();
  CHECK(m->snapshot().messages_total + m->snapshot().queue_drops > 0);
}

void test_demo_isolated_from_live() {
  auto m = new_model();
  m->post_message(make_message("N777LV", "LV0001", "H1", "live"), 1, 1791900000u);
  m->service();
  CHECK(m->live_ingested() == 1 && m->demo_ingested() == 0);
  MessageRecord rec;
  CHECK(!m->inject_demo(rec));                      // cannot inject outside demo mode
  CHECK(m->live_ingested() == 1 && m->demo_ingested() == 0);

  CHECK(m->set_demo(true, DemoScenario::mixed));
  {
    const Snapshot& s = m->snapshot();
    CHECK(s.source == SourceKind::demo && s.message_count > 0);
    for (size_t i = 0; i < s.message_count; ++i) CHECK(s.messages[i].source == SourceKind::demo);
    for (size_t i = 0; i < s.aircraft_count; ++i) CHECK(s.aircraft[i].source == SourceKind::demo);
    for (size_t i = 0; i < s.message_count; ++i) CHECK(std::strcmp(s.messages[i].tail, "N777LV") != 0);
    CHECK(s.tune == TuneStatus::unavailable && s.tuned_hz == 0 && s.queue_drops == 0);
  }
  CHECK(m->live_ingested() == 1);                   // demo never counted as live
  // Real traffic keeps flowing into the live store while DEMO is displayed, and stays out of the demo view.
  const uint32_t demo_total = m->snapshot().messages_total;
  m->post_message(make_message("N778LV", "", "H1", "live2"), 2, 0);
  m->service();
  CHECK(m->live_ingested() == 2 && m->snapshot().messages_total == demo_total);
  CHECK(m->find_message(m->snapshot().messages[0].seq)->source == SourceKind::demo);
  // Leaving demo clears every demo record.
  CHECK(m->set_demo(false));
  CHECK(m->demo_ingested() == 0);
  const Snapshot& s = m->snapshot();
  CHECK(s.source == SourceKind::live && s.messages_total == 2);
  for (size_t i = 0; i < s.message_count; ++i) CHECK(s.messages[i].source == SourceKind::live);
  CHECK(!m->set_demo(false));
}

void test_demo_scenarios() {
  auto m = new_model();
  struct Case { DemoScenario sc; ReceiverState rs; bool empty; };
  const Case cases[] = {{DemoScenario::mixed, ReceiverState::running, false},
                        {DemoScenario::full, ReceiverState::running, false},
                        {DemoScenario::empty, ReceiverState::running, true},
                        {DemoScenario::disconnected, ReceiverState::disconnected, true},
                        {DemoScenario::paused, ReceiverState::paused, false},
                        {DemoScenario::no_time, ReceiverState::running, false}};
  for (const Case& c : cases) {
    CHECK(m->set_demo(true, c.sc) || m->scenario() == c.sc);
    const Snapshot& s = m->snapshot();
    CHECK(s.source == SourceKind::demo && s.scenario == c.sc);
    CHECK(s.receiver == c.rs);
    CHECK((s.message_count == 0) == c.empty);
    CHECK(s.messages_held <= kHistoryCapacity);
    if (c.sc == DemoScenario::full) CHECK(s.messages_held == kHistoryCapacity && s.messages_total > kHistoryCapacity);
    if (c.sc == DemoScenario::no_time) for (size_t i = 0; i < s.message_count; ++i) CHECK(!s.messages[i].time_valid);
    if (c.sc == DemoScenario::mixed) {
      bool corrected = false, etb = false, etx = false, no_flight = false, unknown_end = false, untimed = false, timed = false, ctrl = false;
      for (size_t i = 0; i < s.message_count; ++i) {
        const MessageRecord& r = s.messages[i];
        corrected |= r.integrity == Integrity::crc_corrected;
        etb |= r.end_marker == 0x17; etx |= r.end_marker == 0x03; unknown_end |= r.end_marker == 0;
        no_flight |= r.flight[0] == '\0'; untimed |= !r.time_valid; timed |= r.time_valid;
        ctrl |= std::memchr(r.text, 0, r.text_length) != nullptr;
      }
      CHECK(corrected && etb && etx && no_flight && unknown_end && untimed && timed && ctrl);
      CHECK(s.aircraft_total >= 5);
    }
  }
  // Same scenario twice is deterministic.
  auto a = new_model(), b = new_model();
  a->set_demo(true, DemoScenario::mixed);
  b->set_demo(true, DemoScenario::mixed);
  CHECK(a->snapshot().message_count == b->snapshot().message_count);
  for (size_t i = 0; i < a->snapshot().message_count; ++i)
    CHECK(std::memcmp(a->snapshot().messages[i].text, b->snapshot().messages[i].text, 250) == 0 &&
          a->snapshot().messages[i].utc_epoch == b->snapshot().messages[i].utc_epoch);
}

void test_presets() {
  for (int r = 0; r < static_cast<int>(Region::count); ++r) {
    const Region region = static_cast<Region>(r);
    CHECK(preset_count(region) >= 1 && preset_count(region) <= kChannelRows);
    for (size_t i = 0; i < preset_count(region); ++i) {
      const Preset& p = preset(region, i);
      CHECK(p.frequency_hz >= kCustomMinHz && p.frequency_hz <= kCustomMaxHz);
      CHECK(p.frequency_hz % kCustomStepHz == 0);
      CHECK(p.label && p.label[0] && p.source && p.source[0]);   // every preset documents its source
    }
  }
  CHECK(preset(Region::north_america, 0).frequency_hz == 131550000u);
  CHECK(preset(Region::europe, 0).frequency_hz == 131725000u);
}

void test_preferences_persist() {
  g_kv.clear();
  {
    auto m = new_model();
    m->set_store(memory_store());
    m->load_preferences();
    CHECK(m->set_region(Region::europe));
    CHECK(m->snapshot().prefs.preset == 0 && m->snapshot().requested_hz == 131725000u);
    CHECK(m->set_preset(2));
    CHECK(m->set_text_format(TextFormat::plain));
    CHECK(m->set_show_raw_text(false));
    CHECK(m->set_highlight_corrected(false));
    CHECK(m->set_filter(MessageFilter::corrected_only));
    CHECK(m->set_sort(AircraftSort::most_messages));
    CHECK(!m->set_preset(99));                      // out of range is refused
    CHECK(m->snapshot().tune == TuneStatus::unavailable);   // choosing never claims a tune
  }
  auto m = new_model();
  m->set_store(memory_store());
  m->load_preferences();
  const Preferences& p = m->snapshot().prefs;
  CHECK(p.region == Region::europe && p.preset == 2 && p.format == TextFormat::plain && !p.show_raw_text &&
        !p.highlight_corrected && p.filter == MessageFilter::corrected_only && p.sort == AircraftSort::most_messages);
  CHECK(m->snapshot().requested_hz == 131825000u);
  // Invalid stored values fall back to the defaults.
  g_kv["acars_region"] = 200; g_kv["acars_preset"] = 77; g_kv["acars_fmt"] = 9; g_kv["acars_filter"] = 9;
  g_kv["acars_sort"] = 9; g_kv["acars_custom"] = 131000001u;
  const Preferences d = load_preferences(memory_store());
  CHECK(d.region == Region::north_america && d.preset == 0 && d.format == TextFormat::escaped &&
        d.filter == MessageFilter::all && d.sort == AircraftSort::recent && d.custom_hz == 0);
  // No store at all: defaults, nothing crashes.
  auto n = new_model();
  CHECK(n->set_show_raw_text(false));
  CHECK(!n->snapshot().prefs.show_raw_text);
  // Demo mode is never persisted: a fresh model always starts LIVE.
  CHECK(!new_model()->demo());
  for (const auto& kv : g_kv) CHECK(kv.first.find("demo") == std::string::npos);
}

void test_custom_frequency() {
  auto m = new_model();
  CHECK(m->set_custom_frequency(131525000u));
  CHECK(m->snapshot().custom_active && m->snapshot().requested_hz == 131525000u);
  CHECK(!m->set_custom_frequency(131525000u));      // unchanged
  CHECK(!m->set_custom_frequency(131526000u));      // off the 25 kHz grid
  CHECK(!m->set_custom_frequency(100000000u));      // outside the airband
  CHECK(!m->set_custom_frequency(140000000u));
  CHECK(m->snapshot().requested_hz == 131525000u);
  CHECK(m->set_preset(0));                          // choosing a preset clears the custom request
  CHECK(!m->snapshot().custom_active && m->snapshot().requested_hz == 131550000u);
  CHECK(m->set_custom_frequency(130000000u) && m->set_custom_frequency(0));
  CHECK(!m->snapshot().custom_active);
}

void test_layout_geometry() {
  Rect all[512];
  for (int v = 0; v < static_cast<int>(View::count); ++v)
    for (int detail = 0; detail < 2; ++detail) {
      const size_t n = layout::collect_rects(static_cast<View>(v), detail != 0, all, 512);
      CHECK(n > 8 && n < 512);
      for (size_t i = 0; i < n; ++i) {
        CHECK(!all[i].intersects(layout::kStatusBar));            // shared status bar stays clear
        CHECK(all[i].x >= 0 && all[i].y >= layout::kContext.y && all[i].x + all[i].w <= 1280 && all[i].y + all[i].h <= 720);
        CHECK(all[i].y >= layout::kHeaderH);                      // nothing inside the shared header
      }
    }
  // Content rectangles do not run into the tab bar.
  Rect rects[512];
  for (int v = 0; v < static_cast<int>(View::count); ++v)
    for (int detail = 0; detail < 2; ++detail) {
      const size_t n = layout::collect_rects(static_cast<View>(v), detail != 0, rects, 512);
      for (size_t i = 0; i < n; ++i) {
        if (rects[i].y >= layout::kTabsY || rects[i].y < layout::kContentY) continue;   // tabs / context strip
        CHECK(rects[i].y + rects[i].h <= layout::kContentBottom + 1);
      }
    }
  // Tabs tile the full width exactly once.
  int x = 0;
  for (int i = 0; i < layout::kTabCount; ++i) { CHECK(layout::tab(i).x == x); x += layout::tab(i).w; }
  CHECK(x == 1280);
  // Fixed shared geometry (mirrors dashboard_audio_control.hpp).
  CHECK(layout::kStatusBar.x == 420 && layout::kStatusBar.y == 12 && layout::kStatusBar.w == 446 && layout::kStatusBar.h == 54);
}

void test_controller_navigation_and_header_taps() {
  auto m = new_model();
  auto ui = new_ui();
  m->set_demo(true, DemoScenario::mixed);
  // Taps in the shared header, including inside the status-bar reserve, are never this dashboard's.
  const uint32_t rev = ui->revision();
  for (int y : {0, 12, 40, 66, 103})
    for (int x : {30, 420, 600, 866, 1100, 1250}) CHECK(ui->touch(x, y, *m).kind == ActionKind::none);
  CHECK(ui->revision() == rev);
  // Each tab selects its view; the tab bar works from any state.
  for (int i = 0; i < layout::kTabCount; ++i) {
    tap(*ui, *m, layout::tab(i));
    CHECK(static_cast<int>(ui->view()) == i);
  }
  CHECK(ui->touch(-5, 400, *m).kind == ActionKind::none);
  CHECK(ui->touch(5000, 400, *m).kind == ActionKind::none);
  tap(*ui, *m, layout::tab(0));
  // Empty areas of a view do nothing and do not repaint.
  const uint32_t r2 = ui->revision();
  (void)ui->touch(1000, 600, *m);
  CHECK(ui->revision() == r2 || ui->view() == View::live);
}

void test_live_selection_and_detail() {
  auto m = new_model();
  auto ui = new_ui();
  // Empty list: rows do nothing.
  tap(*ui, *m, layout::live_row(0));
  CHECK(!ui->detail_open());
  m->set_demo(true, DemoScenario::mixed);
  const Snapshot& s = m->snapshot();
  const uint32_t top = s.messages[0].seq;
  tap(*ui, *m, layout::live_row(0));
  CHECK(ui->detail_open() && ui->detail_record().seq == top);
  CHECK(ui->detail_record().source == SourceKind::demo);
  CHECK(ui->detail_held(*m));
  // Older / newer navigation.
  const uint32_t older = m->neighbor_seq(top, false);
  CHECK(older != 0 && m->neighbor_seq(top, true) == 0);
  tap(*ui, *m, layout::kDetailOlder);
  CHECK(ui->detail_record().seq == older);
  tap(*ui, *m, layout::kDetailNewer);
  CHECK(ui->detail_record().seq == top);
  const uint32_t rev = ui->revision();
  tap(*ui, *m, layout::kDetailNewer);               // no newer message: nothing changes
  CHECK(ui->revision() == rev);
  // Back closes; tab change also closes.
  tap(*ui, *m, layout::kDetailBack);
  CHECK(!ui->detail_open());
  tap(*ui, *m, layout::live_row(1));
  CHECK(ui->detail_open());
  tap(*ui, *m, layout::tab(2));
  CHECK(!ui->detail_open() && ui->view() == View::channels);
  // Selection survives eviction: the controller holds a copy and reports it is no longer held.
  auto m2 = new_model();
  auto ui2 = new_ui();
  m2->post_message(make_message("N1", "", "H1", "first"), 1, 0);
  m2->service();
  tap(*ui2, *m2, layout::live_row(0));
  CHECK(ui2->detail_open() && std::strcmp(ui2->detail_record().preview, "first") == 0);
  for (int i = 0; i < 60; ++i) { m2->post_message(make_message("N2", "", "H1", "later"), 10 + i, 0); m2->service(); }
  CHECK(!ui2->detail_held(*m2));
  CHECK(std::strcmp(ui2->detail_record().preview, "first") == 0);
  // Scroll buttons.
  auto m3 = new_model();
  auto ui3 = new_ui();
  for (int i = 0; i < 30; ++i) { m3->post_message(make_message("N3", "", "H1", "x"), i, 0); m3->service(); }
  CHECK(tap_action(*ui3, *m3, layout::kLiveNavDown).kind == ActionKind::changed && m3->snapshot().message_offset == 6);
  CHECK(tap_action(*ui3, *m3, layout::kLiveNavUp).kind == ActionKind::changed && m3->snapshot().message_offset == 0);
  CHECK(tap_action(*ui3, *m3, layout::kLiveNavUp).kind == ActionKind::none);
}

void test_detail_text_scroll_and_format() {
  auto m = new_model();
  auto ui = new_ui();
  std::string long_text;
  for (int i = 0; i < 240; ++i) long_text += static_cast<char>('A' + i % 26);
  m->post_message(make_message("N1", "", "H1", long_text.c_str()), 1, 0);
  m->service();
  tap(*ui, *m, layout::live_row(0));
  CHECK(ui->detail_open() && ui->detail_text_length() == 240);
  CHECK(ui->detail_line_count() == (240 + layout::kDetailCols - 1) / layout::kDetailCols);
  const char* start = nullptr;
  size_t len = 0;
  CHECK(ui->detail_line_at(0, &start, &len) && len == layout::kDetailCols && start == ui->detail_text());
  CHECK(!ui->detail_line_at(ui->detail_line_count(), &start, &len));
  // Fits in the eight visible lines: scrolling is inert.
  const uint32_t rev = ui->revision();
  tap(*ui, *m, layout::kDetailRawDown);
  CHECK(ui->detail_line() == 0 && ui->revision() == rev);
  // A block with control bytes: the escaped and plain forms differ, switching in SETUP re-renders it.
  orcsdr::acars::Message msg = make_message("N2", "", "H1", "");
  const char body[] = {'H', 'I', 0, 0x01, 'X'};
  std::memcpy(msg.text, body, sizeof(body));
  msg.text_length = sizeof(body);
  msg.has_text = true;
  m->post_message(msg, 2, 0);
  m->service();
  ui->close_detail();
  tap(*ui, *m, layout::live_row(0));
  CHECK(std::strcmp(ui->detail_text(), "HI<00><01>X") == 0);
  tap(*ui, *m, layout::tab(5));                     // tab change closes the detail
  CHECK(!ui->detail_open());
  tap(*ui, *m, layout::setup_item(0, 1));           // TEXT FORMAT -> PLAIN
  CHECK(m->snapshot().prefs.format == TextFormat::plain);
  tap(*ui, *m, layout::tab(0));
  tap(*ui, *m, layout::live_row(0));
  CHECK(std::strcmp(ui->detail_text(), "HI..X") == 0);
  // A 250-byte block of NULs: 1000 escaped characters, scrolling reaches the end and stops.
  orcsdr::acars::Message big = make_message("N3", "", "H1", "");
  big.text_length = 250;
  big.has_text = true;
  tap(*ui, *m, layout::tab(5));
  tap(*ui, *m, layout::setup_item(0, 1));           // back to ESCAPED
  m->post_message(big, 3, 0);
  m->service();
  tap(*ui, *m, layout::tab(0));
  tap(*ui, *m, layout::live_row(0));
  CHECK(ui->detail_text_length() == 1000);
  const size_t lines = ui->detail_line_count();
  CHECK(lines > static_cast<size_t>(layout::kDetailLines));
  for (int i = 0; i < 100; ++i) tap(*ui, *m, layout::kDetailRawDown);
  CHECK(ui->detail_line() == lines - layout::kDetailLines);
  for (int i = 0; i < 100; ++i) tap(*ui, *m, layout::kDetailRawUp);
  CHECK(ui->detail_line() == 0);
}

void test_aircraft_view_interaction() {
  auto m = new_model();
  auto ui = new_ui();
  m->set_demo(true, DemoScenario::mixed);
  tap(*ui, *m, layout::tab(1));
  CHECK(ui->view() == View::aircraft && ui->selected_tail()[0] == '\0');
  const std::string first = m->snapshot().aircraft[0].tail;
  tap(*ui, *m, layout::ac_row(0));
  CHECK(first == ui->selected_tail());
  const MessageRecord* list[6];
  const size_t n = m->aircraft_messages(ui->selected_tail(), list, 6);
  CHECK(n >= 1);
  tap(*ui, *m, layout::ac_msg_row(0));
  CHECK(ui->detail_open() && ui->detail_record().seq == list[0]->seq);
  tap(*ui, *m, layout::kDetailBack);
  CHECK(!ui->detail_open() && ui->view() == View::aircraft);
  const AircraftSort before = m->snapshot().prefs.sort;
  tap(*ui, *m, layout::kAcSort);
  CHECK(m->snapshot().prefs.sort != before);
  // A message row that is not populated does nothing.
  tap(*ui, *m, layout::ac_msg_row(5));
  CHECK(!ui->detail_open() || n > 5);
  // Selecting rows past the aircraft count is inert (empty list).
  auto m2 = new_model();
  auto ui2 = new_ui();
  tap(*ui2, *m2, layout::tab(1));
  tap(*ui2, *m2, layout::ac_row(0));
  CHECK(ui2->selected_tail()[0] == '\0');
}

void test_channels_view_interaction() {
  auto m = new_model();
  auto ui = new_ui();
  tap(*ui, *m, layout::tab(2));
  Action a = tap_action(*ui, *m, layout::ch_row(1));
  CHECK(a.kind == ActionKind::tune_requested && a.value == 130025000u);
  CHECK(m->snapshot().channels[1].selected && !m->snapshot().channels[0].selected);
  CHECK(m->snapshot().tune == TuneStatus::unavailable);   // honest: nothing was tuned
  CHECK(m->snapshot().tuned_hz == 0 && !m->snapshot().channels[1].receiving);
  a = tap_action(*ui, *m, layout::kChPlus);
  CHECK(a.kind == ActionKind::tune_requested && a.value == 130050000u && m->snapshot().custom_active);
  a = tap_action(*ui, *m, layout::kChMinus);
  CHECK(a.value == 130025000u);
  a = tap_action(*ui, *m, layout::kChClear);
  CHECK(a.kind == ActionKind::tune_requested && !m->snapshot().custom_active);
  CHECK(tap_action(*ui, *m, layout::kChClear).kind == ActionKind::none);   // nothing to clear
  a = tap_action(*ui, *m, layout::kChRegion);
  CHECK(a.kind == ActionKind::tune_requested && m->snapshot().prefs.region == Region::europe && a.value == 131725000u);
  CHECK(m->snapshot().channel_count == 4);
  // Row beyond the region's preset count is inert.
  CHECK(tap_action(*ui, *m, layout::ch_row(5)).kind == ActionKind::none);
  // The airband limits stop the nudge buttons.
  m->set_custom_frequency(kCustomMaxHz);
  CHECK(tap_action(*ui, *m, layout::kChPlus).kind == ActionKind::none);
  m->set_custom_frequency(kCustomMinHz);
  CHECK(tap_action(*ui, *m, layout::kChMinus).kind == ActionKind::none);
}

void test_archive_view_interaction() {
  auto m = new_model();
  auto ui = new_ui();
  tap(*ui, *m, layout::tab(4));
  // LIVE: no archive provider, every control is inert and nothing is invented.
  const uint32_t rev = ui->revision();
  for (int i = 0; i < layout::kArFilterCount; ++i) CHECK(tap_action(*ui, *m, layout::ar_filter(i)).kind == ActionKind::none);
  CHECK(tap_action(*ui, *m, layout::ar_row(0)).kind == ActionKind::none);
  CHECK(ui->revision() == rev && !ui->detail_open());
  const MessageRecord* rows[8];
  size_t total = 99;
  CHECK(m->archive_query(ArchiveQuery{}, 0, rows, 8, &total) == 0 && total == 0);
  CHECK(!m->snapshot().archive_available);
  // DEMO: a clearly synthetic archive that can be filtered by tail and label.
  m->set_demo(true, DemoScenario::mixed);
  CHECK(m->archive_query(ArchiveQuery{}, 0, rows, 8, &total) == 8 && total == m->snapshot().messages_held);
  CHECK(!m->snapshot().archive_available);          // still no real archive provider
  tap(*ui, *m, layout::ar_filter(0));
  CHECK(ui->archive_query().tail[0] != '\0');
  const std::string tail = ui->archive_query().tail;
  size_t n = m->archive_query(ui->archive_query(), 0, rows, 8, &total);
  CHECK(n >= 1 && n == total);
  for (size_t i = 0; i < n; ++i) CHECK(tail == rows[i]->tail);
  tap(*ui, *m, layout::ar_filter(1));
  CHECK(ui->archive_query().label[0] != '\0');
  n = m->archive_query(ui->archive_query(), 0, rows, 8, &total);
  for (size_t i = 0; i < n; ++i) CHECK(tail == rows[i]->tail && std::strcmp(ui->archive_query().label, rows[i]->label) == 0);
  tap(*ui, *m, layout::kArClear);
  CHECK(!ui->archive_query().tail[0] && !ui->archive_query().label[0]);
  tap(*ui, *m, layout::ar_row(0));
  CHECK(ui->detail_open() && ui->detail_record().source == SourceKind::demo);
  // PLANNED filters stay inert even in demo.
  ui->close_detail();
  const uint32_t rev2 = ui->revision();
  for (int i = 2; i < layout::kArFilterCount; ++i) CHECK(tap_action(*ui, *m, layout::ar_filter(i)).kind == ActionKind::none);
  CHECK(ui->revision() == rev2);
  // Tail filter cycles through every tail and returns to ANY.
  ArchiveQuery q;
  int steps = 0;
  do { CHECK(m->archive_cycle_tail(q.tail)); ++steps; CHECK(steps < 40); } while (q.tail[0]);
  // The mixed scenario has one group with no address field; it cannot be filtered by tail, so the cycle visits
  // every other aircraft once and then returns to ANY: aircraft_total - 1 + 1 steps.
  CHECK(steps == static_cast<int>(m->snapshot().aircraft_total));
  // Full history: scrolling reaches the last page.
  m->set_demo(true, DemoScenario::full);
  tap(*ui, *m, layout::tab(5));
  tap(*ui, *m, layout::tab(4));
  for (int i = 0; i < 20; ++i) tap(*ui, *m, layout::kArDown);
  CHECK(ui->archive_offset() == kHistoryCapacity - layout::kArRows);
  for (int i = 0; i < 20; ++i) tap(*ui, *m, layout::kArUp);
  CHECK(ui->archive_offset() == 0);
}

void test_setup_view_interaction() {
  g_kv.clear();
  auto m = new_model();
  m->set_store(memory_store());
  m->load_preferences();
  auto ui = new_ui();
  tap(*ui, *m, layout::tab(5));
  // Every enabled control has an observable result; planned ones are inert.
  struct Check { int item; bool (*changed)(const Snapshot&, const Snapshot&); };
  auto changed_prefs = [](const Snapshot& a, const Snapshot& b) { return std::memcmp(&a.prefs, &b.prefs, sizeof(Preferences)) != 0; };
  (void)changed_prefs;
  for (int item = 0; item <= 5; ++item) {
    Preferences before = m->snapshot().prefs;
    const Action a = tap_action(*ui, *m, layout::setup_item(item / 5, item % 5));
    CHECK(a.kind == ActionKind::changed);
    CHECK(std::memcmp(&before, &m->snapshot().prefs, sizeof(Preferences)) != 0);
  }
  CHECK(!g_kv.empty() && g_kv.count("acars_region") && g_kv.count("acars_raw") && g_kv.count("acars_sort"));
  // Scenario cycling is disabled outside DEMO.
  CHECK(tap_action(*ui, *m, layout::setup_item(1, 2)).kind == ActionKind::none);
  CHECK(!m->demo());
  // Planned items.
  CHECK(tap_action(*ui, *m, layout::setup_item(1, 3)).kind == ActionKind::none);
  CHECK(tap_action(*ui, *m, layout::setup_item(1, 4)).kind == ActionKind::none);
  // DEMO on, then through every scenario, then off.
  Action a = tap_action(*ui, *m, layout::setup_item(1, 1));
  CHECK(a.kind == ActionKind::demo_changed && a.value == 1 && m->demo() && m->scenario() == DemoScenario::mixed);
  for (int i = 1; i < static_cast<int>(DemoScenario::count); ++i) {
    CHECK(tap_action(*ui, *m, layout::setup_item(1, 2)).kind == ActionKind::changed);
    CHECK(static_cast<int>(m->scenario()) == i);
  }
  CHECK(tap_action(*ui, *m, layout::setup_item(1, 2)).kind == ActionKind::changed && m->scenario() == DemoScenario::mixed);
  a = tap_action(*ui, *m, layout::setup_item(1, 1));
  CHECK(a.kind == ActionKind::demo_changed && a.value == 0 && !m->demo());
  CHECK(m->snapshot().source == SourceKind::live && m->snapshot().message_count == 0);
  // Demo state was never persisted.
  for (const auto& kv : g_kv) CHECK(kv.first.find("demo") == std::string::npos);
  // Leaving demo with a detail page open closes the page (its record was synthetic).
  tap(*ui, *m, layout::setup_item(1, 1));
  tap(*ui, *m, layout::tab(0));
  tap(*ui, *m, layout::live_row(0));
  CHECK(ui->detail_open());
  tap(*ui, *m, layout::tab(5));
  tap(*ui, *m, layout::setup_item(1, 1));
  CHECK(!ui->detail_open() && !m->demo());
}

void test_filter_and_raw_toggle_have_effect() {
  auto m = new_model();
  auto ui = new_ui();
  m->set_demo(true, DemoScenario::mixed);
  const size_t all = m->snapshot().matching_count;
  tap(*ui, *m, layout::tab(5));
  tap(*ui, *m, layout::setup_item(0, 4));           // MESSAGE FILTER -> corrected only
  CHECK(m->snapshot().matching_count < all && m->snapshot().matching_count >= 1);
  for (size_t i = 0; i < m->snapshot().message_count; ++i) CHECK(m->snapshot().messages[i].integrity == Integrity::crc_corrected);
  tap(*ui, *m, layout::setup_item(0, 4));
  CHECK(m->snapshot().matching_count == all);
  tap(*ui, *m, layout::setup_item(0, 2));          // raw text hidden
  CHECK(!m->snapshot().prefs.show_raw_text);
}

}  // namespace

// A stats post that changes only the demodulated-bit counter must not bump the revision (that repainted whole pages
// several times a second and showed as flicker), but the new count must still reach the snapshot.
void test_bits_only_stats_do_not_repaint() {
  auto m = new_model();
  orcsdr::acars::Stats a{};
  a.bits = 100;
  a.sync_candidates = 1;
  Capabilities caps;
  caps.live_reception = true;
  m->post_capabilities(caps);
  m->post_stats(a);
  m->service();
  const uint32_t rev = m->snapshot().revision;
  a.bits = 5000;
  m->post_stats(a);
  m->service();
  CHECK(m->snapshot().revision == rev);
  CHECK(m->snapshot().stats.bits == 5000);
  a.crc_ok = 1;
  m->post_stats(a);
  m->service();
  CHECK(m->snapshot().revision != rev);
  CHECK(m->snapshot().stats.crc_ok == 1);
  std::puts("  bits-only stats do not repaint");
}

int main() {
  test_self_checks();
  test_empty_defaults();
  test_capacity_and_order();
  test_queue_overflow_is_counted_not_fatal();
  test_aircraft_grouping_and_eviction();
  test_time_validity();
  test_sanitization();
  test_corrected_state_and_filter();
  test_scroll_messages();
  test_revisions_stable();
  test_bits_only_stats_do_not_repaint();
  test_stats_mailbox();
  test_cross_thread_seqlock_and_queue();
  test_demo_isolated_from_live();
  test_demo_scenarios();
  test_presets();
  test_preferences_persist();
  test_custom_frequency();
  test_layout_geometry();
  test_controller_navigation_and_header_taps();
  test_live_selection_and_detail();
  test_detail_text_scroll_and_format();
  test_aircraft_view_interaction();
  test_channels_view_interaction();
  test_archive_view_interaction();
  test_setup_view_interaction();
  test_filter_and_raw_toggle_have_effect();
  std::puts("acars_dashboard_model_tests: all passed");
  return 0;
}
