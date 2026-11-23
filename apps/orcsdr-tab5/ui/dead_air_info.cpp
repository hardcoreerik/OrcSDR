#include "dead_air_info.hpp"

namespace orcsdr::dead_air::info {
namespace {

const char* const kOverview[] = {
    "Dead Air is a logbook of measurements from your receiver.",
    "It records what the radio actually measured, with a time, and",
    "nothing else. It does not detect ghosts or voices from beyond.",
    "",
    "INVESTIGATION   recording state, elapsed time, tuned frequency.",
    "EVENT LOG       whether events are being saved to the SD card.",
    "RF / AUDIO      latest measured levels in dBFS (relative to the",
    "                receiver's full scale, not calibrated power).",
    "SENSITIVITY     how far above its baseline a level must rise",
    "                before it counts as an event. Set on Events.",
    "RF STRIP        the last minute of RF level. Green dashes: the",
    "                baseline. Yellow: the event limit. A break in the",
    "                line is a moment with no valid measurement.",
    "                A coloured tick marks an event.",
    "ORB             glows with how close the level is to the limit and",
    "                bursts when an event is logged: a cue, not a cause.",
    "SESSION         counts and extremes of what was measured so far.",
    "LATEST EVENTS   the four newest entries from the event log.",
    "",
    "A dash means no measurement is available. Nothing is invented",
    "to fill a gap: with no receiver the readings stay blank.",
    "ENV III is an optional sensor module; blank if not connected.",
};

const char* const kSpectrum[] = {
    "The trace is the live spectrum of the radio signal around the",
    "tuned frequency; the waterfall below is its recent history.",
    "Brighter colour means a stronger signal. Levels are dBFS,",
    "relative to the receiver's full scale.",
    "",
    "TUNING   step the frequency down or up.",
    "MODE     choose the demodulator. Only real demodulators the",
    "         receiver already has are offered; none are simulated.",
    "SWEEP    steps through a band, pausing (dwell) on each step.",
    "         START SWEEP begins; the preset and dwell are shown",
    "         when it is stopped. The radio returns to where it was",
    "         when you stop.",
    "",
    "While sweeping, the log records the gaps: time spent on other",
    "frequencies is marked as not observed, never as quiet.",
    "",
    "PEAK   the strongest point of the trace (not the tuner's centre).",
    "TAP    the plot or waterfall to tune to that frequency.",
    "MAP    after a sweep, a bar per channel shows the level measured",
    "       there. Tap a bar to tune. LIVE / MAP switches the view.",
};

const char* const kAudio[] = {
    "This is a level meter for the audio the receiver demodulates.",
    "It shows how loud that audio is, measured, not what it says.",
    "",
    "RMS     average level of the audio, in dBFS.",
    "PEAK    loudest instant in the same window.",
    "BASELINE  the audio level the screen has learned as normal.",
    "          An AUDIO BURST event is logged when the level rises",
    "          well above it. The baseline is blank until learned.",
    "",
    "With no demodulated audio (no receiver, or the demodulator is",
    "off) the view says so and draws no levels.",
    "",
    "The Tab5 microphone is a different source. It is never mixed",
    "into this meter; see the Review tab for microphone clips.",
};

const char* const kEnvironment[] = {
    "ENV III is an optional sensor module for temperature, humidity",
    "and air pressure. It is not required for anything in Dead Air.",
    "",
    "Connected  readings appear with their units and update live.",
    "Not found  the fields show a dash. Nothing is estimated and",
    "           no earlier value is shown as if it were current.",
    "",
    "A temperature, humidity or pressure reading held away from its",
    "learned baseline is logged as an event (see the Events tab).",
};

const char* const kEvents[] = {
    "Every entry is a measured event or a note you made, in order,",
    "newest first, with its time and level.",
    "",
    "RF RISE      the radio signal rose above its baseline.",
    "AUDIO RISE   the demodulated audio rose above its baseline.",
    "MARK         you pressed MARK EVENT: a note that something",
    "             happened at this moment. It is not a detection.",
    "CLIP         a short recording is saved with this event.",
    "TAP A ROW    shows that event's time, level, baseline and limit.",
    "             VIEW CLIP opens its clip on Review. NEWER and OLDER",
    "             page through the log.",
    "SCRUB        the level 10 s either side of an event, and its clip.",
    "CONTROL RUN  counts false alarms per hour on an empty setup.",
    "CHIME        a short ping when an event is logged (off at start).",
    "",
    "SENSITIVITY  Quiet, Standard or Sensitive: how far above the",
    "             baseline a level must go. Sensitive logs more,",
    "             including more false alarms from ordinary noise.",
    "",
    "Events are saved to the SD card as the session log. A line",
    "that could not be saved is counted, not hidden.",
};

const char* const kReview[] = {
    "SESSION ARCHIVE   the event log file on the SD card: saved,",
    "                  lost and dropped counts.",
    "",
    "CLIPS   an event saves 5 s before and 5 s after, as a WAV file",
    "        with its SHA-256 hash. RADIO clips are the receiver's",
    "        demodulated audio; they are never altered.",
    "MIC     optional and OFF at every start. START MIC shows a red",
    "        banner. It records the room, as separate files, and",
    "        pauses while the speaker is on so it never records the",
    "        radio. It stops when you leave this screen.",
    "",
    "Tap a clip to select it. VERIFY SELECTED rereads the file and",
    "checks its hash. PLAY SELECTED does that first and plays the",
    "clip only if it matches, muting the radio while it plays.",
    "",
    "Each replay is noted in the log. Clips are limited per session.",
};

const Page kPages[] = {
    {"OVERVIEW", kOverview, sizeof(kOverview) / sizeof(kOverview[0])},
    {"SPECTRUM", kSpectrum, sizeof(kSpectrum) / sizeof(kSpectrum[0])},
    {"AUDIO", kAudio, sizeof(kAudio) / sizeof(kAudio[0])},
    {"ENVIRONMENT", kEnvironment, sizeof(kEnvironment) / sizeof(kEnvironment[0])},
    {"EVENTS", kEvents, sizeof(kEvents) / sizeof(kEvents[0])},
    {"REVIEW", kReview, sizeof(kReview) / sizeof(kReview[0])},
};

}  // namespace

Page page(Topic topic) {
  const size_t i = static_cast<size_t>(topic);
  return i < static_cast<size_t>(Topic::count) ? kPages[i] : kPages[0];
}

}  // namespace orcsdr::dead_air::info
