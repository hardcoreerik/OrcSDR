#pragma once
// Digital Voice audio service: owns the speech Engine and the task that moves qualified DMR voice from dv_radio's voice
// queue, through the OrcSDR vocoder, into the existing speaker path. Receive only. The UI never sees decoder internals:
// it reads the published Snapshot. One audio owner: audio leaves ONLY through Hooks::audio_out, which main.cpp points at the
// existing queue_audio_samples sink (speaker, volume, mute, recording and web audio all stay exactly as they are).
//
// While the service runs the raw NFM discriminator audio of the browse-band path is blocked (set_exclusive): a digital
// voice channel demodulated as analogue FM is noise, and was the static users heard. Speech is only ever vocoder output.
#include <cstddef>
#include <cstdint>

#include "dv_speech_rec.hpp"
#include "dv_voice_audio.hpp"

namespace orcsdr::dv_voice_service {

struct Hooks {
  void (*audio_out)(const int16_t* mono48k, size_t samples) = nullptr;   // the existing speaker sink
  bool (*audio_allowed)() = nullptr;      // user sound switch / volume path: false = muted by the user
  bool (*audio_path_ready)() = nullptr;   // speaker path can accept audio (receiver present, sink enabled)
  void (*set_exclusive)(bool on) = nullptr;   // block / unblock the analogue NFM demod audio
  uint32_t (*now_ms)() = nullptr;
  uint32_t (*utc_epoch)() = nullptr;     // wall-clock seconds for recording file names, 0 = unknown
  // Optional pump thread (ESP-IDF: dv_voice_service_task.cpp). Without it the owner calls pump() from its own loop.
  bool (*spawn)(void (*fn)(void*), void* arg) = nullptr;
  void (*join)() = nullptr;
  void (*sleep_ms)(uint32_t ms) = nullptr;
};
void fill_platform_hooks(Hooks& hooks);   // ESP-IDF only

void install(const Hooks& hooks);
bool start();           // allocate (PSRAM), enable the voice queue, block NFM audio. False without memory.
void stop();            // unblock NFM audio, free everything
bool running();
void pump();            // one service step: queue -> engine -> speaker. Called by the task or by host tests.

// Controls (any thread; applied by the next pump).
void set_enabled(bool on);
bool enabled();
void set_listen(dv_voice::Listen listen);
void reset();           // drop audio and codec state
void set_buffer_ms(uint32_t ms);
// Optional recording of DECODED DMR speech (SETUP toggle "SAVE DMR SPEECH"). Off by default. The pump thread collects the call, the UI
// task writes the finished file: speech_ready() hands it over, speech_release() gives the buffer back.
void set_save_speech(bool on);
bool save_speech();
bool speech_ready(const int16_t** pcm, size_t* samples, char* name, size_t name_size, uint8_t header[dv_speech_rec::kHeaderBytes]);
void speech_release(bool saved);
dv_speech_rec::Stats speech_stats();      // playout hold after the first qualified group (jitter buffer), 0..1000 ms
bool play_tone(uint32_t ms);          // speaker-path isolation: known-good 1 kHz PCM through the same resampler/ring/speaker

// Published copy of the engine state (safe to read from the UI task).
struct Published {
  dv_voice::Snapshot engine;
  bool path_ready = false, allowed = false, running = false;
  uint32_t nfm_blocked_blocks = 0;     // analogue demod blocks refused while exclusive (diagnostic: static source)
  uint32_t pumps = 0, max_pump_us = 0, queue_pending = 0;
  uint32_t stack_hwm_bytes = 0;
  uint32_t revision = 0;               // changes whenever a displayed value changes
};
Published snapshot();
void note_nfm_blocked();              // called by the gate in main.cpp
uint32_t nfm_blocked_blocks();
// RTL_DV AUDIO ... commands. `write_line` receives each response line. True when the command was an AUDIO command.
bool handle_command(const char* arg, bool authenticated, void (*write_line)(const char*));
void format_status(char* out, size_t size);
// One short ORC_SUPPORT_DV_AUDIO line for RTL_SUPPORT REPORT. Writes an empty string when the service is not running.
void format_support(char* out, size_t size);
// Built-in self test (vocoder known frames, resampler, selection / mute logic). Returns failures; writes a report line.
int selftest(char* report, size_t size);

}  // namespace orcsdr::dv_voice_service
