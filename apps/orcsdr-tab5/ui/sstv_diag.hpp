#pragma once
// Serial diagnostics for the SSTV receiver, for bench validation on the device:
//
//   SSTV STATUS                       state, metrics, decoder cost, memory, archive counters (no auth)
//   SSTV OPEN                         open the SSTV dashboard (auth)
//   SSTV SELFTEST ROBOT36|ROBOT36_NOVIS|PD120 [SPEED <1..8>]
//                                     play a synthetic transmission through the real decoder,
//                                     display and SD archive path, paced at 1x real time by default (auth)
//   SSTV SELFTEST STOP                abort the running self-test (auth)
//   SSTV SAVE LIVE|NEWEST             queue the image in progress / the newest gallery image for SD (auth)
//   SSTV TUNE <hz>                    tune the open dashboard to a frequency in Hz (auth)
//   SSTV MODE AUTO|NFM|USB|LSB | IFBW <hz> | STEP <hz> | RAILS ON|OFF   TUNE tab settings (auth)
//   SSTV REC START <seconds> | SAVE | STATUS   record the decoder's own 12 kHz audio and save it as a WAV on the SD card (auth)
//   SSTV LAST                         newest gallery image: mode, rows, sync locked/missed (no auth)
//   SSTV INDEX                        print the last lines of the SD index (no auth)
//
// The self-test generates audio inside the firmware (no RF, no transmitter). It feeds the same
// offer_audio_48k() tap the demodulator uses, so decoder cost, ring behaviour, drawing and SD
// writes can be measured without an antenna or a signal source.
#include <cstddef>
#include <cstdint>

namespace orcsdr::sstv_diag {

struct Hooks {
  void (*write_line)(const char* text) = nullptr;
  void (*open_dashboard)() = nullptr;
  bool (*save_image)(int32_t which) = nullptr;   // -1 = image in progress, otherwise gallery index
  bool (*tune)(uint32_t frequency_hz) = nullptr; // false when refused (dashboard closed or out of range)
  bool (*tune_command)(const char* args, char* reply, size_t size) = nullptr;   // MODE / IFBW / STEP / RAILS
  void (*tune_summary)(char* out, size_t size) = nullptr;
};

bool handle_command(const char* command, bool authenticated, const Hooks& hooks);
// Called from the SSTV main-loop tick before the decoder is serviced.
void tick(uint32_t now_ms);
bool selftest_running();
void abort_selftest();

}  // namespace orcsdr::sstv_diag
