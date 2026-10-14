// Host regression for SSTV settings: persistence, sanitising, and what each detection
// strategy really does to the receiver.
#include "sstv_settings.hpp"
#include "sstv_test_signal.hpp"
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

using namespace orcsdr::sstv;
using namespace orcsdr::sstv_settings;
using sstv_test::require;
using sstv_test::Signal;

namespace {
std::map<std::string, int> g_kv;
bool get_bool(const char* k, bool d) { auto it = g_kv.find(k); return it == g_kv.end() ? d : it->second != 0; }
void put_bool(const char* k, bool v) { g_kv[k] = v ? 1 : 0; }
uint8_t get_u8(const char* k, uint8_t d) { auto it = g_kv.find(k); return it == g_kv.end() ? d : static_cast<uint8_t>(it->second); }
void put_u8(const char* k, uint8_t v) { g_kv[k] = v; }
const Store kStore = {get_bool, put_bool, get_u8, put_u8};

void restart() {
  stop();
  clear_gallery();
  start(145800000);
}
}  // namespace

int main() {
  // Defaults on an empty store: automatic, Robot 36 remembered, auto-save on.
  g_kv.clear();
  Settings s = load(kStore);
  require(s.detection == Detection::automatic && s.manual_mode == Mode::robot36 && s.autosave,
          "defaults: automatic detection, auto-save on");

  // Round trip.
  s = with_manual_mode(s, Mode::pd120);
  s.autosave = false;
  save(kStore, s);
  Settings back = load(kStore);
  require(back.detection == Detection::manual && back.manual_mode == Mode::pd120 && !back.autosave,
          "round trip: manual PD 120, auto-save off");

  // The first release stored only sstv_autosave; that value is honoured.
  g_kv.clear();
  g_kv["sstv_autosave"] = 0;
  require(!load(kStore).autosave && load(kStore).detection == Detection::automatic, "legacy key still read");

  // Corrupt values fall back to safe defaults instead of selecting a bogus mode.
  g_kv.clear();
  g_kv["sstv_detect"] = 9;
  g_kv["sstv_manual"] = 77;
  s = load(kStore);
  require(s.detection == Detection::automatic && s.manual_mode == Mode::robot36, "corrupt values sanitised");
  require(!valid_manual_mode(0) && valid_manual_mode(1) && valid_manual_mode(14) && !valid_manual_mode(15),
          "manual mode range is the 14 concrete modes");
  require(with_manual_mode(Settings{}, Mode::auto_vis).detection == Detection::automatic,
          "auto_vis is not a manual choice");

  // What each strategy does to the receiver.
  Settings automatic;
  apply(automatic);
  require(selected_mode() == Mode::auto_vis && auto_strategy() == AutoStrategy::vis_sync, "apply: automatic");
  apply(with_detection(automatic, Detection::vis_only));
  require(selected_mode() == Mode::auto_vis && auto_strategy() == AutoStrategy::vis_only, "apply: VIS only");
  apply(with_manual_mode(automatic, Mode::scottie_s1));
  require(selected_mode() == Mode::scottie_s1 && auto_strategy() == AutoStrategy::vis_only,
          "apply: manual is a VIS filter without sync fallback");

  // Behaviour, not just flags. A transmission with no header:
  restart();
  apply(automatic);
  Signal a;
  a.robot36_rows(0, 120);
  a.tone(1900, 1700);   // longer than the 8 missed lines (1.2 s of Robot 36) that end a picture
  a.flush();
  require(archive_count() == 1 && archived_image(0).identification == Identification::sync,
          "automatic: headerless Robot 36 recovered from sync, labelled sync");
  restart();
  apply(with_detection(automatic, Detection::vis_only));
  Signal b;
  b.robot36_rows(0, 120);
  b.tone(1900, 1700);   // longer than the 8 missed lines (1.2 s of Robot 36) that end a picture
  b.flush();
  require(archive_count() == 0, "VIS only: headerless transmission is not decoded");
  restart();
  apply(with_manual_mode(automatic, Mode::pd120));
  Signal c;
  c.header(8);
  c.robot36_rows(0, 120);
  c.tone(1900, 1700);   // longer than the 8 missed lines (1.2 s of Robot 36) that end a picture
  c.flush();
  require(archive_count() == 0, "manual PD 120: a Robot 36 header is ignored");
  c.silence(500);
  c.header(95);
  c.pd120_pairs(0, 10);
  c.flush();
  require(metrics().receiving && metrics().received_mode == Mode::pd120, "manual PD 120: its own header starts it");

  // Applying settings mid-image must not discard the image or restart the receiver.
  restart();
  apply(automatic);
  Signal d;
  d.header(8);
  d.robot36_rows(0, 50);
  d.flush();
  const uint32_t session = metrics().session_id;
  require(metrics().receiving && session != 0, "mid-image: receiving before the change");
  apply(with_detection(automatic, Detection::vis_only));
  apply(automatic);
  require(metrics().receiving && metrics().session_id == session && active(),
          "mid-image: changing detection keeps the image and the receiver running");
  stop();
  clear_gallery();
  std::puts("SSTV settings: PASS");
  return 0;
}
