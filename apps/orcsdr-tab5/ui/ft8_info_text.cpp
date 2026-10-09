#include "ft8_info_text.hpp"

namespace orcsdr::ft8::info {
namespace {

const char* const kLive[] = {
    "WATERFALL (left): time scrolls down the screen. Each bright streak",
    "is a signal. The numbers along the bottom are audio pitch in Hz",
    "(200 to 3000), not the radio frequency.",
    "",
    "SLOT DIAL (right): digital modes send in fixed time slots. The ring",
    "fills as the slot runs. When it ends, the decoder reads that slot.",
    "FT8 and JS8 Normal use 15 second slots. FT4 uses 7.5 seconds.",
    "",
    "DIAL: the radio frequency. Auto uses the standard one for the band.",
    "MODE: FT8, FT4 or JS8. The decoder reads one mode at a time.",
    "CLOCK must say LOCKED. Slot timing needs accurate UTC time.",
    "DECODER: LISTENING, DECODING or READY shows what it is doing now.",
    "GAIN: tap it to set the receiver gain: AUTO, or a manual step with a",
    "live input-level meter that warns when the input is overloaded.",
    "",
    "LATEST DECODES (bottom): the newest messages as they arrive.",
    "",
    "If Expert Tuning is on (Setup), the DIAL box says TAP TO TUNE.",
    "Tap it to type a frequency or step it up and down.",
};

const char* const kDecodes[] = {
    "Every message the decoder has read this session, newest first.",
    "",
    "UTC     the time of the slot the message arrived in.",
    "SNR     signal strength over noise. A dash means it is not",
    "        available (JS8 messages carry the sender's own number).",
    "DT      how early or late the signal was, in seconds.",
    "DF      the audio pitch of the signal in Hz.",
    "TYPE    CQ means someone is calling anyone. A ? is not classified.",
    "MESSAGE the text. GRID, DIST and BRG come from the sender's grid",
    "        locator: where they are, how far, and which way.",
    "NEW     a callsign heard for the first time (never before).",
    "BAR     the coloured bar at the left of a row: green = first time",
    "        heard, blue = heard on an earlier occasion, gold = worked.",
    "TAP     tap a row to see when that station was first and last heard,",
    "        how often, and on which bands and modes.",
    "",
    "DIST and BRG need your own location to be set.",
    "NEWER and OLDER page through the list. CLEAR empties this list on",
    "screen. Saved logs on the SD card are not touched.",
};

const char* const kMap[] = {
    "Stations are placed by their Maidenhead grid locator, for example",
    "EN82, which is a square on the map about 100 by 150 km.",
    "",
    "Only stations that send a grid can appear. FT8 and FT4 CQ calls",
    "usually include one.",
    "",
    "JS8 heartbeat replies do not carry a grid, so a JS8 map stays empty",
    "until frames that include a grid are decoded.",
    "",
    "Your own position comes from Settings. Without it there is no",
    "distance or direction.",
    "",
    "No Internet is used. The map is built into the device.",
};

const char* const kHunter[] = {
    "The Hunter finds which band has FT8 activity right now, so you do",
    "not have to try each band by hand.",
    "",
    "It visits the common bands one after another and listens.",
    "FAST HUNT listens for one slot per band (about 15 seconds each).",
    "DECODE HUNT listens for two and also tries to decode messages.",
    "",
    "Each band gets the strongest evidence it showed:",
    "  QUIET    nothing there",
    "  ENERGY   some signal power",
    "  FT8 SIG  something that looks like an FT8 signal",
    "  DECODED  at least one valid message",
    "",
    "The best band is the one with the strongest evidence, then the most",
    "decodes, then the best signal. When it is COMPLETE, LISTEN BEST",
    "tunes to it. STOP ends a hunt early.",
    "",
    "The Hunter uses FT8 today. FT4 and JS8 hunts are not available yet.",
};

const char* const kHeard[] = {
    "Each callsign decoded this session, shown once with its grid, last",
    "audio pitch and signal report. It is a who-is-on list, not a log of",
    "every message. Tap a station to see its history on this device.",
    "",
    "CONDITIONS (right) summarises what was decoded: the farthest",
    "station, the typical distance, and which directions the signals",
    "come from. It needs your location and stations that sent a grid.",
    "",
    "Everything here comes from this device's own decodes. The history",
    "is kept on the SD card. No Internet lookups are made.",
};

const char* const kSetupFt8[] = {
    "Choose the standard to receive: FT8, FT4 or JS8CALL. The decoder",
    "reads one at a time, and each uses its own frequencies.",
    "",
    "The rows below describe FT8: 15 second slots, 79 symbols, 8 tones",
    "6.25 Hz apart, about 50 Hz wide, with LDPC(174,91) and a 14-bit CRC",
    "to check every message. SNR is estimated from the signal.",
    "",
    "DECODER BINDING and UTC SLOT CLOCK show if the decoder is running",
    "and the clock is accurate enough for slot timing.",
    "",
    "EXPERT TUNING: leave it off and Auto uses the standard dial for each",
    "band. Check it to type a dial frequency or step it by 10 Hz to",
    "100 kHz (tap DIAL on Live). OrcDial is optional: with expert tuning",
    "on, tapping its centre switches the knob between bands and fine",
    "tuning.",
    "",
    "OrcSDR only receives. It never transmits.",
};

const char* const kSetupFt4[] = {
    "Choose the standard to receive: FT8, FT4 or JS8CALL. The decoder",
    "reads one at a time, and each uses its own frequencies.",
    "",
    "The rows below describe FT4: 7.5 second slots, 103 symbols, 4 tones",
    "20.83 Hz apart, about 83 Hz wide, with LDPC(174,91) and a 14-bit CRC",
    "to check every message. It is quicker than FT8 and a little less",
    "sensitive. SNR is estimated from the signal.",
    "",
    "DECODER BINDING and UTC SLOT CLOCK show if the decoder is running",
    "and the clock is accurate enough for slot timing.",
    "",
    "EXPERT TUNING: leave it off and Auto uses the standard dial for each",
    "band. Check it to type a dial frequency or step it by 10 Hz to",
    "100 kHz (tap DIAL on Live). OrcDial is optional: with expert tuning",
    "on, tapping its centre switches the knob between bands and fine",
    "tuning.",
    "",
    "OrcSDR only receives. It never transmits.",
};

const char* const kSetupJs8[] = {
    "Choose the standard to receive: FT8, FT4 or JS8CALL. The decoder",
    "reads one at a time, and each uses its own frequencies.",
    "",
    "JS8 has several speeds. Only NORMAL works today; the others stay",
    "grey until their decoders are verified.",
    "",
    "The rows below describe JS8 Normal: 15 second slots, 79 symbols,",
    "8 tones 6.25 Hz apart, about 50 Hz wide, with LDPC(174,87) and a",
    "12-bit CRC. Only heartbeat SNR replies are shown as messages so far.",
    "The SNR number is what the sender reported, not what we measured.",
    "",
    "DECODER BINDING and UTC SLOT CLOCK show if the decoder is running",
    "and the clock is accurate enough for slot timing.",
    "",
    "EXPERT TUNING: leave it off and Auto uses the standard dial for each",
    "band. Check it to type a dial frequency or step it by 10 Hz to",
    "100 kHz (tap DIAL on Live). OrcDial is optional: with expert tuning",
    "on, tapping its centre switches the knob between bands and fine",
    "tuning.",
    "",
    "OrcSDR only receives. It never transmits.",
};

template <size_t N>
constexpr size_t count_of(const char* const (&)[N]) { return N; }

}  // namespace

Page page(Topic topic) {
  switch (topic) {
    case Topic::live: return {"LIVE", kLive, count_of(kLive)};
    case Topic::decodes: return {"DECODES", kDecodes, count_of(kDecodes)};
    case Topic::map: return {"MAP", kMap, count_of(kMap)};
    case Topic::hunter: return {"HUNTER", kHunter, count_of(kHunter)};
    case Topic::heard: return {"HEARD", kHeard, count_of(kHeard)};
    case Topic::setup_ft8: return {"SETUP  -  FT8", kSetupFt8, count_of(kSetupFt8)};
    case Topic::setup_ft4: return {"SETUP  -  FT4", kSetupFt4, count_of(kSetupFt4)};
    case Topic::setup_js8: return {"SETUP  -  JS8CALL", kSetupJs8, count_of(kSetupJs8)};
    case Topic::count: break;
  }
  return {"", nullptr, 0};
}

}  // namespace orcsdr::ft8::info
