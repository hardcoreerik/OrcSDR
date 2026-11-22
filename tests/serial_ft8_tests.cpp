// Host tests for the FT8 serial command modules (serial_ft8, serial_ft8_query, serial_ft8_data, serial_ft8_wwv,
// serial_ft8_control, serial_ft8_io). Expected text is typed from process_ft8_command() in main.cpp as it was before
// the move (and checked against replies recorded from firmware 39055f2c on a Tab5). Fake hooks record every write, so
// the tests also pin the write boundaries: printf was one write, println was text then a separate newline.
// Build with g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -Iapps/orcsdr-tab5/ui, from the
// repository root: tests/serial_ft8_tests.cpp apps/orcsdr-tab5/ui/serial_ft8*.cpp
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "serial_ft8.hpp"

namespace {
int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

using namespace orcsdr;

std::vector<std::string> g_out;   // one entry per console write; write_line adds the text, then "\n"
std::vector<std::string> g_calls;

void w(const char* text) { g_out.push_back(text); }
void wl(const char* text) { g_out.push_back(text); g_out.push_back("\n"); }
constexpr serial_ft8::Io kIo{w, wl};

std::string all() { std::string s; for (const auto& t : g_out) s += t; return s; }
void reset() { g_out.clear(); g_calls.clear(); }
bool has(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

// ---- fake device ----
std::vector<std::string> g_decodes = {"d0", "d1", "d2"};
bool g_sd = true, g_adif_ok = true, g_wav_ok = true, g_shot_ok = true, g_slot = true, g_start_ok = true;
bool g_decoder_active = true, g_inject_ok = true, g_inject_write_ok = true;
int16_t g_samples[7] = {1, -2, 3, -4, 5, -6, 7};
serial_ft8_wwv::State g_wwv_state;

serial_ft8_query::Status g_status;
const serial_ft8_query::Hooks kQuery{
    kIo,
    [](serial_ft8_query::Status* s) { g_calls.push_back("status"); *s = g_status; },
    [](unsigned long long* ms, bool* valid) { *ms = 1700000000123ull; *valid = true; },
    []() -> size_t { return 3; },
    [](size_t i, const char** label, unsigned long* hz) -> bool {
      if (i == 1) return false;                       // an entry the table does not have is skipped
      *label = i == 0 ? "160m" : "40m";
      *hz = i == 0 ? 1840000ul : 7074000ul;
      return true;
    },
    [] { g_calls.push_back("drain"); },
    []() -> size_t { return g_decodes.size(); },
    [](size_t i) -> bool {
      if (i >= g_decodes.size()) return false;
      w((std::string("ORC_FT8_DECODE ") + g_decodes[i] + "\n").c_str());
      return true;
    },
    [](serial_ft8_query::HuntSummary* s) { *s = {"FAST", "scan", -1, 3, 4}; },
    [](size_t i, serial_ft8_query::HuntBand* b) -> bool {
      if (i != 2) return false;
      *b = {"20m", true, "strong", 4, 2, 1, -61.25};
      return true;
    },
    [](serial_ft8_query::Clock* c) { *c = {2, 90, 1700000000ul, 1700000001ul, true, false, true, -35, 7, true}; },
};

const serial_ft8_data::Hooks kData{
    kIo,
    [](const int16_t** samples, size_t* count, uint64_t* slot_ms) -> bool {
      if (!g_slot) return false;
      *samples = g_samples; *count = 7; *slot_ms = 1700000015000ull;
      return true;
    },
    [] { g_calls.push_back("release"); },
    [] { g_calls.push_back("yield"); },
    [] { g_calls.push_back("drain"); },
    []() -> bool { return g_sd; },
    [](const char* path, unsigned* records) -> bool {
      g_calls.push_back(std::string("adif ") + path);
      *records = 5;
      return g_adif_ok;
    },
    [](const char* path, const int16_t*, size_t count) -> bool {
      g_calls.push_back(std::string("wav ") + path + " " + std::to_string(count));
      return g_wav_ok;
    },
    [](const char* slug, const char** error, unsigned* bytes) -> bool {
      g_calls.push_back(std::string("shot ") + slug);
      *bytes = 921654;
      if (!g_shot_ok) *error = nullptr;
      return g_shot_ok;
    },
    [] { g_calls.push_back("png"); },
    [](size_t n) -> bool { g_calls.push_back("begin " + std::to_string(n)); return g_inject_ok; },
    []() -> size_t { return 4800; },
    []() -> unsigned long { return 0xdeadbeefUL; },
    [](serial_ft8_data::InjectMode m) -> bool {
      g_calls.push_back(std::string("run ") + (m == serial_ft8_data::InjectMode::ft4 ? "ft4" : m == serial_ft8_data::InjectMode::js8 ? "js8" : "ft8"));
      return g_inject_ok;
    },
    [](unsigned long offset, const int16_t* s, size_t count) -> bool {
      g_calls.push_back("write " + std::to_string(offset) + " " + std::to_string(count) + " " + std::to_string(count ? s[0] : 0));
      return g_inject_write_ok;
    },
};

const serial_ft8_wwv::Hooks kWwv{
    kIo,
    [](serial_ft8_wwv::State* s) { *s = g_wwv_state; },
    [] { g_calls.push_back("wwv stop"); },
    [](float mhz) -> bool { return mhz == 2.5f || mhz == 5.0f || mhz == 10.0f || mhz == 15.0f || mhz == 20.0f; },
    []() -> bool { return g_decoder_active; },
    [](float mhz, uint32_t dial, uint32_t seconds, serial_ft8_wwv::Action action) -> bool {
      g_calls.push_back("wwv start " + std::to_string(mhz).substr(0, 4) + " " + std::to_string(dial) + " " +
                        std::to_string(seconds) + " " + std::to_string(static_cast<int>(action)));
      return g_start_ok;
    },
};

serial_ft8_control::LogStatus g_log{true, 12, 3, 1, 2, 18432, 4096};
bool g_ok = true;
double g_snr = 1.5;
const serial_ft8_control::Hooks kControl{
    kIo,
    [] { g_calls.push_back("open"); },
    [](const char* text, size_t* index) -> bool {
      if (strcmp(text, "40m") == 0) { *index = 3; return true; }
      if (strcmp(text, "7074000") == 0) { *index = 3; return true; }
      return false;
    },
    [](size_t index) -> bool { g_calls.push_back("select " + std::to_string(index)); return g_ok; },
    [](size_t, const char** label, unsigned long* hz) { *label = "40m"; *hz = 7074000; },
    [](serial_ft8_control::Mode m, const char** name, unsigned long* hz) {
      g_calls.push_back(m == serial_ft8_control::Mode::ft4 ? "mode ft4" : "mode ft8");
      *name = m == serial_ft8_control::Mode::ft4 ? "FT4" : "FT8";
      *hz = m == serial_ft8_control::Mode::ft4 ? 7047500 : 7074000;
    },
    [] { g_calls.push_back("clear"); },
    []() -> bool { g_calls.push_back("run start"); return g_start_ok; },
    [] { g_calls.push_back("run stop"); },
    [](size_t i) { g_calls.push_back("tab " + std::to_string(i)); },
    [](bool fast) -> bool { g_calls.push_back(fast ? "hunt fast" : "hunt decode"); return g_ok; },
    [] { g_calls.push_back("hunt stop"); },
    [](uint32_t hz) -> bool { g_calls.push_back("dial " + std::to_string(hz)); return g_ok; },
    [] { g_calls.push_back("clocktest release"); },
    [](long d) -> bool { g_calls.push_back("clocktest " + std::to_string(d)); return g_ok; },
    []() -> bool { g_calls.push_back("ntp"); return g_ok; },
    [](float db) { g_calls.push_back("snr set " + std::to_string(db).substr(0, 4)); g_snr = db; },
    [] { g_calls.push_back("snr reset"); g_snr = 0; },
    []() -> double { return g_snr; },
    [](bool full) { g_calls.push_back(full ? "mem full" : "mem"); },
    [] { g_calls.push_back("log load"); },
    [](bool on) { g_calls.push_back(on ? "log on" : "log off"); },
    []() -> bool { g_calls.push_back("log flush"); return g_ok; },
    [](serial_ft8_control::LogStatus* s) { *s = g_log; },
    [](unsigned k, unsigned gate, unsigned fine, unsigned long deadline) -> bool {
      g_calls.push_back("config " + std::to_string(k) + " " + std::to_string(gate) + " " + std::to_string(fine) + " " + std::to_string(deadline));
      return k >= 1 && k <= 64 && gate >= 1 && gate <= 64 && (fine == 4 || fine == 8);
    },
};

const serial_ft8::Hooks kHooks{kIo, &kQuery, &kData, &kWwv, &kControl};

std::string run(const char* args, bool signed_in) {
  reset();
  serial_ft8::handle(args, signed_in, kHooks);
  return all();
}

std::string b64(const uint8_t* bytes, size_t n) {
  static const char k[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  for (size_t i = 0; i < n; i += 3) {
    const uint32_t v = (bytes[i] << 16) | (i + 1 < n ? bytes[i + 1] << 8 : 0) | (i + 2 < n ? bytes[i + 2] : 0);
    out += k[(v >> 18) & 63]; out += k[(v >> 12) & 63];
    out += i + 1 < n ? k[(v >> 6) & 63] : '='; out += i + 2 < n ? k[v & 63] : '=';
  }
  return out;
}
}  // namespace

int main() {
  // ---- dispatcher ----
  const std::string help = run("", false);
  CHECK(g_out.size() == 4 && g_out[1] == "\n" && g_out[3] == "\n");   // two write_line calls
  CHECK(has(help, "ORC_FT8_HELP queries: STATUS | DECODES [n] | BANDS | DUMP | TIME | CLOCK | HUNTSTATUS | HELP\n"));
  CHECK(has(help, "ORC_FT8_HELP control (authenticated): OPEN | BAND <index|label|dial_hz> | MODE <FT8|FT4> | CLEAR | RUN <0|1> | CONFIG <k> <gate>"));
  CHECK(run("   help", false) == help && run(" HeLp extra", true) == help);
  CHECK(run("  bogus", false) == "ORC_FT8_ERROR BOGUS auth_required\n");   // signed out: the sign-in refusal wins over unknown
  CHECK(g_out.size() == 1);
  CHECK(run(" bogus 1", true) == "ORC_FT8_ERROR unknown_command BOGUS (FT8 HELP)\n" && g_out.size() == 1);
  CHECK(run(" aaaaaaaaaaaaaaaaaaaa", true) == "ORC_FT8_ERROR unknown_command AAAAAAAAAAAAAAA (FT8 HELP)\n");   // 15 characters kept
  CHECK(run("status", false) == run("  STATUS  ", false));            // the verb is case-insensitive and padded
  CHECK(run("FT8STATUS", true) == "ORC_FT8_ERROR unknown_command FT8STATUS (FT8 HELP)\n");
  CHECK(run("OPEN", false) == "ORC_FT8_ERROR OPEN auth_required\n" && g_calls.empty());
  CHECK(run("open", true) == "ORC_FT8_OPEN_OK\n" && g_calls == std::vector<std::string>{"open"});
  CHECK(run("RUN 1", false) == "ORC_FT8_ERROR RUN auth_required\n");
  for (const char* verb : {"ADIF", "SAVE", "SHOT", "SNAP", "INJECT", "WWV 10", "SNR", "MEM", "LOG", "CONFIG", "BAND", "MODE", "CLEAR",
                           "TAB", "HUNT", "DIAL", "CLOCKTEST", "NTP"}) {
    const std::string refusal = run(verb, false);
    std::string name(verb);
    name = name.substr(0, name.find(' '));
    CHECK(refusal == "ORC_FT8_ERROR " + name + " auth_required\n" && g_calls.empty());
  }
  CHECK(run("DUMP", false).rfind("ORC_FT8_DUMP_BEGIN samples=7 rate=12000 slot_utc=1700000015\n", 0) == 0);   // no sign-in

  // ---- query ----
  CHECK(run("TIME", false) == "ORC_FT8_TIME utc_ms=1700000000123 valid=1\n" && g_out.size() == 1);
  CHECK(run("BANDS", false) ==
        "ORC_FT8_BAND index=0 label=160m dial_hz=1840000\nORC_FT8_BAND index=2 label=40m dial_hz=7074000\nORC_FT8_BANDS_END\n");
  CHECK(g_out.size() == 4 && g_out[2] == "ORC_FT8_BANDS_END" && g_out[3] == "\n");
  CHECK(run("DECODES", false) == "ORC_FT8_DECODE d0\nORC_FT8_DECODE d1\nORC_FT8_DECODE d2\nORC_FT8_DECODES_END shown=3 total=3\n");
  CHECK(g_calls == std::vector<std::string>{"drain"});
  CHECK(run("DECODES 2", false) == "ORC_FT8_DECODE d0\nORC_FT8_DECODE d1\nORC_FT8_DECODES_END shown=2 total=3\n");
  CHECK(has(run("DECODES 0", false), "shown=3 total=3"));          // 0 means the default 20
  CHECK(has(run("DECODES x", false), "shown=3 total=3"));          // a non-number is 0, so also the default
  g_decodes.assign(25, "x");
  CHECK(has(run("DECODES", false), "shown=20 total=25") && has(run("DECODES 99", false), "shown=25 total=25"));
  g_decodes = {"d0", "d1", "d2"};
  CHECK(run("HUNTSTATUS", false) ==
        "ORC_FT8_HUNTSTATUS mode=FAST phase=scan current=-1 best=3 slots_per_band=4\n"
        "ORC_FT8_HUNTBAND index=2 label=20m visited=1 evidence=strong slots=4 decodes=2 strong=1 peak_dbfs=-61.2\n"
        "ORC_FT8_HUNTSTATUS_END\n");
  CHECK(run("CLOCK", false) ==
        "ORC_FT8_CLOCK level=2 age_s=90 ntp_utc=1700000000 signal_utc=1700000001 wide=1 last_slot_wide=0 dt_valid=1 dt_err_ms=-35 dt_stations=7 hold=1\n");

  g_status = {};
  g_status.runtime = "ready"; g_status.mode = "FT8"; g_status.band = "20m"; g_status.dial_hz = 14074000; g_status.clock = true;
  g_status.rx_running = true; g_status.tap = true; g_status.rate_hz = 12000; g_status.blocks = 12345; g_status.ring = 99;
  g_status.iq_dbfs = -31.46; g_status.iq_clip_pct = 0.0014; g_status.gain_tenth_db = 250; g_status.store = 6; g_status.k = 32;
  g_status.gate = 16; g_status.fine_rows = 8; g_status.deadline_ms = 11000; g_status.headless = true; g_status.dial_offset_hz = -3;
  const std::string status = run("STATUS", false);
  CHECK(g_out.size() == 1 && g_calls == std::vector<std::string>{"status"});
  CHECK(status.rfind("ORC_FT8_STATUS screen=0 runtime=ready headless=1 mode=FT8 band=20m dial_hz=14074000 clock=1 rx_running=1 tap=1 rate_hz=12000 "
                     "blocks=12345 ring=99 avg_us=0 max_us=0 slots=0 skipped=0 last_decodes=0 last_ms=0 spectral_ms=0 refine_ms=0 gate_ms=0 coarse=0 "
                     "deadline_hit=0 slot_rms=0 slot_peak=0 slot_clipped=0 dial_offset_hz=-3 iq_dbfs=-31.5 iq_clip_pct=0.001 gain_tenth_db=250 store=6 "
                     "k=32 gate=16 fine_rows=8 deadline_ms=11000\n", 0) == 0);
  {  // the longest possible STATUS line must fit the reply buffer (the console's printf buffer was 2,048 bytes)
    serial_ft8_query::Status big;
    big.runtime = "no_memory"; big.mode = "JS8"; big.band = "160m"; big.dial_hz = 4294967295ul; big.rate_hz = 4294967295ul;
    big.blocks = big.ring = 18446744073709551615ull;
    big.avg_us = big.max_us = big.slots = big.skipped = big.last_decodes = big.last_ms = big.spectral_ms = big.refine_ms = big.gate_ms = 4294967295ul;
    big.coarse = 65535; big.slot_rms = big.slot_peak = big.slot_clipped = 4294967295ul; big.dial_offset_hz = -2147483647 - 1;
    big.iq_dbfs = -123456.7; big.iq_clip_pct = 100.0; big.gain_tenth_db = -2147483647 - 1; big.store = 4294967295u; big.k = big.gate = big.fine_rows = 4294967295u;
    big.deadline_ms = 4294967295ul;
    g_status = big;
    const std::string line = run("STATUS", false);
    std::printf("serial_ft8: longest STATUS line %zu bytes (buffer %zu)\n", line.size(), serial_ft8::kSayWideMax);
    CHECK(line.size() < serial_ft8::kSayWideMax && line.back() == '\n' && line.size() > 600);
  }

  // ---- data ----
  {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(g_samples);
    const std::string dump = run("DUMP", false);
    CHECK(dump == "ORC_FT8_DUMP_BEGIN samples=7 rate=12000 slot_utc=1700000015\nORC_FT8_DUMP_DATA 0 " + b64(bytes, 14) + "\nORC_FT8_DUMP_END\n");
    CHECK(g_calls == std::vector<std::string>{"release"});
    CHECK(g_out.size() == 4 && g_out[1] == "ORC_FT8_DUMP_DATA 0 " + b64(bytes, 14) + "\n" && g_out[2] == "ORC_FT8_DUMP_END" &&
          g_out[3] == "\n");   // the data line is one write, the end marker a write_line
    g_slot = false;
    CHECK(run("DUMP", false) == "ORC_FT8_ERROR DUMP no_slot_yet\n" && g_calls.empty());
    g_slot = true;
  }
  {  // a longer slot: 72 bytes per line, a yield after every 32nd line, the last line short
    static int16_t big[2000];
    for (int i = 0; i < 2000; ++i) big[i] = static_cast<int16_t>(i * 37);
    const serial_ft8_data::Hooks long_data{
        kIo,
        [](const int16_t** s, size_t* c, uint64_t* ms) -> bool { *s = big; *c = 2000; *ms = 15000; return true; },
        [] { g_calls.push_back("release"); }, [] { g_calls.push_back("yield"); }, [] {}, []() -> bool { return true; },
        [](const char*, unsigned*) -> bool { return true; }, [](const char*, const int16_t*, size_t) -> bool { return true; },
        [](const char*, const char**, unsigned*) -> bool { return true; }, [] {}, [](size_t) -> bool { return true; },
        []() -> size_t { return 0; }, []() -> unsigned long { return 0; }, [](serial_ft8_data::InjectMode) -> bool { return true; },
        [](unsigned long, const int16_t*, size_t) -> bool { return true; },
    };
    reset();
    CHECK(serial_ft8_data::handle("DUMP", "", false, long_data));
    // 4000 bytes: 55 full lines of 72 bytes and one of 40
    CHECK(g_out.size() == 1 + 56 + 2);
    size_t yields = 0;
    for (const auto& c : g_calls) yields += c == "yield";
    CHECK(yields == 1 && g_calls.back() == "release");
    CHECK(has(g_out[56], "ORC_FT8_DUMP_DATA 55 ") && g_out[57] == "ORC_FT8_DUMP_END");
    CHECK(g_out[1].size() == std::strlen("ORC_FT8_DUMP_DATA 0 ") + 96 + 1);
  }
  CHECK(run("ADIF", true) == "ORC_FT8_ADIF_OK path=/sd/orcsdr/ft8/heard.adi records=5\n");
  CHECK(g_calls == (std::vector<std::string>{"drain", "adif /orcsdr/ft8/heard.adi"}));
  CHECK(has(run("ADIF my log!/../x", true), "path=/sd/orcsdr/ft8/mylogx.adi"));   // only letters, digits, _ and -
  CHECK(has(run("ADIF 123456789012345678901234567890123456789012345", true), "/orcsdr/ft8/123456789012345678901234567890123456789.adi"));   // 39 kept
  g_sd = false;
  CHECK(run("ADIF", true) == "ORC_FT8_ERROR ADIF no_sd_card\n" && g_calls == std::vector<std::string>{"drain"});
  g_sd = true; g_adif_ok = false;
  CHECK(run("ADIF a", true) == "ORC_FT8_ERROR ADIF open_failed path=/orcsdr/ft8/a.adi\n");
  g_adif_ok = true;

  CHECK(run("SAVE", true) == "ORC_FT8_SAVE_OK path=/sd/orcsdr/ft8/slot_1700000015.wav samples=7 slot_utc=1700000015\n");
  CHECK(g_calls == (std::vector<std::string>{"wav /orcsdr/ft8/slot_1700000015.wav 7", "release"}));
  CHECK(has(run("SAVE take-1", true), "path=/sd/orcsdr/ft8/take-1.wav"));
  g_slot = false;
  CHECK(run("SAVE", true) == "ORC_FT8_ERROR SAVE no_slot_yet\n" && g_calls.empty());
  g_slot = true; g_sd = false;
  CHECK(run("SAVE", true) == "ORC_FT8_ERROR SAVE no_sd_card\n" && g_calls.empty());   // the slot is NOT released here (as before)
  g_sd = true; g_wav_ok = false;
  CHECK(run("SAVE", true) == "ORC_FT8_ERROR SAVE open_failed path=/orcsdr/ft8/slot_1700000015.wav\n" &&
        g_calls == std::vector<std::string>{"wav /orcsdr/ft8/slot_1700000015.wav 7"});   // nor here
  g_wav_ok = true;

  CHECK(run("SHOT", true) == "ORC_FT8_SHOT_OK path=/orcsdr/screenshots/ft8_screen.bmp bytes=921654\n" && g_calls == std::vector<std::string>{"shot ft8_screen"});
  CHECK(has(run("SHOT a b", true), "screenshots/ab.bmp"));
  g_shot_ok = false;
  CHECK(run("SHOT", true) == "ORC_FT8_ERROR SHOT capture_failed\n");
  g_shot_ok = true; g_sd = false;
  CHECK(run("SHOT", true) == "ORC_FT8_ERROR SHOT no_sd_card\n" && g_calls.empty());
  g_sd = true;
  CHECK(run("SNAP", true).empty() && g_calls == std::vector<std::string>{"png"});

  CHECK(run("INJECT BEGIN 180000", true) == "ORC_FT8_INJECT_BEGIN_OK\n" && g_calls == std::vector<std::string>{"begin 180000"});
  CHECK(run("inject begin 5", true) == "ORC_FT8_INJECT_BEGIN_OK\n");
  CHECK(run("INJECT BEGIN", true) == "ORC_FT8_INJECT_BEGIN_OK\n" && g_calls == std::vector<std::string>{"begin 0"});   // "BEGIN" alone: one field read
  g_inject_ok = false;
  CHECK(run("INJECT BEGIN 1", true) == "ORC_FT8_INJECT_ERROR begin (start the decoder with RUN 1 first; at most 16 s)\n");
  CHECK(run("INJECT RUN", true) == "ORC_FT8_INJECT_ERROR run\n");
  g_inject_ok = true;
  CHECK(run("INJECT PING", true) == "ORC_FT8_INJECT_ACK written=4800 crc=deadbeef\n" && g_out.size() == 1);
  CHECK(has(run("INJECT pingxyz", true), "INJECT_ACK"));
  CHECK(run("INJECT RUN", true) == "ORC_FT8_INJECT_RUN_OK\n" && g_calls == std::vector<std::string>{"run ft8"});
  CHECK(run("INJECT RUN FT4", true) == "ORC_FT8_INJECT_RUN_OK\n" && g_calls == std::vector<std::string>{"run ft4"});
  CHECK(run("INJECT RUN   js8", true) == "ORC_FT8_INJECT_RUN_OK\n" && g_calls == std::vector<std::string>{"run js8"});
  CHECK(run("INJECT RUN FT4X", true) == "ORC_FT8_INJECT_RUN_OK\n" && g_calls == std::vector<std::string>{"run ft8"});
  CHECK(run("INJECT", true) == "ORC_FT8_INJECT_ERROR usage\n" && g_calls.empty());
  CHECK(run("INJECT 5", true) == "ORC_FT8_INJECT_ERROR usage\n");   // no space after the offset
  CHECK(run("INJECT x y", true) == "ORC_FT8_INJECT_ERROR usage\n");
  {
    const int16_t pcm[4] = {0x0102, -2, 0x7fff, -32768};
    const std::string line = "INJECT 96 " + b64(reinterpret_cast<const uint8_t*>(pcm), 8);
    CHECK(run(line.c_str(), true).empty() && g_calls == std::vector<std::string>{"write 96 4 258"});
    g_inject_write_ok = false;
    CHECK(run(line.c_str(), true) == "ORC_FT8_INJECT_ERROR write\n");
    g_inject_write_ok = true;
    CHECK(run("INJECT 0 ====", true).empty() && g_calls == std::vector<std::string>{"write 0 0 0"});   // an empty payload is written as 0 samples
    // At most 512 bytes (256 samples) are taken from a longer line.
    const std::string big(1000, 'A');
    CHECK(run(("INJECT 7 " + big).c_str(), true).empty() && g_calls == std::vector<std::string>{"write 7 256 0"});
  }
  {  // decode_base64: stops at '=', skips other characters, capacity-limited
    uint8_t out[8];
    CHECK(serial_ft8_data::decode_base64("TWFu", out, 8) == 3 && std::memcmp(out, "Man", 3) == 0);
    CHECK(serial_ft8_data::decode_base64("TW Fu\n", out, 8) == 3);
    CHECK(serial_ft8_data::decode_base64("TWE=", out, 8) == 2 && std::memcmp(out, "Ma", 2) == 0);
    CHECK(serial_ft8_data::decode_base64("TWFuTWFu", out, 4) == 4);
    CHECK(serial_ft8_data::decode_base64("", out, 8) == 0 && serial_ft8_data::decode_base64("=AAA", out, 8) == 0);
    CHECK(serial_ft8_data::decode_base64("-_*#", out, 8) == 0);
  }

  // ---- wwv ----
  g_wwv_state = {};
  CHECK(run("WWV", false) == "ORC_FT8_WWV_STATUS phase=0 seconds=0 action=0 mhz=0.0\n");
  g_wwv_state = {1, 90, serial_ft8_wwv::Action::calibrate, 10.0};
  CHECK(run("WWV", true) == "ORC_FT8_WWV_STATUS phase=1 seconds=90 action=2 mhz=10.0\n");
  CHECK(run("WWV stop", false) == "ORC_FT8_ERROR WWV auth_required\n" && g_calls.empty());
  CHECK(run("WWV stop", true) == "ORC_FT8_WWV_STOP requested\n" && g_calls == std::vector<std::string>{"wwv stop"});
  g_wwv_state = {};
  const char* kInvalid = "ORC_FT8_ERROR WWV invalid use WWV <2.5|5|10|15|20> [20-240 seconds] [CAL|SYNC] | WWV STOP | WWV\n";
  for (const char* bad : {"WWV 3", "WWV 10.04", "WWV 10 19", "WWV 10 241", "WWV 10 60 NOPE", "WWV abc", "WWV 10 60 SYNCX"})
    CHECK(run(bad, true) == kInvalid && g_calls.empty());
  g_decoder_active = false;
  CHECK(run("WWV 10", true) == "ORC_FT8_ERROR WWV the decoder is not running (FT8 RUN 1 or open the FT8 screen)\n");
  g_decoder_active = true;
  g_wwv_state = {1, 0, serial_ft8_wwv::Action::none, 0};
  CHECK(run("WWV 10", true) == "ORC_FT8_ERROR WWV a measurement is already running (FT8 WWV STOP)\n");
  g_wwv_state = {0, 0, serial_ft8_wwv::Action::sync, 0};
  CHECK(run("WWV 10", true) == "ORC_FT8_ERROR WWV a measurement is already running (FT8 WWV STOP)\n");
  g_wwv_state = {};
  CHECK(run("WWV 10", true) == "ORC_FT8_WWV_START mhz=10.0 dial_hz=9999000 seconds=90 action=1 (about 102 s with the retune; the band comes back by itself)\n");
  CHECK(g_calls == std::vector<std::string>{"wwv start 10.0 9999000 90 1"});
  CHECK(has(run("wwv 2.5 120 cal", true), "dial_hz=2499000 seconds=120 action=2 (about 132 s") && g_calls == std::vector<std::string>{"wwv start 2.50 2499000 120 2"});
  CHECK(has(run("WWV 20 240 SYNC", true), "dial_hz=19999000 seconds=240 action=3 (about 252 s"));
  CHECK(has(run("WWV 15 20", true), "seconds=20 action=1"));
  g_start_ok = false;
  CHECK(run("WWV 10", true) == "ORC_FT8_ERROR WWV could not start (tuning refused or no memory)\n");
  g_start_ok = true;

  // ---- control ----
  CHECK(run("BAND", true) == "ORC_FT8_ERROR BAND invalid use BAND <index|label|dial_hz> (see FT8 BANDS)\n");
  CHECK(run("BAND 40m", true) == "ORC_FT8_BAND_OK index=3 label=40m dial_hz=7074000\n" && g_calls == std::vector<std::string>{"select 3"});
  g_ok = false;
  CHECK(run("BAND 40m", true) == "ORC_FT8_BAND_FAILED index=3 label=40m dial_hz=7074000\n");
  g_ok = true;
  CHECK(run("MODE FT4", true) == "ORC_FT8_MODE_OK mode=FT4 dial_hz=7047500\n" && g_calls == std::vector<std::string>{"mode ft4"});
  CHECK(run("mode ft8", true) == "ORC_FT8_MODE_OK mode=FT8 dial_hz=7074000\n");
  CHECK(run("MODE JS8", true) == "ORC_FT8_ERROR MODE invalid use MODE <FT8|FT4> (JS8 is disabled)\n" && g_calls.empty());
  CHECK(run("MODE", true) == "ORC_FT8_ERROR MODE invalid use MODE <FT8|FT4> (JS8 is disabled)\n");
  CHECK(run("CLEAR", true) == "ORC_FT8_CLEAR_OK\n" && g_calls == std::vector<std::string>{"clear"});
  CHECK(run("RUN 1", true) == "ORC_FT8_RUN_OK headless=1\n" && g_calls == std::vector<std::string>{"run start"});
  CHECK(run("RUN 0", true) == "ORC_FT8_RUN_OK headless=0\n" && g_calls == std::vector<std::string>{"run stop"});
  CHECK(run("RUN 1x", true) == "ORC_FT8_RUN_OK headless=1\n");          // only the first character counts
  CHECK(run("RUN", true) == "ORC_FT8_ERROR RUN invalid use RUN <0|1>\n" && g_calls.empty());
  CHECK(run("RUN 2", true) == "ORC_FT8_ERROR RUN invalid use RUN <0|1>\n");
  g_start_ok = false;
  CHECK(run("RUN 1", true) == "ORC_FT8_ERROR RUN start_failed\n");
  g_start_ok = true;
  CHECK(run("TAB map", true) == "ORC_FT8_TAB_OK tab=MAP\n" && g_calls == std::vector<std::string>{"tab 2"});
  CHECK(run("TAB Setup", true) == "ORC_FT8_TAB_OK tab=SETUP\n" && g_calls == std::vector<std::string>{"tab 5"});
  CHECK(run("TAB", true) == "ORC_FT8_ERROR TAB invalid use TAB <LIVE|DECODES|MAP|HUNTER|HEARD|SETUP>\n" && g_calls.empty());
  CHECK(run("HUNT FAST", true) == "ORC_FT8_HUNT_OK\n" && g_calls == std::vector<std::string>{"hunt fast"});
  CHECK(run("hunt decode", true) == "ORC_FT8_HUNT_OK\n" && g_calls == std::vector<std::string>{"hunt decode"});
  g_ok = false;
  CHECK(run("HUNT FAST", true) == "ORC_FT8_HUNT_FAILED\n");
  g_ok = true;
  CHECK(run("HUNT STOP", true) == "ORC_FT8_HUNT_OK stopped\n" && g_calls == std::vector<std::string>{"hunt stop"});
  CHECK(run("HUNT", true) == "ORC_FT8_ERROR HUNT invalid use HUNT <FAST|DECODE|STOP>\n");
  CHECK(run("DIAL 14074000", true).empty() && g_calls == std::vector<std::string>{"dial 14074000"});   // success prints nothing here
  const char* kDial = "ORC_FT8_ERROR DIAL invalid use DIAL <hz> (0.5 MHz to the tuner limit)\n";
  CHECK(run("DIAL", true) == kDial && run("DIAL 0", true) == kDial && run("DIAL 14074000oops", true) == kDial && g_calls.empty());
  CHECK(run("DIAL 7074000  ", true).empty() && g_calls == std::vector<std::string>{"dial 7074000"});   // trailing spaces are fine
  g_ok = false;
  CHECK(run("DIAL 7074000", true) == kDial);
  g_ok = true;
  CHECK(run("CLOCKTEST 250", true) == "ORC_FT8_CLOCKTEST OK step_ms=250\n" && g_calls == std::vector<std::string>{"clocktest 250"});
  CHECK(run("CLOCKTEST -5000", true) == "ORC_FT8_CLOCKTEST OK step_ms=-5000\n");
  CHECK(run("CLOCKTEST 0", true) == "ORC_FT8_CLOCKTEST released\n" && g_calls == std::vector<std::string>{"clocktest release"});
  CHECK(run("CLOCKTEST", true) == "ORC_FT8_CLOCKTEST released\n");   // a missing number reads as 0
  CHECK(run("CLOCKTEST 5001", true) == "ORC_FT8_ERROR CLOCKTEST invalid use CLOCKTEST <-5000..5000 ms>\n" && g_calls.empty());
  CHECK(run("CLOCKTEST -5001", true) == "ORC_FT8_ERROR CLOCKTEST invalid use CLOCKTEST <-5000..5000 ms>\n");
  g_ok = false;
  CHECK(run("CLOCKTEST 10", true) == "ORC_FT8_CLOCKTEST FAILED step_ms=10\n");
  CHECK(run("NTP", true) == "ORC_FT8_NTP_FAILED (no Wi-Fi or already running)\n");
  g_ok = true;
  CHECK(run("NTP", true) == "ORC_FT8_NTP_OK started (see ORC_NTP_OK; then FT8 TIME)\n");
  g_snr = 0.0;
  CHECK(run("SNR", true) == "ORC_FT8_SNR user_offset_db=0.0\n" && g_calls.empty());
  CHECK(run("SNR OFFSET 1.5", true) == "ORC_FT8_SNR user_offset_db=1.5\n" && g_calls == std::vector<std::string>{"snr set 1.50"});
  CHECK(run("SNR offset -2", true) == "ORC_FT8_SNR user_offset_db=-2.0\n");
  CHECK(run("SNR OFFSET", true) == "ORC_FT8_ERROR SNR usage: SNR OFFSET <dB>\n" && g_calls.empty());
  CHECK(run("SNR OFFSET x", true) == "ORC_FT8_ERROR SNR usage: SNR OFFSET <dB>\n");
  CHECK(run("SNR RESET", true) == "ORC_FT8_SNR user_offset_db=0.0\n" && g_calls == std::vector<std::string>{"snr reset"});
  CHECK(run("SNR bogus", true) == "ORC_FT8_SNR user_offset_db=0.0\n" && g_calls.empty());
  CHECK(run("MEM", true).empty() && g_calls == std::vector<std::string>{"mem"});
  CHECK(run("MEM FULL", true).empty() && g_calls == std::vector<std::string>{"mem full"});
  CHECK(run("MEM full!", true).empty() && g_calls == std::vector<std::string>{"mem full"});
  const char* kLog = "ORC_FT8_LOG enabled=1 written=12 staged=3 dropped=1 errors=2 dir=/orcsdr/ft8 dma_largest=18432 need=4096\n";
  CHECK(run("LOG", true) == kLog && g_calls == std::vector<std::string>{"log load"});
  CHECK(run("LOG ON", true) == kLog && g_calls == (std::vector<std::string>{"log load", "log on"}));
  CHECK(run("LOG off", true) == kLog && g_calls == (std::vector<std::string>{"log load", "log off"}));
  CHECK(run("LOG FLUSH", true) == std::string("ORC_FT8_LOG_FLUSH ok\n") + kLog && g_calls == (std::vector<std::string>{"log load", "log flush"}));
  g_ok = false;
  CHECK(run("LOG FLUSH", true) == std::string("ORC_FT8_LOG_FLUSH pending (check SD errors and DMA memory)\n") + kLog);
  g_ok = true;
  CHECK(run("CONFIG 32 16 8 11000", true) == "ORC_FT8_CONFIG_OK k=32 gate=16 fine_rows=8 deadline_ms=11000\n" &&
        g_calls == std::vector<std::string>{"config 32 16 8 11000"});
  const char* kConfig = "ORC_FT8_ERROR CONFIG invalid use CONFIG <k 1-64> <gate 1-64> <fine_rows 4|8> <deadline_ms>\n";
  CHECK(run("CONFIG", true) == kConfig && g_calls.empty());          // sscanf failed: the device is not asked
  CHECK(run("CONFIG 1 2 3", true) == kConfig && g_calls.empty());
  CHECK(run("CONFIG 0 0 0 0", true) == kConfig && g_calls.size() == 1);   // parsed, then refused by the decoder
  CHECK(run("CONFIG 1 1 4 1 extra", true) == "ORC_FT8_CONFIG_OK k=1 gate=1 fine_rows=4 deadline_ms=1\n");

  // ---- io helpers ----
  {
    char slug[8];
    CHECK(serial_ft8::file_slug("a b/c-d_e.f", slug, sizeof(slug)) == 7 && std::strcmp(slug, "abc-d_e") == 0);   // cut at size - 1
    CHECK(serial_ft8::file_slug("../..", slug, sizeof(slug)) == 0 && slug[0] == '\0');
    CHECK(serial_ft8::file_slug("\xc3\xa9x", slug, sizeof(slug)) == 1 && std::strcmp(slug, "x") == 0);
  }

  if (failures == 0) std::printf("serial_ft8: all checks passed\n");
  return failures == 0 ? 0 : 1;
}
