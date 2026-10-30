// Host tests for the ACARS interpreter. The texts are real blocks captured from the Tab5 on 131.550 MHz (2026-10-28), verbatim.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "acars_interpret.hpp"

using namespace orcsdr::acars_ui;

namespace {

[[noreturn]] void fail(const char* expression, int line) {
  std::fprintf(stderr, "FAIL line=%d check=%s\n", line, expression);
  std::exit(1);
}
#define CHECK(expression) \
  do { if (!(expression)) fail(#expression, __LINE__); } while (false)

MessageRecord make(const char* label, char block_id, char ack, const char* text) {
  MessageRecord m;
  m.seq = 1;
  m.mode = '2';
  m.block_id = block_id;
  m.acknowledgement = ack;
  std::snprintf(m.label, sizeof(m.label), "%s", label);
  m.has_text = text[0] != '\0';
  m.text_length = static_cast<uint16_t>(std::strlen(text));
  std::memcpy(m.text, text, m.text_length);
  return m;
}

bool any_line_contains(const Interpretation& r, const char* needle) {
  for (size_t i = 0; i < r.line_count; ++i)
    if (std::strstr(r.lines[i], needle)) return true;
  return false;
}
void dump(const Interpretation& r) {
  std::printf("  [%s] %s\n", r.headline, r.summary);
  for (size_t i = 0; i < r.line_count; ++i) std::printf("    %s\n", r.lines[i]);
}

void squitter() {
  Interpretation r;
  const MessageRecord m = make("SQ", '?', '?', "02XAEUGKEUG14407N12313WV136975/ARINC");
  CHECK(interpret(m, r));
  dump(r);
  CHECK(std::strcmp(r.headline, "GROUND STATION SQUITTER") == 0);
  CHECK(any_line_contains(r, "EUG (KEUG) number 1, ARINC"));
  CHECK(any_line_contains(r, "44.117N 123.217W"));   // 44 deg 07 min, 123 deg 13 min
  CHECK(any_line_contains(r, "VDL2 136.975"));
  CHECK(any_line_contains(r, "ARINC"));
  CHECK(std::strstr(r.summary, "EUG") != nullptr);
  // The ARINC 620 examples: station list and a second service.
  const MessageRecord e = make("SQ", '?', '?', "02XACIDKCID14153N09143WV136975,52ABCDE,52ABCDF-V136925/FreeText");
  CHECK(interpret(e, r));
  CHECK(any_line_contains(r, "VDL2 136.975, VDL2 136.925"));
  CHECK(any_line_contains(r, "Note: FreeText"));
  const MessageRecord v1 = make("SQ", '?', '?', "01XSCIDKCID2/x");
  CHECK(interpret(v1, r));
  CHECK(any_line_contains(r, "CID (KCID) number 2, SITA"));
  CHECK(!interpret(make("SQ", '?', '?', "zz"), r) || r.recognised);   // garbage never crashes
}

void general_response() {
  Interpretation r;
  // Uplink block "W" confirming downlink block 2; the label is "_<DEL>", stored as "_?".
  MessageRecord m = make("_?", 'W', '2', "");
  CHECK(interpret(m, r));
  dump(r);
  CHECK(std::strcmp(r.headline, "GROUND ACKNOWLEDGEMENT") == 0);
  CHECK(any_line_contains(r, "downlink block 2"));
  m = make("_?", 'B', '3', "S83ANW2766");   // ack carrying a message number and flight
  m.acknowledgement = '4';
  CHECK(interpret(m, r));
  CHECK(any_line_contains(r, "Message S83A   Flight NW2766"));
  CHECK(std::strstr(r.summary, "ACK of block 4") != nullptr);
  m = make("_?", '4', '?', "");   // an NAK shows as '?'
  CHECK(interpret(m, r));
  CHECK(std::strcmp(r.headline, "AIRCRAFT ACKNOWLEDGEMENT") == 0);
  CHECK(any_line_contains(r, "no block named"));
}

void autotune_and_tests() {
  Interpretation r;
  CHECK(interpret(make(":;", 'X', '3', "130025"), r));
  dump(r);
  CHECK(any_line_contains(r, "130.025 MHz"));
  CHECK(interpret(make(":;", 'X', '3', "1300250120"), r));
  CHECK(any_line_contains(r, "For 120 seconds"));
  CHECK(interpret(make(":;", 'X', '3', "13"), r));   // too short to parse: the label is still named, nothing is invented
  CHECK(any_line_contains(r, "not decoded"));
  CHECK(!any_line_contains(r, "MHz"));
  CHECK(interpret(make("Q0", '4', '?', "S82ANW2766"), r));
  CHECK(std::strcmp(r.headline, "LINK TEST") == 0);
  CHECK(interpret(make("_d", '1', '?', ""), r));
}

void fmc_position() {
  Interpretation r;
  // Alaska 536 at 20:27:50Z. Degrees and minutes: 44 39.7 N = 44.662, 122 19.2 W = 122.320.
  MessageRecord m = make("H1", '2', '?',
      "F63AAS0536#M1BPOSN44397W122192,KNGDM,202750,400,WRUSL,203243,BTG,M57,268051,93,439,449K,237K,73493B");
  CHECK(interpret(m, r));
  dump(r);
  CHECK(std::strcmp(r.headline, "FMC POSITION REPORT") == 0);
  CHECK(any_line_contains(r, "Message F63 part A   Flight AS0536 (Alaska)"));
  CHECK(any_line_contains(r, "44.662N 122.320W"));
  CHECK(any_line_contains(r, "FL400"));
  CHECK(any_line_contains(r, "Over KNGDM at 20:27:50Z, next WRUSL ETA 20:32:43Z"));
  CHECK(any_line_contains(r, "-57C"));
  CHECK(any_line_contains(r, "wind 268/51 kt"));
  // Five digit wind and the older layout from another aircraft.
  m = make("H1", '6', '?', "F44ANW2766#M1BPOSN43511W121534,HYJOE,203344,389,JAGWA,212751,BURGL,M62,26253,1348D38");
  CHECK(interpret(m, r));
  CHECK(any_line_contains(r, "43.852N 121.890W"));
  CHECK(any_line_contains(r, "FL389"));
  CHECK(any_line_contains(r, "wind 262/53 kt"));
  CHECK(any_line_contains(r, "-62C"));
  // Invalid minutes are rejected rather than guessed.
  m = make("H1", '6', '?', "F44ANW2766#M1BPOSN43911W121534,HYJOE,203344,389");
  CHECK(interpret(m, r));
  CHECK(std::strcmp(r.headline, "MESSAGE FOR/FROM LEFT FMC") == 0);
}

void label21() {
  Interpretation r;
  MessageRecord m = make("21", '0', '?', "M05AF94668POSN 42.872W120.685, 102,202236,36975,26701,  50,-59,214330,KLAS");
  CHECK(interpret(m, r));
  dump(r);
  CHECK(std::strcmp(r.headline, "POSITION REPORT") == 0);
  CHECK(any_line_contains(r, "Flight F94668 (Frontier)"));
  CHECK(any_line_contains(r, "42.872N 120.685W"));
  CHECK(any_line_contains(r, "FL370"));
  CHECK(any_line_contains(r, "20:22:36Z"));
  CHECK(any_line_contains(r, "Outside air -59C"));
  CHECK(any_line_contains(r, "ETA 21:43:30Z at KLAS"));
}

void dfdau() {
  Interpretation r;
  // First block of a cruise report: route plus two complete fixes and a third cut off at the block end.
  MessageRecord m = make("H1", '0', '?',
      "D59AAS0536#DFB  00#  551#.281026.20085400000536.KOAK.KSEA.CR.3700.20237.8818-#  "
      "N42.3526.W122.0239.282008.#40000.-58.0.266.053.CR#  N42.5984.W122.0193.282010.#40004.-59.2.262.056.CR#  "
      "N42.8461.W122.0141.282012.#40000.-57");
  CHECK(interpret(m, r));
  dump(r);
  CHECK(std::strcmp(r.headline, "FLIGHT DATA UNIT POSITIONS") == 0);
  CHECK(any_line_contains(r, "Route KOAK to KSEA"));
  CHECK(any_line_contains(r, "2 position fixes; latest 42.598N 122.019W"));
  CHECK(any_line_contains(r, "Outside air -59C"));
  CHECK(any_line_contains(r, "wind 262/56 kt"));
  // A continuation block (no header) with three fixes.
  m = make("H1", '1', '?',
      "D59BAS0536#DFB.5.263.050.CR#  N43.0955.W122.0090.282014.#39999.-59.2.267.056.CR#  "
      "N43.3439.W122.0059.282016.#40005.-58.2.262.058.CR#  N43.5853.W122.0591.282018.#40001.-57.7.264.052.CR#");
  CHECK(interpret(m, r));
  CHECK(any_line_contains(r, "3 position fixes; latest 43.585N 122.059W"));
  // The decimal degrees agree with the FMC report 13 seconds before it, to within about 2 nm (see the cross-check note).
  m = make("H1", '3', '?', "D60AAS0536#DFB  TRP#202803#044.6863#-122.3252#400#127.4#451#0001767#0.01748#0050.00#0270.00#");
  CHECK(interpret(m, r));
  dump(r);
  CHECK(std::strcmp(r.headline, "FLIGHT DATA UNIT POSITION") == 0);
  CHECK(any_line_contains(r, "44.686N 122.325W"));
  CHECK(any_line_contains(r, "Level FL400"));
  CHECK(any_line_contains(r, "20:28:03Z"));
  // Report header with the route (Northwest-style layout, multi-block part A).
  m = make("H1", '9', '?',
      "D47ANW2766#DFBA319,037438,1,1,TB000000/REP019,01,01,4/CCN327NB,OCT28,203354,KPDX,KLAX,2766/C0TCF05005120004");
  CHECK(interpret(m, r));
  dump(r);
  CHECK(any_line_contains(r, "Route KPDX to KLAX"));
  CHECK(any_line_contains(r, "N327NB"));
  // Later blocks of such a report are recognised as flight data unit blocks but not decoded.
  m = make("H1", '2', '?', "D47DNW2766#DFB03,07680,00096,718,727,718,0/S41273,4495,004,013,202,35,38195,2/T41263,4515");
  CHECK(interpret(m, r));
  CHECK(any_line_contains(r, "Layout not decoded"));
}

void unknown_and_names() {
  Interpretation r;
  CHECK(!interpret(make("ZZ", '1', '?', "whatever"), r));   // unknown label, nothing invented
  CHECK(!r.recognised);
  CHECK(r.line_count == 0);
  // Known label without a parser still gets its meaning.
  CHECK(interpret(make("QA", '1', '?', "M12AUA1234OUT"), r));
  CHECK(std::strstr(r.headline, "OUT REPORT") != nullptr);
  CHECK(std::strcmp(label_name("_?"), "General response (acknowledgement)") == 0);
  CHECK(label_name("85") != nullptr);
  CHECK(label_name("ZZ") == nullptr);
  CHECK(std::strcmp(airline_name("WN1234"), "Southwest") == 0);
  CHECK(std::strcmp(airline_name("DL0456"), "Delta") == 0);
  CHECK(std::strcmp(airline_name("AA0100"), "American") == 0);
  CHECK(airline_name("NW2766") == nullptr);    // not guessed
  CHECK(airline_name("AS") == nullptr);
  // H1 with an unknown sub-label.
  CHECK(interpret(make("H1", '1', '?', "M01AUA1234#ZZBsome text"), r));
  CHECK(std::strstr(r.headline, "SUB-LABEL ZZ") != nullptr);
  // Hostile input: every length from 0 to the maximum, random bytes, no crash (the CI build adds ASan/UBSan).
  unsigned seed = 12345;
  for (int round = 0; round < 3000; ++round) {
    MessageRecord m;
    static const char* labels[] = {"H1", "SQ", "_?", ":;", "21", "Q0", "QA", "_d", "5Z"};
    std::snprintf(m.label, sizeof(m.label), "%s", labels[round % 9]);
    m.has_text = true;
    m.text_length = static_cast<uint16_t>((round * 7) % (sizeof(m.text) + 1));
    for (size_t i = 0; i < m.text_length; ++i) {
      seed = seed * 1103515245u + 12345u;
      const char* alphabet = "NSEW0123456789.,#/-ABCDEFGHIJKLMNOPQRSTUVWXYZ POSV";
      m.text[i] = (seed >> 16) % 7 == 0 ? static_cast<char>(seed >> 8) : alphabet[(seed >> 8) % std::strlen(alphabet)];
    }
    // Also splice real headers so the deeper parsers are reached.
    if (round % 3 == 0 && m.text_length > 20) std::memcpy(m.text, "D59AAS0536#DFB", 14);
    if (round % 5 == 0 && m.text_length > 20) std::memcpy(m.text, "F63AAS0536#M1BPOSN44397W122192", 30 > m.text_length ? m.text_length : 30);
    if (round % 7 == 0 && m.text_length > 30) std::memcpy(m.text, "C87AOO137C#CFBFDE0633591029ABZ 0631570730", m.text_length < 41 ? m.text_length : 41);
    if (round % 13 == 0 && m.text_length > 40) {
      std::memcpy(m.text, round % 2 ? "D15AWN0436#DFB05201,0926,B737-700,261029,WN0436,KEUG,KDEN," : "D15EWN0436#DFB00,0000  13.30.19,04172,217.2,.354,-3",
                  m.text_length < 40 ? m.text_length : 40);
    }
    if (round % 17 == 0 && m.text_length > 40) {
      std::memcpy(m.text, "M30AQX245227,B,29OCT26,133601,+44.219,-122.470,30006,", m.text_length < 50 ? m.text_length : 50);
      m.label[0] = '3';
      m.label[1] = '3';
    }
    if (round % 11 == 0 && m.text_length > 30) {
      std::memcpy(m.text, "M10AOO137CP,E,2026-10-29 06:23:43,4140,65924,N 44.0", m.text_length < 51 ? m.text_length : 51);
      m.label[0] = '3';
      m.label[1] = '2';
    }
    interpret(m, r);
    CHECK(r.line_count <= kInterpLines);
  }
}

// Live VDL2 captures from the Tab5 on 136.975 MHz (2026-10-29), verbatim.
void live_vdl2_formats() {
  Interpretation r;
  const MessageRecord pos = make("32", '9', '?',
      "M10AOO137CP,E,2026-10-29 06:23:43,4140,65924,N 44.045 W 122.171,14097,249,045,-13,333,368,585,0635,");
  CHECK(interpret(pos, r) && r.decoded);
  dump(r);
  CHECK(std::strcmp(r.headline, "POSITION REPORT") == 0);
  CHECK(any_line_contains(r, "Flight OO137C (SkyWest)"));
  CHECK(any_line_contains(r, "44.045N 122.171W"));
  CHECK(any_line_contains(r, "2026-10-29 06:23:43Z"));
  CHECK(any_line_contains(r, "Altitude 14097 ft (probable)"));
  CHECK(any_line_contains(r, "P E 4140 65924 249 045 -13 333 368 585 0635"));   // shown as received, not named
  CHECK(std::strstr(r.summary, "44.045N 122.171W 06:23:43Z") != nullptr);
  // The community sample (SkyWest, 2022) starts with "N" instead of "P".
  const MessageRecord pos_n = make("32", '9', '?',
      "M10AOO5484N,E,2022-06-18 15:30:15,4240,66554,N 29.798 W 095.666,9340,109,005,11,291,294,448,1546,");
  CHECK(interpret(pos_n, r) && r.decoded && any_line_contains(r, "29.798N 95.666W") && any_line_contains(r, "Altitude 9340 ft"));
  // A different position and a negative-free sample from the same descent.
  const MessageRecord pos2 = make("32", '4', '?',
      "M15AOO137CP,E,2026-10-29 06:25:43,4080,65864,N 44.102 W 122.399,11581,249,029,-9,274,297,468,0636,");
  CHECK(interpret(pos2, r) && any_line_contains(r, "44.102N 122.399W"));
  // Impossible dates, times or coordinates are not decoded.
  CHECK(!interpret(make("32", '9', '?', "M10AOO137CP,E,2026-13-29 06:23:43,4140,65924,N 44.045 W 122.171,1,2,3,"), r) || !r.decoded);
  CHECK(!interpret(make("32", '9', '?', "M10AOO137CP,E,2026-10-29 25:23:43,4140,65924,N 44.045 W 122.171,1,2,3,"), r) || !r.decoded);
  CHECK(!interpret(make("32", '9', '?', "M10AOO137CP,E,2026-10-29 06:23:43,4140,65924,N 94.045 W 122.171,1,2,3,"), r) || !r.decoded);

  const MessageRecord cf = make("H1", '1', '?',
      "C87AOO137C#CFBFDE0633591029ABZ 06315707301    0    0 06315707308    0    0 06320507310    0    0");
  CHECK(interpret(cf, r) && r.decoded);
  dump(r);
  CHECK(std::strcmp(r.headline, "FLIGHT DECK EFFECTS (FAULT DISPLAY)") == 0);
  CHECK(any_line_contains(r, "Flight OO137C (SkyWest)"));
  CHECK(any_line_contains(r, "Report 06:33:59Z 10-29, tag ABZ: 3 entries"));
  CHECK(any_line_contains(r, "06:31:57 07301  06:31:57 07308  06:32:05 07310"));
  const MessageRecord cf6 = make("H1", '7', '?',
      "C85AOO137C#CFBFDE0630151029ABZ 06281417294    0    0 06281517402    0    0 06283617310    0    0 06283617301    0    0 06283617308    0    0 06295107402    0    0");
  CHECK(interpret(cf6, r) && r.decoded);
  CHECK(any_line_contains(r, "6 entries"));
  CHECK(any_line_contains(r, "06:28:14 17294  06:28:15 17402  06:28:36 17310  06:28:36 17301  06:28:36 17308"));
  CHECK(any_line_contains(r, "06:29:51 07402"));
  // A damaged entry makes the event list undecoded; the unit is still named.
  const MessageRecord bad = make("H1", '1', '?', "C87AOO137C#CFBFDE0633591029ABZ 0631570730    0    0");
  CHECK(interpret(bad, r) && std::strncmp(r.headline, "MESSAGE FOR/FROM", 16) == 0);   // only the unit is named

  // Southwest #DFB report, captured on 136.975 MHz 2026-10-29 (WN0436, KEUG to KDEN): header block and two sample blocks.
  const MessageRecord sw_head = make("H1", '9', '?', "D15AWN0436#DFB05201,0926,B737-700,261029,WN0436,KEUG,KDEN,2341,");
  CHECK(interpret(sw_head, r) && r.decoded);
  dump(r);
  CHECK(any_line_contains(r, "B737-700  flight WN0436  KEUG to KDEN  2026-10-29"));
  CHECK(std::strstr(r.summary, "KEUG to KDEN") != nullptr);
  const MessageRecord sw_a = make("H1", '9', '?', "D15EWN0436#DFB00,00000000,00000000  13.30.19,04172,217.2,.354,-3.37,");
  CHECK(interpret(sw_a, r) && r.decoded);
  CHECK(std::strcmp(r.headline, "FLIGHT DATA UNIT SAMPLES") == 0);
  CHECK(any_line_contains(r, "latest 13:30:19Z  altitude 4172 ft  speed 217.2 kt  Mach 0.354"));
  const MessageRecord sw_b = make("H1", '9', '?', "D21JWN0436#DFB0000000,00000000,00000000  13.33.14,11886,289.0,.540,-3.37,");
  CHECK(interpret(sw_b, r) && any_line_contains(r, "altitude 11886 ft  speed 289.0 kt  Mach 0.540"));
  // Bad time or a damaged field: not a sample, and nothing is invented.
  CHECK(interpret(make("H1", '9', '?', "D15EWN0436#DFB00,00000000,00000000  13.61.19,04172,217.2,.354,-3.37,"), r) && std::strcmp(r.headline, "FLIGHT DATA UNIT SAMPLES") != 0);
  CHECK(interpret(make("H1", '9', '?', "D15EWN0436#DFB00,00000000,00000000  13.30.19,04172,217.2,354,-3.37,"), r) && std::strcmp(r.headline, "FLIGHT DATA UNIT SAMPLES") != 0);

  // Label 33 position report (Horizon, KMFR to KSEA); the last letter of the destination was cut by the screen and is completed here.
  const MessageRecord l33 = make("33", '9', '?', "M30AQX245227,B,29OCT26,133601,+44.219,-122.470,30006,7808,KMFR,KSEA,");
  CHECK(interpret(l33, r) && r.decoded);
  dump(r);
  CHECK(std::strcmp(r.headline, "POSITION REPORT") == 0);
  CHECK(any_line_contains(r, "44.219N 122.470W"));
  CHECK(any_line_contains(r, "2026-10-29 13:36:01Z"));
  CHECK(any_line_contains(r, "Altitude 30006 ft (probable)"));
  CHECK(any_line_contains(r, "Route KMFR to KSEA"));
  CHECK(any_line_contains(r, "27 B 7808"));
  CHECK(!interpret(make("33", '9', '?', "M30AQX245227,B,29XXX26,133601,+44.219,-122.470,30006,7808,KMFR,KSEA,"), r) || !r.decoded);
  CHECK(!interpret(make("33", '9', '?', "M30AQX245227,B,29OCT26,253601,+44.219,-122.470,30006,7808,KMFR,KSEA,"), r) || !r.decoded);
  CHECK(!interpret(make("33", '9', '?', "M30AQX245227,B,29OCT26,133601,+94.219,-122.470,30006,7808,KMFR,KSEA,"), r) || !r.decoded);

  // Southwest climb block (live, WN3763, B737-8; the longitude was cut by the screen and is completed here).
  const MessageRecord climb = make("H1", '9', '?', "D57BWN3763#DFB91513,32973,-51.8,241,076,CL,00000,0,  N4527.7,W12126.9,");
  CHECK(interpret(climb, r) && r.decoded);
  dump(r);
  CHECK(any_line_contains(r, "45.462N 121.448W"));
  CHECK(any_line_contains(r, "Altitude 32973 ft (probable)   -51C"));
  CHECK(any_line_contains(r, "Phase: climb"));
  CHECK(interpret(make("H1", '9', '?', "D57BWN3763#DFB91513,32973,-51.8,241,076,CL,00000,0,  N4567.7,W12126.9,"), r) && std::strcmp(r.headline, "FLIGHT DATA UNIT POSITION") != 0);
  // Header block with the "++" prefix and a MAX type (live, WN3763 KSEA to KSAN).
  CHECK(interpret(make("H1", '9', '?', "D57AWN3763#DFB++86501,N8971L,B7378MAX,261029,WN3763,KSEA,KSAN,"), r) && any_line_contains(r, "B7378MAX  flight WN3763  KSEA to KSAN"));
  // Label 17 uses the label 32 layout; its altitude field is not named.
  const MessageRecord l17 = make("17", '9', '?', "M14AOO4075P,E,2026-10-29 15:26:11,4140,65924,N 44.102 W 122.183,9340,109,005,11,291,294,448,1546,");
  CHECK(interpret(l17, r) && r.decoded && any_line_contains(r, "44.102N 122.183W"));
  CHECK(!any_line_contains(r, "probable"));

  // ARINC 622 envelope: four live blocks (checksum passes) and the specification's own example "AT1.N678DL" + 4104532300 -> A9B3.
  static const char* const arinc[][2] = {
      {"F06AWN0436#M1B/BA USADCXA.AT1.N926WN6082D826032E1F", "N926WN"},
      {"M14AGS0001#M1B/BA USADCXA.AT1.N142QS6187080700B267", "N142QS"},
      {"F17AUA1114#M1B/BA USADCXA.AT1.N775306209080600CB04", "N77530"},
      {"F27AAS1337#M1B/BA USADCXA.AT1.N982AK620907C900E55B", "N982AK"},
      {"F01AXX0001#M1B/AA OAKXGXA.AT1.N678DL4104532300A9B3", "N678DL"},
      {"M69AXA0001#M1B/BA USADCXA.AT1..N6509679F1A15001D15", "Aircraft N6509 "},
      {"F34ACX3280#M1B/BA USADCXA.AT1..B-LJK79191A9D005EBA", "Aircraft B-LJK "}};
  for (const auto& a : arinc) {
    CHECK(interpret(make("H1", '9', '?', a[0]), r) && r.decoded);
    CHECK(std::strcmp(r.headline, "ATC DATA LINK MESSAGE (ARINC 622)") == 0);
    CHECK(any_line_contains(r, a[1]));
    CHECK(any_line_contains(r, "checksum OK"));
  }
  interpret(make("H1", '9', '?', arinc[0][0]), r);
  dump(r);
  CHECK(any_line_contains(r, "Downlink   Ground address USADCXA   ATC data"));
  CHECK(any_line_contains(r, "Content as received: 6082D82603"));
  // A damaged checksum or data is not decoded as an ARINC 622 message.
  CHECK(interpret(make("H1", '9', '?', "F06AWN0436#M1B/BA USADCXA.AT1.N926WN6082D826032E1E"), r) && std::strcmp(r.headline, "ATC DATA LINK MESSAGE (ARINC 622)") != 0);
  CHECK(interpret(make("H1", '9', '?', "F06AWN0436#M1B/BA USADCXA.AT1.N926WN6082D826132E1F"), r) && std::strcmp(r.headline, "ATC DATA LINK MESSAGE (ARINC 622)") != 0);

  // D-ATIS request (label B9): six samples, all pass the checksum (the first is live from this unit, the rest from the same carrier).
  static const char* const atis[][3] = {
      {"L03AAS0608/KSEA.TI2/024KSEAA3BC3", "KSEA", "arrival ATIS"},
      {"M85AOO3736/KSJC.TI2/024KSJCC51D2", "KSJC", "automatic updates"},
      {"L01AXX0001/KRDU.TI2/024KRDUAB5F0", "KRDU", "arrival ATIS"},
      {"L01AXX0001/KLAX.TI2/024KLAXCB1E3", "KLAX", "automatic updates"},
      {"L01AXX0001/KSFO.TI2/040KSFODB6BD", "KSFO", "departure ATIS"}};
  for (const auto& a : atis) {
    CHECK(interpret(make("B9", '9', '?', a[0]), r) && r.decoded);
    CHECK(std::strcmp(r.headline, "AIRPORT WEATHER (ATIS) REQUEST") == 0);
    CHECK(any_line_contains(r, a[1]));
    CHECK(any_line_contains(r, a[2]));
  }
  CHECK(!interpret(make("B9", '9', '?', "L03AAS0608/KSEA.TI2/024KSEAA3BC4"), r) || std::strcmp(r.headline, "AIRPORT WEATHER (ATIS) REQUEST") != 0);
  CHECK(!interpret(make("B9", '9', '?', "L03AAS0608/KSEA.TI2/024KSEAX3BC3"), r) || std::strcmp(r.headline, "AIRPORT WEATHER (ATIS) REQUEST") != 0);

  // Variants found by running the interpreter over a whole day of real blocks (2026-10-29).
  // A seven-letter D-ATIS station.
  CHECK(interpret(make("B9", '9', '?', "M61AUA0677/ATSUAXA.TI2/024KSFOC61DE"), r) && std::strcmp(r.headline, "AIRPORT WEATHER (ATIS) REQUEST") == 0);
  CHECK(any_line_contains(r, "station ATSUAXA for KSFO"));
  // Label 32 without the spaces after the hemisphere letters.
  CHECK(interpret(make("32", '9', '?', "M32AOO6290P,R,2026-10-29 13:57:13,7920,67700,N44.230 W121.750,29592,242,"), r) && r.decoded &&
        any_line_contains(r, "44.230N 121.750W"));
  // Label 22 (position in degrees, minutes and seconds) and label 10 "LDR01".
  CHECK(interpret(make("22", '9', '?', "M50AF92407N 440914W1213321,-------,152006,37929, ,      , ,M 59,23777  62, 104,"), r) && r.decoded);
  CHECK(any_line_contains(r, "44.153N 121.555W") && any_line_contains(r, "15:20:06Z") && any_line_contains(r, "Altitude 37929 ft (probable)   Outside air -59C"));
  CHECK(!interpret(make("22", '9', '?', "M50AF92407N 446114W1213321,-------,152006,37929, ,      , ,M 59,23777  62, 104,"), r) || !r.decoded);
  CHECK(interpret(make("10", '9', '?', "M23AWN3972LDR01,189,D,SWA-2600-019,0,N 43.779,W122.100,40003, 11.2,KSMF,KSEA,KSEA,16R/,16L/,/,1,1,"), r) && r.decoded);
  CHECK(any_line_contains(r, "43.779N 122.100W") && any_line_contains(r, "Altitude 40003 ft (probable)") && any_line_contains(r, "KSMF and KSEA"));
  // Fault display entries separated by line breaks, as logged.
  CHECK(interpret(make("H1", '1', '?', "C87AOO137C#CFBFDE0633591029ABZ\n06315707301   0   0\n06315707308   0   0\n06320507310   0   0\n"), r) && r.decoded &&
        any_line_contains(r, "3 entries"));
  // A flight data unit block with no known layout is recognised but not "decoded": the number shown for coverage must stay honest.
  CHECK(interpret(make("H1", '9', '?', "D43FUA1220#DFB5.810.446    97    973511-0000\r\n1100  8183  8248"), r) && !r.decoded);
  CHECK(interpret(make("H1", '9', '?', "F13BAS0574#M1B5,180,P0,M12/FHHYJOE,010851,268K"), r) && !r.decoded);

  // Second pass over the day's blocks: T1 messages, in-range reports, flight data unit headers with a route and a position.
  CHECK(interpret(make("H1", '9', '?', "163AUA1241#T1BRTE 1 29OCT26 1209 N37257 UAL1241 KEUG/KDEN BCG59-U000-08"), r) && r.decoded);
  CHECK(any_line_contains(r, "Report RTE 1, 29OCT26 12:09Z") && any_line_contains(r, "N37257") && any_line_contains(r, "KEUG to KDEN"));
  CHECK(!interpret(make("H1", '9', '?', "163AUA1241#T1BRTE 1 29OCT26 2509 N37257 UAL1241 KEUG/KDEN"), r) || !r.decoded);
  CHECK(interpret(make("80", '9', '?', "M18AAS13583701 INRANG 1358/29 KSMF/KSEA .N253AK  /ETA 1430"), r) && r.decoded);
  CHECK(any_line_contains(r, "Day 29   Route KSMF to KSEA") && any_line_contains(r, "Aircraft N253AK   Expected arrival 14:30Z"));
  CHECK(interpret(make("28", '9', '?', "M28AQX24523702 INRANG 2452/29 KMFR/KSEA  N646QX /ETA 1414/EFD 063"), r) && r.decoded && any_line_contains(r, "Aircraft N646QX"));
  CHECK(!interpret(make("80", '9', '?', "M18AAS13583701 INRANG 1358/29 KSMF/KSEA .N253AK  /ETB 1430"), r) || !r.decoded);
  CHECK(interpret(make("H1", '9', '?', "D13AWN1899#DFB76401  02E29KEUGKSMF  N44079W12322012180254P005217011G000"), r) && r.decoded);
  CHECK(any_line_contains(r, "Route KEUG to KSMF") && any_line_contains(r, "Position 44.132N 123.367W"));
  CHECK(interpret(make("H1", '9', '?', "D15AUA0677#DFBCRZ01134950677KSEAKSFO1029260057061318  268790034977N2901"), r) && r.decoded && any_line_contains(r, "Route KSEA to KSFO"));
  CHECK(interpret(make("H1", '9', '?', "D65AAS0574#DFB\r\nTRP#011044#043.6299#-121.8231#350#141.1#\r\n455#0001923"), r) && r.decoded && any_line_contains(r, "43.629N 121.823W"));
  // Random eight letter runs that are not airports must not become a route.
  CHECK(interpret(make("H1", '9', '?', "D15AUA0677#DFBXYZABCDEFGHIJ 1234 5678"), r) && !r.decoded);

  // Review fixes: impossible coordinates and dates are not shown as decoded.
  CHECK(!interpret(make("H1", '9', '?', "F63AAS0536#M1BPOSN95397W122192,KNGDM,202750,350"), r) || !r.decoded);      // latitude 95
  CHECK(!interpret(make("H1", '9', '?', "F63AAS0536#M1BPOSN44397W190192,KNGDM,202750,350"), r) || !r.decoded);      // longitude 190
  CHECK(interpret(make("SQ", '?', '?', "02XAEUGKEUG14407N12313WV136975/ARINC"), r) && any_line_contains(r, "Located at"));
  CHECK(interpret(make("SQ", '?', '?', "02XAEUGKEUG14467N12313WV136975/ARINC"), r) && !any_line_contains(r, "Located at") && any_line_contains(r, "VDL2 136.975"));
  CHECK(interpret(make("SQ", '?', '?', "02XAEUGKEUG19107N12313WV136975/ARINC"), r) && !any_line_contains(r, "Located at"));
  CHECK(!interpret(make("32", '9', '?', "M10AOO137CP,E,2026-02-31 06:23:43,4140,65924,N 44.045 W 122.171,14097,249,045,-13,333,368,585,0635,"), r) || !r.decoded);
  CHECK(interpret(make("32", '9', '?', "M10AOO137CP,E,2024-02-29 06:23:43,4140,65924,N 44.045 W 122.171,14097,249,045,-13,333,368,585,0635,"), r) && r.decoded);   // leap day
  CHECK(!interpret(make("33", '9', '?', "M30AQX245227,B,31FEB26,133601,+44.219,-122.470,30006,7808,KMFR,KSEA,"), r) || !r.decoded);
  CHECK(interpret(make("Q0", '9', '?', "S74AUA2818"), r) && r.decoded);
  CHECK(interpret(make("Q0", '9', '?', "??"), r) && !r.decoded);

  // A VDL2 data frame that is not ACARS, kept as raw hex by the receiver.
  MessageRecord av = make("AV", '?', '\0', "2143658711234567008280AA550102");
  std::snprintf(av.tail, sizeof(av.tail), "%s", "1468D2A");
  CHECK(interpret(av, r) && r.recognised && !r.decoded);
  CHECK(std::strcmp(r.headline, "VDL2 DATA FRAME") == 0);
  CHECK(any_line_contains(r, "1468D2A"));
}

}  // namespace

int main() {
  squitter();
  general_response();
  autotune_and_tests();
  fmc_position();
  label21();
  dfdau();
  unknown_and_names();
  live_vdl2_formats();
  std::printf("acars_interpret_tests passed\n");
  return 0;
}
