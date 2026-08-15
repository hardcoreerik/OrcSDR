#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <SD_MMC.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp32-hal-hosted.h>
#include <esp_mac.h>
#include <esp_intr_alloc.h>
#include <esp_heap_caps.h>
#include <esp_attr.h>
#include <esp_app_desc.h>
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>
#include <driver/usb_serial_jtag.h>
#include <usb/usb_helpers.h>
#include <usb/usb_host.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

extern "C" {
#include "mbelib.h"
}

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <iterator>

#if !defined(RTL_USE_LEGACY_USB)
#define RTL_USE_LEGACY_USB 0
#endif

#if !defined(ORC_LORA_TEST_BUILD)
#define ORC_LORA_TEST_BUILD 0
#endif

#include "rtl_sdr_v4_transfers.h"
#include "orcsdr_splash.hpp"
#include "adsb_dashboard.hpp"
#include "adsb_decoder.hpp"
#include "catalog_sync.hpp"
#include "dashboard_audio_control.hpp"
#include "dashboard_registry.hpp"
#include "fm_dashboard.hpp"
#include "fm_config.hpp"
#include "home_dashboard.hpp"
#include "p25_dashboard.hpp"
#include "p25_config.hpp"
#include "p25_decoder.hpp"
#include "settings_app.hpp"
#include "ui_capture.hpp"
#if !RTL_USE_LEGACY_USB
#include "rtl_sdr_v4_esp.h"
#endif

class OrcConsole {
 public:
  void begin(uint32_t) {
    if (usb_serial_jtag_is_driver_installed()) return;
    usb_serial_jtag_driver_config_t config = {
        .tx_buffer_size = 4096,
        .rx_buffer_size = 1024,
    };
    usb_serial_jtag_driver_install(&config);
  }

  int available() {
    if (has_pending_) return 1;
    has_pending_ = usb_serial_jtag_read_bytes(&pending_, 1, 0) == 1;
    return has_pending_ ? 1 : 0;
  }

  int read() {
    if (!has_pending_) return -1;
    has_pending_ = false;
    return pending_;
  }

  size_t readBytes(uint8_t* output, size_t size, uint32_t timeout_ms) {
    if (output == nullptr || size == 0) return 0;
    size_t received = 0;
    if (has_pending_) {
      output[received++] = pending_;
      has_pending_ = false;
    }
    while (received < size) {
      const int count = usb_serial_jtag_read_bytes(
          output + received, size - received, pdMS_TO_TICKS(timeout_ms));
      if (count <= 0) break;
      received += static_cast<size_t>(count);
    }
    return received;
  }

  void print(char value) { write(&value, 1); }
  void print(const char* value) { write(value, strlen(value)); }
  void println() { print('\n'); }
  void println(const char* value) {
    print(value);
    println();
  }

  void printf(const char* format, ...) {
    char output[2048];
    va_list args;
    va_start(args, format);
    const int length = vsnprintf(output, sizeof(output), format, args);
    va_end(args);
    if (length > 0) write(output, min(static_cast<size_t>(length), sizeof(output) - 1));
  }

  size_t writeBytes(const uint8_t* data, size_t size) {
    size_t written = 0;
    while (written < size) {
      const int count = usb_serial_jtag_write_bytes(
          data + written, size - written, pdMS_TO_TICKS(3000));
      if (count <= 0) break;
      written += static_cast<size_t>(count);
    }
    return written;
  }

 private:
  void write(const void* data, size_t size) {
    usb_serial_jtag_write_bytes(data, size, pdMS_TO_TICKS(100));
  }

  uint8_t pending_ = 0;
  bool has_pending_ = false;
};

OrcConsole orc_console;
#ifdef Serial
#undef Serial
#endif
#define Serial orc_console

namespace {
constexpr int kButtonX = 390;
constexpr int kButtonY = 300;
constexpr int kButtonWidth = 500;
constexpr int kButtonHeight = 120;
constexpr uint32_t kJournalMagic = 0x4f4a4e32;
constexpr uint32_t kWorkflowMagic = 0x4f574631;
constexpr size_t kJournalCapacity = 8;
constexpr uint32_t kSessionTimeoutMs = 5000;
constexpr int kWifiClockPin = 12;
constexpr int kWifiCommandPin = 13;
constexpr int kWifiData0Pin = 11;
constexpr int kWifiData1Pin = 10;
constexpr int kWifiData2Pin = 9;
constexpr int kWifiData3Pin = 8;
constexpr int kWifiResetPin = 15;
constexpr uint32_t kRtlSampleRateSps = 960000;
constexpr uint32_t kRtlCaptureBytes = 9600000;
constexpr size_t kRtlBulkBytes = 32768;
constexpr size_t kRtlControlMps = 64;
constexpr uint32_t kRtlControlTimeoutMs = 1000;
constexpr uint32_t kRtlCaptureTimeoutMs = 30000;
/* Demod staging (~one URB). */
constexpr size_t kRtlAudioBufferSamples = 2048;
/* ~25 ms blocks at 48 kHz. M5Unified requires rotating runtime buffers. */
constexpr size_t kRtlAudioPlayBatchSamples = 2048;
constexpr size_t kRtlAudioPlayBlockCount = 3;
constexpr size_t kRtlAudioPlayBlockFrames = kRtlAudioPlayBatchSamples;
constexpr size_t kRtlAudioPlayBlockSamples = kRtlAudioPlayBlockFrames * 2;
constexpr uint32_t kRtlAudioPlaySafetyMs = 12;
constexpr uint32_t kP25VoiceHangMs = 1800;
constexpr uint32_t kP25VoiceAcquireMs = 3000;
/*
 * WBFM mono DSP targets (app-side; not RF hardware calibration).
 * Pipeline: 960 kS/s IQ → complex channel LPF → ×4 → discr → audio LPF → ×5 → 48 kHz
 * → 75 µs de-emphasis (US broadcast) → DC block → soft AGC/limiter.
 * Aimed at rtl_fm -M wbfm-ish behavior without heavy FIR cost on P4.
 */
/** Post-discr one-pole at 240 kS/s; ~16 kHz mono audio before decimate. */
constexpr float kWbfmAudioLpfK = 0.34f;
/** 75 µs de-emphasis @ 48 kHz: k = 1 - exp(-1/(τ·fs)), τ=75e-6. */
constexpr float kWbfmDeemphK = 0.2424f;
/** NFM (WX): tighter post-discr LPF; little/no de-emphasis. */
constexpr float kNfmAudioLpfK = 0.55f;
constexpr float kNfmDeemphK = 0.08f;
/** Boxcar decimation: 960k/4 = 240k discr rate; 240k/5 = 48 kHz audio. */
constexpr uint8_t kFmRfDecim = 4;
constexpr uint8_t kFmAudioDecim = 5;
/*
 * FM stereo recovery, all at the 240 kS/s discriminator rate.
 *
 * The composite (MPX) that falls out of the discriminator carries
 *   0-15 kHz  L+R      19 kHz  pilot      23-53 kHz  L-R (DSB-SC on 38 kHz)
 * and 240 kS/s leaves Nyquist at 120 kHz, so all of it survives.
 *
 * The 38 kHz carrier is synthesised by SQUARING the recovered pilot
 * (cos²x = ½(1+cos2x)), which is phase-locked to the pilot by construction.
 * That avoids a per-sample trig PLL on the audio hot path — this loop already
 * runs 240k times a second and shares a core with USB.
 *
 * Two-pole resonators, H(z) = (1 - z^-2) / (1 - 2r·cosθ·z^-1 + r²·z^-2):
 *   r = 1 - pi*BW/fs,  θ = 2*pi*f0/fs
 */
/** 19 kHz pilot, ~3 kHz BW: r=0.960730, θ=0.497419. */
constexpr float kPilotTwoRCos = 1.688612f;
constexpr float kPilotR2 = 0.923002f;
/** 38 kHz regenerated carrier, ~6 kHz BW: r=0.921460, θ=0.994838. */
constexpr float kSubTwoRCos = 1.003726f;
constexpr float kSubR2 = 0.849089f;
/** Envelope trackers (~30 ms at 240 kS/s) for lock detect and carrier AGC. */
constexpr float kStereoEnvK = 0.00014f;
/** Pilot is nominally 8-10% of peak deviation; latch well below that. */
constexpr float kStereoLockOn = 0.030f;
constexpr float kStereoLockOff = 0.018f;
/** 57 kHz RDS subcarrier, ~2.4 kHz half-BW (matches the actual ~4.8 kHz
 * occupied RDS signal — narrowed from an initial 4 kHz Stage-1-only guess
 * after Stage 2 block sync failed to converge on real air; a too-wide
 * bandpass was inflating the carrier-energy estimate): r=0.960923,
 * θ=1.492257. */
constexpr float kRdsBpTwoRCos = 0.151988f;
constexpr float kRdsBpR2 = 0.938155f;
constexpr float kRdsEnvK = 0.00014f;
/** RDS is a much smaller fraction of deviation than the pilot; threshold is
 * provisional until verified against a real broadcast. */
constexpr float kRdsCarrierOn = 0.006f;
constexpr float kRdsCarrierOff = 0.0035f;
/*
 * RDS Stage 2 — symbol/bit/block recovery. The nominal carrier is mixed with a
 * recursive complex-rotator NCO (4 mul + 2 add per sample) rather than
 * per-sample sinf/cosf: at 240 kS/s, calling sinf+cosf every sample would
 * cost ~480,000 transcendental calls/sec just for RDS, on top of everything
 * else demodulate_fm already does. The rotation increment (which needs one
 * sin+cos pair) is only recomputed every kRdsNcoRecalcSamples — the loop's
 * fixed complex rotator; differential complex chip pairing cancels carrier
 * phase and slow tuner offset without a Costas loop.
 */
constexpr float kRdsNcoNominal = 1.492257f;      /* 2*pi*57000/240000 rad/sample */
constexpr int kRdsNcoRecalcSamples = 64;
constexpr float kRdsSymLpfK = 0.060899f;         /* single-pole, ~2.4 kHz BW */
/* Measured from the verified 96.1 MHz MPX capture. Keep this explicit because
 * the RTL sample clock is physical hardware, not an exact 240 kHz source. */
constexpr float kRdsChipRateCorrectionPpm = -10.0f;
constexpr float kRdsChipInc =
    (2375.0f / 240000.0f) * (1.0f + kRdsChipRateCorrectionPpm * 1.0e-6f);
constexpr size_t kRdsTimingTracks = 4;
constexpr uint32_t kRdsMpxRateHz = kRtlSampleRateSps / kFmRfDecim;
constexpr size_t kRdsCaptureSeconds = 8;
constexpr size_t kRdsCaptureSamples = kRdsMpxRateHz * kRdsCaptureSeconds;
constexpr float kRdsMpxToInt16 = 32767.0f / 3.14159265358979323846f;
constexpr float kRdsInt16ToMpx = 1.0f / kRdsMpxToInt16;
static_assert(kRdsMpxRateHz == 240000, "RDS MPX file format requires 240 kS/s");
/* RDS/RBDS generator polynomial x^10+x^8+x^7+x^5+x^4+x^3+1, and the five
 * offset words (IEC 62106). Verified by direct polynomial-division check
 * against all five before this was written into demodulate_fm. */
constexpr uint32_t kRdsGenPoly = 0b10110111001;  /* 11 bits, degree 10 explicit */
constexpr uint16_t kRdsOffsetA = 0x0FC;
constexpr uint16_t kRdsOffsetB = 0x198;
constexpr uint16_t kRdsOffsetC = 0x168;
constexpr uint16_t kRdsOffsetCp = 0x350;         /* C', used in version-B groups */
constexpr uint16_t kRdsOffsetD = 0x1B4;
constexpr int kRdsBadBlocksBeforeUnlock = 32;

/** One candidate bit-alignment hypothesis for RDS block sync. Chip pairing
 * has a 1-chip (half-bit) ambiguity that can't be resolved analytically —
 * two of these run in parallel, offset by one chip, and whichever achieves
 * sustained CRC lock first is the correct alignment. Standard technique for
 * this exact ambiguity, not a workaround. */
struct RdsHypothesis {
  uint32_t shift_reg = 0;
  int bit_fill = 0;               /* caps at 26: "do we have a full window yet" */
  bool have_prev_pair = false;
  float prev_pair_i = 0;
  float prev_pair_q = 0;
  bool have_prev_match = false;
  int prev_match_type = 0;        /* 0=A,1=B,2=C/C',3=D */
  uint32_t prev_match_bitpos = 0;
  int streak = 0;
  bool locked = false;
  int next_expected_type = 0;
  uint32_t next_expected_bitpos = 0;
  int bad_block_streak = 0;
  uint32_t bit_pos = 0;
  uint16_t group_info[4] = {0, 0, 0, 0};
  bool group_info_valid[4] = {false, false, false, false};
  uint32_t good_blocks = 0;
  uint32_t total_blocks = 0;
};

struct RdsTimingTrack {
  float chip_i_sum = 0;
  float chip_q_sum = 0;
  float prev_chip_i = 0;
  float prev_chip_q = 0;
  float chip_phase = 0;
  bool have_prev_chip = false;
  uint32_t chip_index = 0;
  RdsHypothesis hyp[2];
};

/** Binary polynomial division of a 26-bit RDS block by the generator poly.
 * For an error-free received block, this equals the offset word used at
 * the transmitter (A/B/C/C'/D) — verified against all five in Python
 * before this was written (see phasing.md's RDS Stage 2 notes). */
uint16_t rds_syndrome(uint32_t block26) {
  uint32_t reg = block26 & 0x3FFFFFFu;
  for (int deg = 25; deg >= 10; --deg) {
    if (reg & (1u << deg)) reg ^= kRdsGenPoly << (deg - 10);
  }
  return static_cast<uint16_t>(reg & 0x3FFu);
}

/** Classify a syndrome as block type 0=A,1=B,2=C/C',3=D, or -1 for no match. */
int rds_offset_match(uint16_t syn) {
  if (syn == kRdsOffsetA) return 0;
  if (syn == kRdsOffsetB) return 1;
  if (syn == kRdsOffsetC || syn == kRdsOffsetCp) return 2;
  if (syn == kRdsOffsetD) return 3;
  return -1;
}

/**
 * Feed one differentially-decoded data bit into a block-sync hypothesis.
 *
 * Search mode (not yet locked): checks the syndrome at every bit position.
 * Four consecutive matches in A->B->C(C')->D order at exactly 26-bit
 * spacing declares lock. Positional mode (locked): only checks the syndrome
 * at the next expected 26-bit boundary — cheaper, and gives a clean BLER
 * count (attempted vs. matched) since every expected position is counted
 * whether or not the block checks out. Drops back to search mode after a
 * sustained 32-block failure burst (~0.67 s), not a short noisy-air fade.
 */
void rds_hypothesis_feed(RdsHypothesis& h, bool bit) {
  h.shift_reg = ((h.shift_reg << 1) | (bit ? 1u : 0u)) & 0x3FFFFFFu;
  if (h.bit_fill < 26) ++h.bit_fill;
  ++h.bit_pos;
  if (h.bit_fill < 26) return;

  if (!h.locked) {
    const int match_type = rds_offset_match(rds_syndrome(h.shift_reg));
    if (match_type >= 0) {
      if (h.have_prev_match && (h.bit_pos - h.prev_match_bitpos == 26) &&
          match_type == (h.prev_match_type + 1) % 4) {
        ++h.streak;
      } else {
        h.streak = 1;
      }
      h.prev_match_type = match_type;
      h.prev_match_bitpos = h.bit_pos;
      h.have_prev_match = true;
      h.group_info[match_type] = static_cast<uint16_t>(h.shift_reg >> 10);
      h.group_info_valid[match_type] = true;
      if (h.streak >= 4) {
        h.locked = true;
        h.next_expected_type = (match_type + 1) % 4;
        h.next_expected_bitpos = h.bit_pos + 26;
        h.bad_block_streak = 0;
        h.good_blocks = static_cast<uint32_t>(h.streak);
        h.total_blocks = static_cast<uint32_t>(h.streak);
      }
    }
  } else if (h.bit_pos == h.next_expected_bitpos) {
    const uint16_t syn = rds_syndrome(h.shift_reg);
    bool ok = false;
    switch (h.next_expected_type) {
      case 0: ok = (syn == kRdsOffsetA); break;
      case 1: ok = (syn == kRdsOffsetB); break;
      case 2: ok = (syn == kRdsOffsetC || syn == kRdsOffsetCp); break;
      default: ok = (syn == kRdsOffsetD); break;
    }
    ++h.total_blocks;
    if (ok) {
      ++h.good_blocks;
      h.bad_block_streak = 0;
      h.group_info[h.next_expected_type] = static_cast<uint16_t>(h.shift_reg >> 10);
      h.group_info_valid[h.next_expected_type] = true;
    } else {
      ++h.bad_block_streak;
      h.group_info_valid[h.next_expected_type] = false;
      if (h.bad_block_streak >= kRdsBadBlocksBeforeUnlock) {
        h.locked = false;
        h.streak = 0;
        h.have_prev_match = false;
        /* Without this, good/total stayed frozen at whatever they were
         * when lock was lost, making a stale past episode look like a
         * live 60%-ish BLER forever instead of showing search is ongoing. */
        h.good_blocks = 0;
        h.total_blocks = 0;
      }
    }
    h.next_expected_type = (h.next_expected_type + 1) % 4;
    h.next_expected_bitpos = h.bit_pos + 26;
  }
}
/*
 * Scope FFT size. 256 bins @ 960 kS/s ≈ 3.75 kHz/bin (was 128 / 7.5 kHz).
 * Welch multi-window average runs only when GFX is on and audio is not stressed.
 */
constexpr size_t kRtlSpectrumBins = 256;
/** Average this many non-overlapping windows for a quieter, more precise trace. */
constexpr size_t kRtlSpectrumWelchWindows = 2;
constexpr bool kStreamDiagnosticsEnabled = false;
// Keep scope cadence stable when sound is toggled; only back off if audio drops.
constexpr uint32_t kRtlSpectrumIntervalMs = 100;
constexpr uint32_t kRtlSpectrumStressedIntervalMs = 220;
constexpr size_t kRtlRingDepth = 3;
constexpr uint32_t kRtlAudioPrimeMs = 450;
constexpr uint32_t kRtlSignalMeterIntervalMs = 200;
/* Tab5 microSD (M5 docs): SPI pins for optional WAV export. */
constexpr int kTab5SdCsPin = 42;
constexpr int kTab5SdSckPin = 43;
constexpr int kTab5SdMosiPin = 44;
constexpr int kTab5SdMisoPin = 39;
/** Post-demod mono capture rate (matches playRaw). */
constexpr uint32_t kAudioRecRateHz = 48000;
/** ~12 s of int16 mono in PSRAM — enough for interference A/B without SD thrash mid-stream. */
constexpr size_t kAudioRecMaxSeconds = 12;
constexpr size_t kAudioRecMaxSamples = kAudioRecRateHz * kAudioRecMaxSeconds;
constexpr UBaseType_t kRtlAppTaskPrio = 5;
constexpr int kSpectrumX = 64;
constexpr int kSpectrumY = 96;
constexpr int kSpectrumWidth = 1152;
constexpr int kCbSpectrumWidth = 768;
constexpr int kCbPanelX = kSpectrumX + kCbSpectrumWidth;
constexpr int kCbPanelY = kSpectrumY;
constexpr int kCbPanelWidth = 384;
constexpr int kCbPanelHeight = 470;
constexpr char kCbDashboardPath[] = "/orcsdr/cb_dashboard_384x470.jpg";
// Full 1152x470 custom dashboard (same footprint as the LoRa command center),
// laid out to match docs mockup: two-column card grid, origin at (kSpectrumX, kSpectrumY).
constexpr char kLoraLivePath[] = "/orcsdr/lora_live_command_center_1152x470.jpg";
constexpr char kLoraMessagesPath[] = "/orcsdr/lora_messages_command_center_1152x470.jpg";
constexpr char kLoraMapPath[] = "/orcsdr/lora_map_command_center_1152x470.jpg";
constexpr int kSpectrumHeight = 200;
constexpr int kWaterfallY = 316;
constexpr int kWaterfallHeight = 250;
// Two control rows under the waterfall; keep spectrum/waterfall dominant.
constexpr int kSdrBandY = 580;
constexpr int kSdrTuneY = 648;
constexpr int kSdrControlsHeight = 52;
constexpr int kSdrGap = 12;
constexpr int kSdrEdge = 48;
/*
 * Portable RF tool shell (Tab5 + Blog V4).
 * Radio is the first product surface; Scope/Capture are analysis tools.
 * Future tools (band scan, IQ dump, gain lab, analyzer) plug in here — do not
 * hard-wire every new feature only into the FM listen path.
 */
enum class OrcTool : uint8_t {
  Radio = 0,
  Scope = 1,
  Capture = 2,
  /* Reserved for growth: Analyzer, Scan, GainLab, IqDump */
  Count = 3
};
constexpr int kToolTabY = 68;
constexpr int kToolTabH = 28;
constexpr int kToolTabW = 150;
constexpr int kToolTabGap = 10;
constexpr int kPinchToggleX = 1040;
constexpr int kPinchToggleY = 66;
constexpr int kPinchToggleW = 192;
constexpr int kPinchToggleH = 30;
constexpr int kNavPanelX = 760;
constexpr int kNavPanelY = 100;
constexpr int kNavPanelW = 456;
constexpr int kNavPanelH = 456;
constexpr uint8_t kRtlVolumeMin = 0;
constexpr uint8_t kRtlVolumeMax = 255;
// ~50% of the M5 speaker scale (0-255). Operator found 220 too loud as a start.
constexpr uint8_t kRtlVolumeDefault = 128;
constexpr uint8_t kRtlVolumeStep = 16;
constexpr uint32_t kRtlFmMinHz = 87500000;
constexpr uint32_t kRtlFmMaxHz = 108000000;
/* FREQ +/- coarse step. Header still shows 0.001 MHz; LO apply is 5 kHz. */
constexpr uint32_t kRtlFmStepHz = 100000;
constexpr uint32_t kRtlFmAutoStepHz = 800000;
constexpr uint32_t kRtlFmAutoSettleMs = 500;
/** LO quantize for hot retune — finer than this thrashes USB/audio. */
constexpr uint32_t kRtlHotRetuneQuantHz = 5000;
/** Min time between LO applies (each apply drains bulk + EP0). */
constexpr uint32_t kRtlHotRetuneMinIntervalMs = 280;
constexpr uint32_t kRtlScopeSpanMinHz = 120000;
constexpr uint32_t kRtlScopeSpanMaxHz = 960000;
constexpr uint32_t kRtlFmFilterDefaultHz = 180000;
constexpr uint32_t kRtlAmFilterDefaultHz = 10000;
constexpr uint32_t kRtlWxFilterDefaultHz = 25000;
static_assert(kRtlScopeSpanMinHz < kRtlScopeSpanMaxHz);
// KZEL (Santa Rosa): best measured LO on Tab5+V4 was 96.113 MHz (not 96.100).
constexpr uint32_t kRtlFmDefaultHz = 96113000;
constexpr uint32_t kRtlAmMinHz = 520000;
constexpr uint32_t kRtlAmMaxHz = 1710000;
constexpr uint32_t kRtlAmStepHz = 10000;
constexpr uint32_t kRtlAmDefaultHz = 1000000;
constexpr uint32_t kRtlWxHz = 162400000;
constexpr uint32_t kRtlBrowseMinHz = RTL_SDR_V4_ESP_FREQ_MIN_HZ;
constexpr uint32_t kRtlBrowseMaxHz = RTL_SDR_V4_ESP_FREQ_MAX_HZ;
constexpr uint32_t kRtlBrowseDefaultHz = 146520000;
constexpr uint32_t kLoraMinHz = 902000000;
constexpr uint32_t kLoraMaxHz = 928000000;
constexpr uint32_t kLoraDefaultHz = 906875000;  // Meshtastic US LongFast default slot
constexpr uint32_t kAdsbDefaultHz = 1090000000;
constexpr uint32_t kP25MinHz = 450000000;
constexpr uint32_t kP25MaxHz = 470000000;
constexpr uint32_t kP25DefaultHz = 453812500;
constexpr uint32_t kP25StepHz = 12500;
// Clean-room LO offset: LO = RF + 1.814972 MHz (from 100 MHz observation).
constexpr double kRtlIfOffsetHz = 1814972.0;
constexpr double kRtlXtalHz = 28800000.0;

enum class RtlBand : uint8_t { fm, am, wx, cb, lora, browse, adsb, p25 };
enum class CbMode : uint8_t { am, usb, lsb };

constexpr uint32_t kCbChannelsHz[] = {
    26965000, 26975000, 26985000, 27005000, 27015000, 27025000, 27035000,
    27055000, 27065000, 27075000, 27085000, 27105000, 27115000, 27125000,
    27135000, 27155000, 27165000, 27175000, 27185000, 27205000, 27215000,
    27225000, 27255000, 27235000, 27245000, 27265000, 27275000, 27285000,
    27295000, 27305000, 27315000, 27325000, 27335000, 27345000, 27355000,
    27365000, 27375000, 27385000, 27395000, 27405000};
static_assert(std::size(kCbChannelsHz) == 40);
constexpr uint32_t kCbDefaultHz = kCbChannelsHz[18];
constexpr bool cb_channel_plan_valid() {
  if (kCbDefaultHz != 27185000) return false;
  for (size_t i = 0; i < std::size(kCbChannelsHz); ++i) {
    if (kCbChannelsHz[i] < 26965000 || kCbChannelsHz[i] > 27405000) return false;
    for (size_t j = i + 1; j < std::size(kCbChannelsHz); ++j) {
      if (kCbChannelsHz[i] == kCbChannelsHz[j]) return false;
    }
  }
  return true;
}
static_assert(cb_channel_plan_valid(), "CB channel plan must contain 40 unique US channels");

struct RfBandGuide {
  uint32_t low_hz;
  uint32_t high_hz;
  uint32_t preset_hz;
  RtlBand mode;
  const char* label;
  const char* description;
  bool quick;
};

/* US receive guide. Allocations overlap; this is identification help, not authority to transmit. */
constexpr RfBandGuide kRfBandGuide[] = {
    {26965000, 27405000, kCbDefaultHz, RtlBand::cb, "CB RADIO", "HF / 40-channel citizens band", true},
    {28000000, 29700000, 28400000, RtlBand::browse, "HAM RADIO", "HF / 10 m amateur", true},
    {50000000, 54000000, 52525000, RtlBand::browse, "HAM RADIO", "VHF / 6 m amateur", true},
    {88000000, 108000000, kRtlFmDefaultHz, RtlBand::fm, "FM BROADCAST", "VHF / music and talk", true},
    {108000000, 118000000, 113000000, RtlBand::browse, "AIR NAV", "VHF / aircraft navigation", false},
    {118000000, 137000000, 121500000, RtlBand::browse, "AIRBAND", "VHF / aircraft voice/emergency", true},
    {137000000, 138000000, 137500000, RtlBand::browse, "NOAA SATELLITE", "VHF / weather downlinks", true},
    {144000000, 148000000, 146520000, RtlBand::browse, "HAM RADIO", "VHF / 2 m amateur", true},
    {156000000, 162025000, 156800000, RtlBand::browse, "MARINE RADIO", "VHF / marine voice/safety", false},
    {162400000, 162550000, kRtlWxHz, RtlBand::wx, "NOAA WEATHER", "VHF / forecasts and alerts", true},
    {222000000, 225000000, 223500000, RtlBand::browse, "HAM RADIO", "VHF / 1.25 m amateur", false},
    {406000000, 406100000, 406050000, RtlBand::browse, "DISTRESS SAT", "UHF / emergency beacons", false},
    {420000000, 450000000, 446000000, RtlBand::browse, "HAM RADIO", "UHF / 70 cm amateur", true},
    {kP25MinHz, kP25MaxHz, kP25DefaultHz, RtlBand::p25, "P25 / LRIG", "UHF / Lane County public safety", true},
    {462550000, 467725000, 462562500, RtlBand::browse, "FRS / GMRS", "UHF / personal two-way", false},
    {kLoraMinHz, kLoraMaxHz, kLoraDefaultHz, RtlBand::lora, "LORA / ISM", "UHF / LoRa CSS and mesh data", true},
    {977900000, 978100000, 978000000, RtlBand::browse, "ADS-B UAT", "UHF / aircraft position", false},
    {1089900000, 1090100000, kAdsbDefaultHz, RtlBand::adsb, "ADS-B / MODE S", "L-band / aircraft tracking", true},
    {1525000000, 1559000000, 1545000000, RtlBand::browse, "SATCOM", "L-band / satellite downlinks", false},
    {1575000000, 1576000000, 1575420000, RtlBand::browse, "GNSS / GPS", "L-band / navigation", false},
    {1610600000, 1626500000, 1620000000, RtlBand::browse, "SATCOM", "L-band / mobile satellite", false},
};
static_assert(std::size(kRfBandGuide) == 21);
constexpr const char* kRfQuickLabels[] = {
    "CB 27", "HAM 10M", "HAM 6M", "FM RADIO", "AIRBAND", "NOAA SAT",
    "HAM 2M", "NOAA WX", "HAM 70CM", "P25 LRIG", "LORA 915", "ADS-B 1090"};
static_assert(std::size(kRfQuickLabels) == 12);

constexpr bool rf_band_guide_valid() {
  size_t quick_count = 0;
  for (const auto& entry : kRfBandGuide) {
    if (entry.low_hz < kRtlBrowseMinHz || entry.high_hz > kRtlBrowseMaxHz ||
        entry.low_hz > entry.preset_hz || entry.preset_hz > entry.high_hz) return false;
    if (entry.quick) ++quick_count;
  }
  return quick_count == std::size(kRfQuickLabels);
}
static_assert(rf_band_guide_valid(), "RF band guide ranges or quick presets are invalid");

// Independently observed 100 MHz final-tune sequence. Fixed presets patch only
// the calculated divider and PLL bytes immediately before these records run.
constexpr RtlControlRecord kRtlFinalTuneTemplate[] = {
    {0x0074, 0x0610, 0x40, 2, {0x17, 0x20}},
    {0x0074, 0x0610, 0x40, 2, {0x1a, 0x2a}},
    {0x0074, 0x0610, 0x40, 2, {0x1b, 0x34}},
    {0x0074, 0x0610, 0x40, 2, {0x10, 0xa4}},
    {0x0074, 0x0610, 0x40, 2, {0x08, 0xc0}},
    {0x0074, 0x0610, 0x40, 2, {0x09, 0x40}},
    {0x0074, 0x0610, 0x40, 2, {0x0c, 0x68}},
    {0x0074, 0x0610, 0x40, 2, {0x10, 0xa4}},
    {0x0074, 0x0610, 0x40, 2, {0x1a, 0x22}},
    {0x0074, 0x0610, 0x40, 2, {0x12, 0x06}},
    {0x0074, 0x0610, 0x40, 1, {0x00}},
    {0x0074, 0x0600, 0xc0, 5, {}},
    {0x0074, 0x0610, 0x40, 2, {0x10, 0x84}},
    {0x0074, 0x0610, 0x40, 2, {0x14, 0xca}},
    {0x0074, 0x0610, 0x40, 2, {0x12, 0x06}},
    {0x0074, 0x0610, 0x40, 2, {0x16, 0x90}},
    {0x0074, 0x0610, 0x40, 2, {0x15, 0x5a}},
    {0x0074, 0x0610, 0x40, 1, {0x00}},
    {0x0074, 0x0600, 0xc0, 3, {}},
    {0x0074, 0x0610, 0x40, 2, {0x1a, 0x2a}},
    {0x0074, 0x0610, 0x40, 2, {0x17, 0x20}},
    {0x0074, 0x0610, 0x40, 2, {0x06, 0x30}},
};

struct RtlAudioState {
  float i_sum = 0;
  float q_sum = 0;
  uint8_t rf_phase = 0;
  float previous_i = 0;
  float previous_q = 0;
  bool have_previous = false;
  /** Complex one-pole state (pre-demod channel LPF on I/Q). */
  float iq_i_lpf = 0;
  float iq_q_lpf = 0;
  /** Post-discriminator mono LPF (was generic channel_filter). */
  float channel_filter = 0;
  float audio_sum = 0;
  uint8_t audio_phase = 0;
  float deemphasis = 0;
  float dc = 0;
  float envelope_filter = 0;
  float agc_gain = 1.0f;
  float agc_level = 2000.0f;
  float last_out = 0;
  float ssb_cos = 1.0f;
  float ssb_sin = 0.0f;
  uint16_t fade_in = 0;
  uint8_t buffer = 0;
  uint64_t samples = 0;
  uint32_t queued_chunks = 0;
  uint32_t dropped_chunks = 0;
  int16_t peak = 0;
  uint64_t square_sum = 0;

  /* ---- FM stereo (WBFM only; decoder runs whenever wbfm=true) ---- */
  /** 19 kHz pilot resonator, two delay lines (Direct Form I, poles only). */
  float pilot_y1 = 0, pilot_y2 = 0;
  /** 38 kHz sub-carrier resonator, driven by the squared pilot. */
  float sub_y1 = 0, sub_y2 = 0;
  float pilot_env = 0;     /* smoothed |pilot| for lock detect / AGC */
  bool stereo_locked = false;
  /** Separate L/R post-discriminator LPF + de-emphasis state. */
  float channel_filter_diff = 0;
  float audio_sum_diff = 0;  /* boxcar accumulator paired with audio_sum/audio_phase */
  float deemphasis_l = 0, deemphasis_r = 0;
  float dc_l = 0, dc_r = 0;

  /* ---- RDS carrier detect (Stage 1 — no bit/block sync yet) ---- */
  /** 57 kHz resonator on the same pre-mono-LPF composite tap the pilot/sub
   * decoders use. Detects RDS subcarrier *presence*, not lock — full symbol
   * recovery is a separate, larger follow-up once this stage is verified
   * against a real broadcast. */
  float rds_bp_y1 = 0, rds_bp_y2 = 0;
  float rds_env = 0;
  bool rds_carrier_present = false;

  /* ---- RDS Stage 2 — complex differential symbol/bit/block recovery ---- */
  /** Recursive complex-rotator NCO (see kRdsNcoNominal comment for why this
   * isn't per-sample sinf/cosf). Unit vector, rotated each RF sample. */
  float rds_nco_i = 1.0f, rds_nco_q = 0.0f;
  float rds_nco_inc_cos = 1.0f, rds_nco_inc_sin = 0.0f;
  int rds_nco_recalc_counter = 0;
  float rds_i_lpf = 0, rds_q_lpf = 0;
  RdsTimingTrack rds_timing[kRdsTimingTracks];
  bool rds_had_block_lock = false;
  uint32_t rds_diag_chip_count = 0;  /* for symbols/sec, reset each print window */
};

RtlAudioState rtl_audio;

void rtl_rds_reset() {
  rtl_audio.rds_bp_y1 = 0;
  rtl_audio.rds_bp_y2 = 0;
  rtl_audio.rds_env = 0;
  rtl_audio.rds_carrier_present = false;
  rtl_audio.rds_nco_i = 1.0f;
  rtl_audio.rds_nco_q = 0.0f;
  rtl_audio.rds_nco_inc_cos = 1.0f;
  rtl_audio.rds_nco_inc_sin = 0.0f;
  rtl_audio.rds_nco_recalc_counter = 0;
  rtl_audio.rds_i_lpf = 0;
  rtl_audio.rds_q_lpf = 0;
  for (size_t index = 0; index < kRdsTimingTracks; ++index) {
    rtl_audio.rds_timing[index] = RdsTimingTrack();
    rtl_audio.rds_timing[index].chip_phase =
        static_cast<float>(index) / static_cast<float>(kRdsTimingTracks);
  }
  rtl_audio.rds_had_block_lock = false;
  rtl_audio.rds_diag_chip_count = 0;
}

struct RdsSelection {
  const RdsHypothesis* best = nullptr;
  bool locked = false;
  int parity_streak[2] = {0, 0};
  float chip_phase = 0;
};

RdsSelection rds_select() {
  RdsSelection result;
  for (const RdsTimingTrack& timing : rtl_audio.rds_timing) {
    for (size_t parity = 0; parity < 2; ++parity) {
      const RdsHypothesis& candidate = timing.hyp[parity];
      if (candidate.streak > result.parity_streak[parity]) {
        result.parity_streak[parity] = candidate.streak;
      }
      const bool better = result.best == nullptr ||
                          (candidate.locked && !result.best->locked) ||
                          (candidate.locked == result.best->locked &&
                           (candidate.good_blocks > result.best->good_blocks ||
                            (candidate.good_blocks == result.best->good_blocks &&
                             candidate.streak > result.best->streak)));
      if (better) {
        result.best = &candidate;
        result.chip_phase = timing.chip_phase;
      }
      result.locked = result.locked || candidate.locked;
    }
  }
  return result;
}

/** Soft reset of FM/NFM filter memory after LO change (keep AGC/fade partially). */
void rtl_audio_reset_demod_filters() {
  rtl_audio.i_sum = 0;
  rtl_audio.q_sum = 0;
  rtl_audio.rf_phase = 0;
  rtl_audio.previous_i = 0;
  rtl_audio.previous_q = 0;
  rtl_audio.have_previous = false;
  rtl_audio.iq_i_lpf = 0;
  rtl_audio.iq_q_lpf = 0;
  rtl_audio.channel_filter = 0;
  rtl_audio.audio_sum = 0;
  rtl_audio.audio_phase = 0;
  rtl_audio.deemphasis = 0;
  rtl_audio.dc = 0;
  rtl_audio.ssb_cos = 1.0f;
  rtl_audio.ssb_sin = 0.0f;
  if (rtl_audio.fade_in > 48) rtl_audio.fade_in = 48;
  rtl_audio.pilot_y1 = 0;
  rtl_audio.pilot_y2 = 0;
  rtl_audio.sub_y1 = 0;
  rtl_audio.sub_y2 = 0;
  rtl_audio.pilot_env = 0;
  rtl_audio.stereo_locked = false;
  rtl_audio.channel_filter_diff = 0;
  rtl_audio.audio_sum_diff = 0;
  rtl_audio.deemphasis_l = 0;
  rtl_audio.deemphasis_r = 0;
  rtl_audio.dc_l = 0;
  rtl_audio.dc_r = 0;
  rtl_rds_reset();
}
int16_t rtl_audio_buffers[3][kRtlAudioBufferSamples];
/* M5Unified queues pointers; never overwrite a block until its audio has played. */
static int16_t rtl_audio_play_blocks[kRtlAudioPlayBlockCount][kRtlAudioPlayBlockSamples];
static size_t rtl_audio_play_count = 0;
static size_t rtl_audio_play_block_index = 0;
static uint32_t rtl_audio_play_block_ready_ms[kRtlAudioPlayBlockCount]{};
static std::atomic<uint32_t> rtl_audio_ring_overruns{0};
static std::atomic<uint32_t> rtl_audio_submit_failures{0};
static std::atomic<bool> rtl_audio_test_tone{false};
static std::atomic<bool> rtl_audio_test_metrics{false};
static uint32_t rtl_audio_test_started_ms = 0;
static uint32_t rtl_audio_test_last_report_ms = 0;
static UBaseType_t rtl_audio_test_task_baseline = 0;
// Dual-core IQ ring (prefer PSRAM). USB produces, DSP consumes.
struct RtlIqBlock {
  uint8_t* data;
  size_t bytes;
  uint32_t sequence;
  bool end_marker;
};
static uint8_t* rtl_ring_slots[kRtlRingDepth]{};
static QueueHandle_t rtl_free_q = nullptr;
static QueueHandle_t rtl_filled_q = nullptr;
/* Scratch IQ buffer for demod/spectrum (size >= max bulk transfer). */
static uint8_t rtl_iq_processing[32768 + 512];
/* Relative RF level from IQ power (dBFS-ish, 0 = full-scale CU8). */
static std::atomic<float> rtl_signal_dbfs{-90.0f};
/** WBFM stereo: fresh-per-callback dBFS (same granularity as rtl_signal_dbfs);
 * UI applies its own EMA at redraw cadence, matching draw_signal_meter. */
static std::atomic<float> rtl_audio_left_dbfs{-90.0f};
static std::atomic<float> rtl_audio_right_dbfs{-90.0f};
static std::atomic<bool> rtl_stereo_locked{false};
// RDS Stage 1 carrier presence and Stage 2 block sync telemetry.
static std::atomic<float> rtl_rds_signal_dbfs{-90.0f};
static std::atomic<bool> rtl_rds_carrier_present{false};
// RDS Stage 2: block-sync status, whichever chip-alignment hypothesis is
// currently locked (or -1 if neither is). BLER stored as good/total counts
// rather than a pre-divided percentage so the UI can decide its own window.
static std::atomic<bool> rtl_rds_block_locked{false};
static std::atomic<uint32_t> rtl_rds_good_blocks{0};
static std::atomic<uint32_t> rtl_rds_total_blocks{0};

// FM presets: populated by an explicit band scan (not auto on entry). Top 10
// by signal strength, deduped by proximity so one strong station does not
// occupy multiple slots.
constexpr int kFmPresetCapacity = 10;
struct FmPreset {
  uint32_t freq_hz = 0;
  float level_dbfs = -120.0f;
};
static FmPreset fm_presets[kFmPresetCapacity];
static int fm_preset_count = 0;
std::atomic<bool> fm_config_save_pending{false};
static int fm_preset_scroll_top = 0;
static std::atomic<bool> rtl_fm_preset_scan_requested{false};
static std::atomic<bool> rtl_fm_preset_scan_cancel{false};
static std::atomic<bool> rtl_fm_preset_scan_active{false};
static std::atomic<int> rtl_fm_preset_scan_step{0};
static std::atomic<int> rtl_fm_preset_scan_total_steps{1};
static std::atomic<int> rtl_fm_preset_scan_found{0};
static std::atomic<uint32_t> rtl_fm_preset_scan_freq_hz{kRtlFmMinHz};

// Runs on the streaming task only (single-threaded access to fm_presets).
void fm_preset_offer(uint32_t freq_hz, float level_dbfs) {
  // Merge into an existing nearby entry (same station found by adjacent steps)
  // instead of adding a duplicate slot.
  for (int i = 0; i < fm_preset_count; ++i) {
    const uint32_t delta = freq_hz > fm_presets[i].freq_hz
                                ? freq_hz - fm_presets[i].freq_hz
                                : fm_presets[i].freq_hz - freq_hz;
    if (delta <= 300000u) {
      if (level_dbfs > fm_presets[i].level_dbfs) {
        fm_presets[i].freq_hz = freq_hz;
        fm_presets[i].level_dbfs = level_dbfs;
      }
      return;
    }
  }
  if (fm_preset_count < kFmPresetCapacity) {
    fm_presets[fm_preset_count].freq_hz = freq_hz;
    fm_presets[fm_preset_count].level_dbfs = level_dbfs;
    ++fm_preset_count;
  } else {
    // Replace the weakest slot if this station is stronger.
    int weakest = 0;
    for (int i = 1; i < kFmPresetCapacity; ++i) {
      if (fm_presets[i].level_dbfs < fm_presets[weakest].level_dbfs) weakest = i;
    }
    if (level_dbfs > fm_presets[weakest].level_dbfs) {
      fm_presets[weakest].freq_hz = freq_hz;
      fm_presets[weakest].level_dbfs = level_dbfs;
    }
  }
  // Keep sorted low-to-high frequency for a stable, scannable list.
  for (int i = 1; i < fm_preset_count; ++i) {
    FmPreset key = fm_presets[i];
    int j = i - 1;
    while (j >= 0 && fm_presets[j].freq_hz > key.freq_hz) {
      fm_presets[j + 1] = fm_presets[j];
      --j;
    }
    fm_presets[j + 1] = key;
  }
}
static std::atomic<CbMode> cb_mode{CbMode::am};
static std::atomic<int32_t> cb_clarifier_hz{0};
static std::atomic<int32_t> cb_squelch_dbfs{-75};
static std::atomic<bool> cb_squelch_open{false};
static std::atomic<uint8_t> lora_sf{11};
static std::atomic<uint32_t> lora_bandwidth_hz{250000};
static std::atomic<bool> lora_detector_enabled{true};
static std::atomic<uint32_t> lora_rf_events{0};
static std::atomic<float> lora_noise_dbfs{-90.0f};
static std::atomic<float> lora_trigger_dbfs{-75.0f};
static std::atomic<uint32_t> lora_messages{0};
static portMUX_TYPE lora_message_mux = portMUX_INITIALIZER_UNLOCKED;
struct LoraDisplayPacket {
  char text[112]{};
  uint32_t sender = 0;
  uint32_t destination = 0;
  uint32_t packet_id = 0;
  uint32_t received_ms = 0;
  int32_t latitude_e7 = INT32_MAX;
  int32_t longitude_e7 = INT32_MAX;
  int16_t snr_tenths = INT16_MAX;
  int16_t signal_tenths = INT16_MAX;
  uint16_t port = 0;
};
constexpr size_t kLoraDisplayPacketCount = 8;
static LoraDisplayPacket lora_display_packets[kLoraDisplayPacketCount]{};
struct LoraLogRecord {
  LoraDisplayPacket packet{};
  uint32_t frequency_hz = 0;
};
constexpr size_t kLoraLogQueueDepth = 32;
constexpr char kLoraLogPath[] = "/orcsdr/lora_packets.csv";
static QueueHandle_t lora_log_queue = nullptr;
static TaskHandle_t lora_log_task_handle = nullptr;
static std::atomic<bool> lora_log_requested{false};
static std::atomic<bool> lora_log_ready{false};
static std::atomic<bool> lora_log_error{false};
static std::atomic<uint32_t> lora_log_dropped{0};
static std::atomic<uint32_t> lora_log_last_packet_ms{0};
struct LoraNodePosition {
  uint32_t node = 0;
  uint32_t received_ms = 0;
  int32_t latitude_e7 = 0;
  int32_t longitude_e7 = 0;
};
constexpr size_t kLoraNodePositionCount = 8;
static LoraNodePosition lora_node_positions[kLoraNodePositionCount]{};
enum class LoraView : uint8_t { Live = 0, Packets = 1, Map = 2, Count = 3 };
static LoraView lora_view = LoraView::Live;
static std::atomic<int32_t> rtl_scope_peak_offset_hz{0};
static std::atomic<float> rtl_scope_peak_level{-120.0f};
static std::atomic<bool> rtl_auto_fm_requested{false};
static std::atomic<bool> rtl_auto_fm_active{false};
static float rtl_signal_dbfs_smooth = -80.0f;
static uint32_t rtl_signal_meter_last_ms = 0;
static TaskHandle_t rtl_dsp_task_handle = nullptr;
static std::atomic<uint32_t> rtl_usb_overruns{0};
static std::atomic<uint32_t> rtl_dsp_blocks{0};
static std::atomic<uint32_t> rtl_dsp_window_us{0};
static std::atomic<uint32_t> rtl_dsp_window_blocks{0};
static std::atomic<uint32_t> rtl_dsp_block_us_max{0};
static std::atomic<bool> rtl_session_active{false};
static RtlBand rtl_session_band = RtlBand::fm;
static float rtl_session_audio_scale = 5500.0f;
static bool rtl_session_continuous = true;
static uint32_t rtl_session_started_ms = 0;
static std::atomic<uint32_t> rtl_session_frequency_hz{kRtlFmDefaultHz};
float rtl_spectrum_real[kRtlSpectrumBins];
float rtl_spectrum_imaginary[kRtlSpectrumBins];
float rtl_spectrum_levels[kRtlSpectrumBins];
float rtl_spectrum_smooth[kRtlSpectrumBins];
float rtl_spectrum_peak[kRtlSpectrumBins];
float rtl_spectrum_window[kRtlSpectrumBins];
int16_t rtl_spectrum_y[kRtlSpectrumBins];
int16_t rtl_spectrum_peak_y[kRtlSpectrumBins];
uint16_t rtl_waterfall_row[kSpectrumWidth];
bool rtl_spectrum_window_ready = false;
bool rtl_spectrum_trace_valid = false;
uint32_t rtl_spectrum_last_ms = 0;
uint32_t rtl_spectrum_trace_last_ms = 0;
uint32_t rtl_spectrum_frames = 0;
uint32_t rtl_spectrum_fps_window_ms = 0;
uint16_t rtl_spectrum_fps = 0;
/** IQ snapshot for scope only (never taken from the live demod buffer mid-write). */
static uint8_t rtl_spectrum_iq_snap[kRtlSpectrumBins * 2 * kRtlSpectrumWelchWindows];
static size_t rtl_spectrum_iq_snap_bytes = 0;
static portMUX_TYPE rtl_spectrum_snap_mux = portMUX_INITIALIZER_UNLOCKED;

/* ---- Post-demod audio recorder (PSRAM ring → optional SD WAV) ---- */
static int16_t* g_audio_rec_buf = nullptr;
static size_t g_audio_rec_capacity = 0;
static std::atomic<size_t> g_audio_rec_write{0};
static std::atomic<bool> g_audio_rec_active{false};
static std::atomic<bool> g_audio_rec_full{false};
static uint32_t g_audio_rec_freq_hz = 0;
static RtlBand g_audio_rec_band = RtlBand::fm;
static uint32_t g_audio_rec_file_seq = 0;
static bool g_sd_ready = false;
static fs::FS* g_sd_fs = nullptr;
static bool g_sd_tried = false;
static char g_audio_rec_last_path[64] = "";
static std::atomic<uint8_t> g_orc_tool{static_cast<uint8_t>(OrcTool::Radio)};
static std::atomic<bool> g_audio_rec_export_pending{false};

/* ---- FM multiplex capture for repeatable RDS replay ---- */
static int16_t* g_rds_capture_buf = nullptr;
static std::atomic<size_t> g_rds_capture_write{0};
static std::atomic<bool> g_rds_capture_active{false};
static std::atomic<bool> g_rds_capture_ready{false};
static std::atomic<bool> g_rds_capture_writing{false};
static uint32_t g_rds_capture_frequency_hz = 0;
static uint32_t g_rds_capture_started_ms = 0;
static uint32_t g_rds_capture_file_seq = 0;
static char g_rds_capture_last_path[96] = "";

/* ---- Raw CU8 IQ capture and adaptive LoRa energy trigger ---- */
constexpr size_t kIqRecSeconds = 3;
constexpr size_t kIqRecMaxBytes = kRtlSampleRateSps * 2u * kIqRecSeconds;
constexpr size_t kLoraPreRollBytes = kRtlSampleRateSps / 2u;  // 250 ms CU8 IQ
constexpr float kLoraTriggerMarginDb = 9.0f;
constexpr float kLoraTriggerHysteresisDb = 3.0f;
static uint8_t* g_iq_rec_buf = nullptr;
static uint8_t* g_lora_pre_roll_buf = nullptr;
static std::atomic<size_t> g_iq_rec_write{0};
static std::atomic<bool> g_iq_rec_active{false};
static std::atomic<bool> g_iq_rec_ready{false};
static std::atomic<bool> g_iq_rec_export_pending{false};
static std::atomic<bool> g_iq_rec_export_busy{false};
static std::atomic<bool> g_iq_rec_auto_triggered{false};
static std::atomic<bool> g_iq_retrieve_resume{false};
static uint32_t g_iq_rec_frequency_hz = 0;
static uint8_t g_iq_rec_sf = 11;
static uint32_t g_iq_rec_bandwidth_hz = 250000;
static uint32_t g_iq_rec_file_seq = 0;
static char g_iq_rec_last_path[96] = "";
static size_t g_lora_pre_roll_write = 0;
static size_t g_lora_pre_roll_fill = 0;
static float g_lora_noise_floor_dbfs = -90.0f;
static uint16_t g_lora_noise_samples = 0;
static bool g_lora_trigger_armed = false;

enum class RtlCaptureState : uint8_t {
  disconnected,
  ready,
  queued,
  running,
  complete,
  failed,
};

struct JournalEntry {
  uint32_t sequence;
  char kind[20];
  int16_t x;
  int16_t y;
};

struct JournalState {
  uint32_t magic;
  uint32_t next_sequence;
  uint32_t acknowledged_sequence;
  uint8_t head;
  uint8_t count;
  uint32_t dropped_events;
  JournalEntry entries[kJournalCapacity];
};

struct WorkflowState {
  uint32_t magic;
  uint32_t config_revision;
  uint16_t max_runs;
  uint16_t runs;
};

char node_id[32];
uint64_t heartbeat_sequence = 0;
uint32_t last_heartbeat_ms = 0;
uint32_t last_ping_ms = 0;
uint32_t last_power_draw_ms = 0;
bool was_pressed = false;
bool paired = false;
bool authenticated = false;
bool ui_documentation_mode = false;
bool offline_transition_handled = false;
Preferences preferences;
orcsdr::adsb::Settings adsb_settings;
std::atomic<bool> adsb_settings_persist_pending{false};
JournalState journal{};
WorkflowState workflow{};
uint8_t pairing_key[32];
char serial_input[384];
size_t serial_input_length = 0;
constexpr size_t kSdPutChunkBytes = 16 * 1024;
// USB Serial/JTAG has a 4 KiB TX queue; smaller reads avoid producer deadlock.
constexpr size_t kSdGetChunkBytes = 2 * 1024;
constexpr uint64_t kSdPutMaxBytes = 64ULL * 1024ULL * 1024ULL;
struct SdPutState {
  File file;
  bool active = false;
  uint64_t expected = 0;
  uint64_t received = 0;
  char target[128]{};
  char temporary[136]{};
  uint8_t expected_sha[32]{};
  mbedtls_sha256_context sha;
};
SdPutState g_sd_put;
struct SdGetState {
  File file;
  bool active = false;
  uint64_t size = 0;
  uint64_t sent = 0;
  char path[128]{};
  mbedtls_sha256_context sha;
};
SdGetState g_sd_get;
static std::atomic<bool> g_sd_transfer_active{false};
struct IqGetState {
  bool active = false;
  size_t sent = 0;
  mbedtls_sha256_context sha;
};
IqGetState g_iq_get;
uint8_t g_sd_put_chunk[kSdPutChunkBytes];
bool wifi_station_ready = false;
bool wifi_hosted_versions_match = false;
bool wifi_scan_running = false;
std::atomic<bool> wifi_scan_requested{false};
std::atomic<bool> wifi_connect_requested{false};
bool wifi_configured = false;
bool settings_wifi_power_enabled = true;
bool settings_wifi_external_antenna = false;
bool wifi_connected = false;
bool wifi_connecting = false;
bool wifi_save_after_connect = false;
uint32_t wifi_connect_started_ms = 0;
int wifi_network_count = -1;
char wifi_ssid[33]{};
char wifi_password[64]{};
char wifi_status_message[48]{};
struct WifiProfile {
  char ssid[33]{};
  char password[64]{};
};
EXT_RAM_BSS_ATTR WifiProfile wifi_profiles[4]{};
uint8_t wifi_profile_count = 0;
struct WifiScanResult {
  char ssid[33]{};
  int16_t rssi = 0;
  bool secure = false;
};
EXT_RAM_BSS_ATTR WifiScanResult wifi_scan_results[6]{};
uint8_t wifi_scan_result_count = 0;
uint32_t wifi_scan_started_ms = 0;
uint8_t settings_brightness = 180;
uint8_t settings_rotation = 1;
uint16_t settings_screen_timeout_sec = 0;
bool settings_sound_default = true;
bool settings_auto_start_reception = true;
bool settings_graphics_default = true;
char settings_location_label[40]{};
char settings_map_pack[40]{};
bool settings_restore_graphics = true;
bool settings_restore_home = false;
std::atomic<uint32_t> rtl_sdr_status_revision{0};
uint32_t drawn_rtl_sdr_status_revision = 0;
char rtl_sdr_status[96] = "RTL-SDR: waiting for USB-A host";
char rtl_sdr_serial[48]{};
char rtl_sdr_speed[8] = "none";
uint16_t rtl_sdr_vid = 0;
uint16_t rtl_sdr_pid = 0;
uint8_t pending_usb_address = 0;
#if RTL_USE_LEGACY_USB
usb_host_client_handle_t usb_client = nullptr;
usb_device_handle_t rtl_sdr_device = nullptr;
bool rtl_sdr_gone = false;
static inline bool rtl_device_ready() { return rtl_sdr_device != nullptr; }
#else
rtl_sdr_v4_esp_handle_t g_rtl = nullptr;
std::atomic<bool> g_rtl_device_ready{false};
static float g_stream_audio_scale = 5500.0f;
static RtlBand g_stream_band = RtlBand::fm;
static inline bool rtl_device_ready() {
  return g_rtl_device_ready.load(std::memory_order_acquire) && g_rtl != nullptr;
}
#endif
std::atomic<RtlCaptureState> rtl_capture_state{RtlCaptureState::disconnected};
std::atomic<bool> rtl_capture_requested{false};
std::atomic<RtlBand> rtl_requested_band{RtlBand::fm};
std::atomic<uint32_t> rtl_requested_frequency_hz{kRtlFmDefaultHz};
// Non-zero = apply PLL retune without tearing down the IQ stream (fluid scroll).
std::atomic<uint32_t> rtl_hot_retune_hz{0};
std::atomic<uint8_t> rtl_requested_volume{kRtlVolumeDefault};
std::atomic<uint8_t> rtl_live_volume{kRtlVolumeDefault};
std::atomic<bool> rtl_audio_user_enabled{true};
std::atomic<bool> rtl_audio_enabled{false};
std::atomic<bool> rtl_speaker_start_allowed{false};
enum class BootInitStage : uint8_t {
  idle,
  usb_power_off,
  usb_power_settle,
  rtl_enumerating,
  speaker_settle,
  ready,
};
BootInitStage boot_init_stage = BootInitStage::idle;
uint32_t boot_init_stage_started_ms = 0;
bool boot_auto_start_allowed = false;
uint32_t power_monitor_until_ms = 0;
uint32_t power_monitor_next_ms = 0;
char power_monitor_tag[24]{};
std::atomic<bool> rtl_volume_changed{false};
std::atomic<uint32_t> rtl_audio_settings_persist_due_ms{0};
/** When false: no scope/waterfall updates (audio + SIG meter still run). A/B for chop diagnosis. */
std::atomic<bool> rtl_graphics_enabled{true};
enum class SdrPinchMode : uint8_t { Span, Filter };
enum class SdrNavDropdown : uint8_t { None, Band, Pinch, Step };
std::atomic<uint32_t> rtl_scope_span_hz{kRtlScopeSpanMaxHz};
std::atomic<uint32_t> rtl_filter_bandwidth_hz{kRtlFmFilterDefaultHz};
SdrPinchMode rtl_pinch_mode = SdrPinchMode::Span;
SdrNavDropdown rtl_nav_dropdown = SdrNavDropdown::None;
bool rtl_nav_open = false;
bool rtl_frequency_keypad_open = false;
uint32_t rtl_fm_step_hz = kRtlFmStepHz;
uint32_t rtl_am_step_hz = kRtlAmStepHz;
char rtl_frequency_entry[16]{};
std::atomic<bool> rtl_continuous_requested{false};
std::atomic<bool> rtl_stop_requested{false};
std::atomic<bool> rtl_restart_requested{false};
std::atomic<bool> rtl_ui_active{false};
std::atomic<bool> usb_transfer_done{false};
std::atomic<uint32_t> rtl_ui_revision{0};
uint32_t drawn_rtl_ui_revision = 0;
RtlBand rtl_ui_band = RtlBand::fm;
uint32_t rtl_ui_frequency_hz = kRtlFmDefaultHz;
// Last good FM LO; seeded from NVS (or kRtlFmDefaultHz) and rewritten on retune.
uint32_t rtl_saved_fm_hz = kRtlFmDefaultHz;
uint8_t rtl_ui_volume = kRtlVolumeDefault;
std::atomic<bool> p25_survey_active{false};
std::atomic<bool> p25_hold{false};
std::atomic<bool> p25_auto_follow{true};
std::atomic<bool> p25_encryption_skip{true};
enum class P25FollowState : uint8_t { control, voice };
std::atomic<P25FollowState> p25_follow_state{P25FollowState::control};
uint32_t p25_control_frequency_hz = kP25DefaultHz;
orcsdr::p25config::Config p25_config{};
char p25_config_status[64] = "SD profile pending";
uint32_t p25_config_revision = 0;
std::atomic<bool> p25_config_save_pending{false};
uint32_t p25_voice_frequency_hz = 0;
uint32_t p25_follow_started_ms = 0;
uint16_t p25_hold_talkgroup = 0;
uint16_t p25_skipped_talkgroup = 0;
uint32_t p25_skip_until_ms = 0;
orcsdr::p25decoder::Grant p25_follow_grant{};
std::atomic<uint32_t> p25_imbe_frames{0};
std::atomic<uint32_t> p25_imbe_errors{0};
std::atomic<uint32_t> p25_pcm_frames{0};
std::atomic<uint32_t> p25_audio_last_ms{0};
std::atomic<uint32_t> p25_voice_session{0};
std::atomic<uint32_t> p25_voice_stack_hwm{0};
std::atomic<uint32_t> p25_imbe_max_us{0};
std::atomic<uint32_t> p25_imbe_synth_max_us{0};
std::atomic<uint32_t> p25_audio_queue_max_us{0};
struct P25VoiceContext {
  mbe_parms current{};
  mbe_parms previous{};
  mbe_parms enhanced{};
  int16_t pcm8k[160]{};
  int16_t pcm48k[960]{};
  char matrix[8][23]{};
  char decoded[88]{};
  char error_text[16]{};
  int16_t previous_sample = 0;
};
P25VoiceContext p25_voice_context{};
uint8_t p25_candidate_index = 0;
float p25_candidate_levels[orcsdr::p25config::kMaxControlChannels] = {};
uint32_t p25_candidate_tsbk_good[orcsdr::p25config::kMaxControlChannels]{};
uint32_t p25_survey_sample_at_ms = 0;
uint32_t p25_entry_probe_at_ms = 0;
uint32_t p25_entry_probe_tsbk_good = 0;
uint64_t rtl_capture_bytes = 0;
uint8_t rtl_capture_min = 0;
uint8_t rtl_capture_max = 0;
double rtl_capture_mean = 0;
char rtl_capture_sha256[65]{};
char rtl_capture_error[64] = "not run";
orcsdr::adsb_rx::Decoder adsb_decoder;
constexpr size_t kAdsbIqBlockBytes = 32768;
constexpr uint8_t kAdsbIqBlockCount = 4;
uint8_t* adsb_iq_blocks[kAdsbIqBlockCount]{};
size_t adsb_iq_sizes[kAdsbIqBlockCount]{};
QueueHandle_t adsb_iq_free = nullptr;
QueueHandle_t adsb_iq_ready = nullptr;
std::atomic<uint32_t> adsb_iq_drops{0};

struct AdsbTrack {
  bool used = false;
  uint32_t icao = 0;
  char callsign[9]{};
  bool has_callsign = false;
  int altitude_ft = 0;
  bool has_altitude = false;
  int speed_kts = 0;
  bool has_speed = false;
  int heading_deg = 0;
  bool has_heading = false;
  int vertical_rate_fpm = 0;
  bool has_vertical_rate = false;
  double latitude = 0;
  double longitude = 0;
  bool has_position = false;
  orcsdr::adsb_rx::Frame even_cpr{};
  orcsdr::adsb_rx::Frame odd_cpr{};
  uint32_t even_cpr_ms = 0;
  uint32_t odd_cpr_ms = 0;
  uint32_t last_seen_ms = 0;
  uint16_t signal = 0;
  bool metadata_pending = false;
  char registration[9]{};
  char type[49]{};
  char owner[51]{};
};

constexpr size_t kAdsbTrackCount = 64;
AdsbTrack adsb_tracks[kAdsbTrackCount]{};
portMUX_TYPE adsb_tracks_mux = portMUX_INITIALIZER_UNLOCKED;
std::atomic<uint32_t> adsb_track_revision{0};
std::atomic<uint32_t> adsb_total_messages{0};
std::atomic<uint8_t> adsb_aircraft_count{0};

void reset_adsb_tracks() {
  portENTER_CRITICAL(&adsb_tracks_mux);
  for (auto& track : adsb_tracks) track = {};
  portEXIT_CRITICAL(&adsb_tracks_mux);
  adsb_track_revision.store(0, std::memory_order_relaxed);
  adsb_total_messages.store(0, std::memory_order_relaxed);
  adsb_aircraft_count.store(0, std::memory_order_relaxed);
}

void expire_adsb_tracks(uint32_t now) {
  uint8_t count = 0;
  bool changed = false;
  portENTER_CRITICAL(&adsb_tracks_mux);
  for (auto& track : adsb_tracks) {
    if (track.used && now - track.last_seen_ms > 60000u) {
      track = {};
      changed = true;
    }
    if (track.used) ++count;
  }
  portEXIT_CRITICAL(&adsb_tracks_mux);
  adsb_aircraft_count.store(count, std::memory_order_relaxed);
  if (changed) adsb_track_revision.fetch_add(1, std::memory_order_release);
}

void publish_adsb_snapshot(uint32_t now) {
  static uint32_t last_sample_ms = 0;
  static uint32_t last_sample_messages = 0;
  static uint32_t last_state_revision = UINT32_MAX;
  static float published_rate = -1;
  static uint32_t ui_revision = 0;
  if (now - last_sample_ms < 1000u) return;

  const uint32_t messages = adsb_total_messages.load(std::memory_order_relaxed);
  const uint32_t state_revision = adsb_track_revision.load(std::memory_order_acquire);
  const float message_rate = (messages - last_sample_messages) * 1000.0f /
                             static_cast<float>(now - last_sample_ms);
  last_sample_messages = messages;
  last_sample_ms = now;
  if (state_revision == last_state_revision && fabsf(message_rate - published_rate) < 0.05f)
    return;

  orcsdr::adsb::Snapshot snapshot{};
  portENTER_CRITICAL(&adsb_tracks_mux);
  for (const auto& track : adsb_tracks) {
    if (!track.used || snapshot.visible_count == orcsdr::adsb::kVisibleAircraft) continue;
    auto& aircraft = snapshot.aircraft[snapshot.visible_count++];
    aircraft.icao = track.icao;
    strlcpy(aircraft.callsign, track.callsign, sizeof(aircraft.callsign));
    strlcpy(aircraft.registration, track.registration, sizeof(aircraft.registration));
    strlcpy(aircraft.type, track.type, sizeof(aircraft.type));
    strlcpy(aircraft.owner, track.owner, sizeof(aircraft.owner));
    aircraft.altitude_ft = track.altitude_ft;
    aircraft.speed_kts = track.speed_kts;
    aircraft.heading_deg = track.heading_deg;
    aircraft.vertical_rate_fpm = track.vertical_rate_fpm;
    aircraft.latitude = static_cast<float>(track.latitude);
    aircraft.longitude = static_cast<float>(track.longitude);
    aircraft.has_callsign = track.has_callsign;
    aircraft.has_altitude = track.has_altitude;
    aircraft.has_speed = track.has_speed;
    aircraft.has_heading = track.has_heading;
    aircraft.has_vertical_rate = track.has_vertical_rate;
    aircraft.has_position = track.has_position;
  }
  portEXIT_CRITICAL(&adsb_tracks_mux);

  snapshot.total_messages = messages;
  snapshot.aircraft_count = adsb_aircraft_count.load(std::memory_order_relaxed);
  snapshot.message_rate = message_rate;
  snapshot.strongest_signal_dbfs = rtl_signal_dbfs.load(std::memory_order_relaxed);
  snapshot.revision = ++ui_revision;
  last_state_revision = state_revision;
  published_rate = message_rate;
  orcsdr::adsb::set_live_snapshot(snapshot);
}

void on_adsb_frame(const orcsdr::adsb_rx::Frame& frame, void*) {
  const uint32_t now = millis();
  bool added = false;
  bool decode_position = false;
  orcsdr::adsb_rx::Frame even_cpr{}, odd_cpr{};
  bool use_odd = false;
  portENTER_CRITICAL(&adsb_tracks_mux);
  AdsbTrack* track = nullptr;
  AdsbTrack* oldest = &adsb_tracks[0];
  for (auto& candidate : adsb_tracks) {
    if (candidate.used && candidate.icao == frame.icao) {
      track = &candidate;
      break;
    }
    if (!candidate.used) oldest = &candidate;
    else if (oldest->used && candidate.last_seen_ms < oldest->last_seen_ms) oldest = &candidate;
  }
  if (!track) {
    track = oldest;
    added = !track->used;
    *track = {};
    track->used = true;
    track->icao = frame.icao;
    track->metadata_pending = true;
  }
  track->last_seen_ms = now;
  track->signal = frame.signal;
  if (frame.has_callsign) {
    strlcpy(track->callsign, frame.callsign, sizeof(track->callsign));
    track->has_callsign = true;
  }
  if (frame.has_altitude) {
    track->altitude_ft = frame.altitude_ft;
    track->has_altitude = true;
  }
  if (frame.has_speed) {
    track->speed_kts = frame.speed_kts;
    track->has_speed = true;
  }
  if (frame.has_heading) {
    track->heading_deg = frame.heading_deg;
    track->has_heading = true;
  }
  if (frame.has_vertical_rate) {
    track->vertical_rate_fpm = frame.vertical_rate_fpm;
    track->has_vertical_rate = true;
  }
  if (frame.has_cpr) {
    if (frame.cpr_odd) {
      track->odd_cpr = frame;
      track->odd_cpr_ms = now;
    } else {
      track->even_cpr = frame;
      track->even_cpr_ms = now;
    }
    const uint32_t pair_age = track->even_cpr_ms > track->odd_cpr_ms
                                  ? track->even_cpr_ms - track->odd_cpr_ms
                                  : track->odd_cpr_ms - track->even_cpr_ms;
    if (track->even_cpr_ms && track->odd_cpr_ms && pair_age <= 10000u) {
      even_cpr = track->even_cpr;
      odd_cpr = track->odd_cpr;
      use_odd = track->odd_cpr_ms > track->even_cpr_ms;
      decode_position = true;
    }
  }
  portEXIT_CRITICAL(&adsb_tracks_mux);
  if (decode_position) {
    double latitude = 0, longitude = 0;
    if (orcsdr::adsb_rx::decode_global_cpr(even_cpr, odd_cpr, use_odd,
                                           &latitude, &longitude)) {
      portENTER_CRITICAL(&adsb_tracks_mux);
      if (track->used && track->icao == frame.icao) {
        track->latitude = latitude;
        track->longitude = longitude;
        track->has_position = true;
      }
      portEXIT_CRITICAL(&adsb_tracks_mux);
    }
  }
  if (added) adsb_aircraft_count.fetch_add(1, std::memory_order_relaxed);
  adsb_total_messages.fetch_add(1, std::memory_order_relaxed);
  adsb_track_revision.fetch_add(1, std::memory_order_release);

  static uint32_t last_log_ms = 0;
  if (now - last_log_ms < 250) return;
  last_log_ms = now;
  char raw[29]{};
  for (size_t i = 0; i < frame.bit_length / 8; ++i)
    snprintf(raw + i * 2, sizeof(raw) - i * 2, "%02X", frame.bytes[i]);
  Serial.printf("RTL_ADSB_FRAME raw=%s icao=%06lX tc=%u callsign=%s "
                "altitude_ft=%d altitude_valid=%d speed_kts=%d speed_valid=%d "
                "heading_deg=%d heading_valid=%d vertical_fpm=%d vertical_valid=%d signal=%u\n",
                raw, static_cast<unsigned long>(frame.icao), frame.type_code,
                frame.has_callsign ? frame.callsign : "-", frame.altitude_ft,
                frame.has_altitude ? 1 : 0, frame.speed_kts, frame.has_speed ? 1 : 0,
                frame.heading_deg, frame.has_heading ? 1 : 0, frame.vertical_rate_fpm,
                frame.has_vertical_rate ? 1 : 0, frame.signal);
}

void adsb_decoder_task(void*) {
  uint8_t index = 0;
  while (true) {
    if (xQueueReceive(adsb_iq_ready, &index, portMAX_DELAY) == pdTRUE) {
      adsb_decoder.process_cu8(adsb_iq_blocks[index], adsb_iq_sizes[index], on_adsb_frame,
                              nullptr);
      (void)xQueueSend(adsb_iq_free, &index, portMAX_DELAY);
    }
  }
}

void emit_identity();
bool decode_hex(const char* value, uint8_t* output, size_t output_size);
bool decode_hex_text(const char* value, char* output, size_t output_size);
void print_hex(const uint8_t* value, size_t size);
void reset_spectrum_renderer();
void draw_spectrum_grid();
void draw_spectrum_axis();
void draw_band_edges();
void draw_cb_dashboard(bool static_panel);
bool handle_cb_touch(int32_t x, int32_t y);
void draw_lora_dashboard(bool static_panel);
void draw_fm_dashboard(bool static_panel);
void draw_p25_dashboard(bool static_panel);
void draw_lora_packets_view(bool force);
void draw_lora_map_view(bool force);
bool handle_lora_touch(int32_t x, int32_t y);
void draw_rf_band_guide(uint32_t frequency_hz);
int spectrum_draw_width();
void redraw_spectrum_panel();
void draw_sdr_controls(RtlBand band, bool running);
void handle_sdr_touch(int32_t x, int32_t y);
void poll_sdr_touch_from_stream();
void request_hot_retune(uint32_t frequency_hz);
void queue_local_rtl_listen(RtlBand band, uint32_t frequency_hz,
                            bool persist_navigation = true);
void draw_sdr_screen(RtlBand band, uint32_t frequency_hz, uint8_t volume);
void adjust_rtl_volume(int delta);
void draw_nav_panel();
bool handle_nav_touch(int32_t x, int32_t y);
void spectrum_offer_iq_snapshot(const uint8_t* iq, size_t bytes);
bool audio_rec_ensure_buffer();
bool audio_rec_start();
bool audio_rec_stop_and_export();
void audio_rec_append(const int16_t* samples, size_t count);
void queue_audio_samples(int16_t* audio, size_t audio_count);
void audio_rec_status_print();
void rds_process_mpx_sample(float phase);
void rds_publish_state();
bool rds_capture_start();
bool rds_capture_stop_and_export();
bool rds_replay(const char* path);
void rds_capture_status_print();
bool ensure_tab5_sd();
uint64_t sd_total_bytes();
void enrich_one_adsb_track();
bool sd_put_path_allowed(const char* path);
void bump_rtl_ui();
const char* orc_tool_name(OrcTool tool);
OrcTool orc_tool_current();
void set_orc_tool(OrcTool tool);
void draw_tool_tabs();
void draw_capture_tool_panel();
bool handle_tool_tab_touch(int32_t x, int32_t y);
orcsdr::settings::State global_settings_state();
orcsdr::home::Snapshot home_dashboard_snapshot(bool demo = false);
void show_home(bool demo = false);
void handle_home_action(const orcsdr::home::Action& action);
void open_global_settings(orcsdr::settings::Section section);
void handle_global_settings_touch(int32_t x, int32_t y);
void draw_global_settings_gear();
orcsdr::fm::Snapshot fm_dashboard_snapshot();
void handle_fm_dashboard_action(const orcsdr::fm::Action& action);
orcsdr::p25::Snapshot p25_dashboard_snapshot();
void handle_p25_dashboard_action(const orcsdr::p25::Action& action);
void service_p25_survey(uint32_t now);
void service_p25_follow(uint32_t now);
bool p25_voice_self_check();
void start_wifi_inventory();
void stop_wifi();
void begin_power_monitor(const char* tag, uint32_t duration_ms = 1000);
void service_power_monitor();

void draw_touch_state(const char* message, uint32_t color) {
  M5.Display.fillRect(300, 480, 680, 70, TFT_BLACK);
  M5.Display.setTextColor(color, TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(2);
  M5.Display.drawString(message, 640, 515);
}

/** True while loading splash owns the display — home chrome must not paint. */
static bool g_suppress_home_paint = false;

void draw_session_state(const char* message, uint32_t color) {
  if (g_suppress_home_paint || orcsdr::settings::active() || orcsdr::home::active()) return;
  M5.Display.fillRect(250, 210, 780, 55, TFT_BLACK);
  M5.Display.setTextColor(color, TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(2);
  M5.Display.drawString(message, 640, 237);
}

void draw_wifi_state() {
  if (g_suppress_home_paint || orcsdr::settings::active() || orcsdr::home::active()) return;
  char message[80];
  uint32_t color = TFT_ORANGE;
  if (!wifi_station_ready) {
    snprintf(message, sizeof(message), "Wi-Fi unavailable");
  } else if (wifi_connected) {
    snprintf(message, sizeof(message), "Wi-Fi connected: %s",
             WiFi.localIP().toString().c_str());
    color = TFT_GREEN;
  } else if (wifi_configured) {
    snprintf(message, sizeof(message), "Wi-Fi connecting");
    color = TFT_YELLOW;
  } else if (wifi_scan_running) {
    snprintf(message, sizeof(message), "Wi-Fi inventory scanning");
  } else if (wifi_network_count < 0) {
    snprintf(message, sizeof(message), "Wi-Fi inventory failed (%d)", wifi_network_count);
  } else {
    snprintf(message, sizeof(message), "Wi-Fi inventory: %d network%s", wifi_network_count,
             wifi_network_count == 1 ? "" : "s");
    color = TFT_CYAN;
  }
  M5.Display.fillRect(250, 590, 780, 55, TFT_BLACK);
  M5.Display.setTextColor(color, TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(2);
  M5.Display.drawString(message, 640, 617);
}

const char* charging_state() {
  if (M5.Power.getType() == m5::Power_Class::pmic_unknown) return "unknown";
  switch (M5.Power.isCharging()) {
    case m5::Power_Class::is_charging:
      return "charging";
    case m5::Power_Class::is_discharging:
      return "discharging";
    default:
      return "unknown";
  }
}

void draw_power_state() {
  /* Never paint over the SDR control rows (tune row sits ~648–700). */
  if (g_suppress_home_paint || orcsdr::settings::active() || orcsdr::home::active()) return;
  if (rtl_ui_active.load(std::memory_order_acquire)) {
    return;
  }
  char message[96];
  const int32_t level = M5.Power.getBatteryLevel();
  const int16_t battery_mv = M5.Power.getBatteryVoltage();
  const int16_t vbus_mv = M5.Power.getVBUSVoltage();
  snprintf(message, sizeof(message), "Power: %ld%%  battery %dmV  USB %dmV",
           static_cast<long>(level), battery_mv, vbus_mv);
  /* Home layout only — below the Open SDR button, above safe margin. */
  M5.Display.fillRect(200, 500, 880, 40, TFT_BLACK);
  M5.Display.setTextColor(level >= 0 ? TFT_LIGHTGREY : TFT_ORANGE, TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(2);
  M5.Display.drawString(message, 640, 520);
}

void set_rtl_sdr_status(const char* status) {
  strlcpy(rtl_sdr_status, status, sizeof(rtl_sdr_status));
  rtl_sdr_status_revision.fetch_add(1, std::memory_order_release);
}

void draw_rtl_sdr_state() {
  if (g_suppress_home_paint) return;
  if (orcsdr::settings::active() || orcsdr::home::active()) return;
  const bool ready = strstr(rtl_sdr_status, "ready") != nullptr;
  M5.Display.fillRect(150, 545, 980, 40, TFT_BLACK);
  M5.Display.setTextColor(ready ? TFT_GREEN : TFT_ORANGE, TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(2);
  M5.Display.drawString(rtl_sdr_status, 640, 565);
  if (ready) {
    M5.Display.fillRoundRect(kButtonX, kButtonY, kButtonWidth, kButtonHeight, 18,
                             TFT_DARKGREEN);
    M5.Display.drawRoundRect(kButtonX, kButtonY, kButtonWidth, kButtonHeight, 18,
                             TFT_GREEN);
    M5.Display.setTextColor(TFT_WHITE, TFT_DARKGREEN);
    M5.Display.setTextSize(3);
    M5.Display.drawString("Open SDR radio", 640, 360);
  }
  draw_global_settings_gear();
}

void usb_string_to_ascii(const usb_str_desc_t* descriptor, char* output,
                         size_t output_size) {
  if (descriptor == nullptr || output_size == 0) {
    if (output_size > 0) output[0] = '\0';
    return;
  }
  const size_t characters = (descriptor->bLength - 2) / 2;
  const size_t count = min(characters, output_size - 1);
  for (size_t index = 0; index < count; ++index) {
    const uint16_t value = descriptor->wData[index];
    output[index] = value >= 32 && value <= 126 ? static_cast<char>(value) : '?';
  }
  output[count] = '\0';
}

#if RTL_USE_LEGACY_USB
void usb_client_event(const usb_host_client_event_msg_t* event, void*) {
  if (event->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
    pending_usb_address = event->new_dev.address;
  } else if (event->event == USB_HOST_CLIENT_EVENT_DEV_GONE &&
             event->dev_gone.dev_hdl == rtl_sdr_device) {
    rtl_sdr_gone = true;
  }
}

void usb_transfer_complete(usb_transfer_t* transfer) {
  if (transfer != nullptr && transfer->context != nullptr) {
    static_cast<std::atomic<bool>*>(transfer->context)
        ->store(true, std::memory_order_release);
  } else {
    usb_transfer_done.store(true, std::memory_order_release);
  }
}

bool wait_for_flag(std::atomic<bool>* flag, usb_transfer_t* transfer,
                   uint32_t timeout_ms) {
  const uint32_t started = millis();
  while (!flag->load(std::memory_order_acquire) && !rtl_sdr_gone &&
         millis() - started < timeout_ms) {
    uint32_t event_flags = 0;
    usb_host_lib_handle_events(0, &event_flags);
    usb_host_client_handle_events(usb_client, pdMS_TO_TICKS(10));
  }
  if (!flag->load(std::memory_order_acquire)) {
    if (transfer != nullptr && transfer->bEndpointAddress != 0) {
      usb_host_endpoint_halt(transfer->device_handle, transfer->bEndpointAddress);
      usb_host_endpoint_flush(transfer->device_handle, transfer->bEndpointAddress);
    }
    const uint32_t flush_started = millis();
    while (!flag->load(std::memory_order_acquire) &&
           millis() - flush_started < 1000) {
      uint32_t event_flags = 0;
      usb_host_lib_handle_events(0, &event_flags);
      usb_host_client_handle_events(usb_client, pdMS_TO_TICKS(10));
    }
  }
  return flag->load(std::memory_order_acquire) && transfer != nullptr &&
         transfer->status == USB_TRANSFER_STATUS_COMPLETED;
}

bool wait_for_usb_transfer(usb_transfer_t* transfer, uint32_t timeout_ms) {
  return wait_for_flag(&usb_transfer_done, transfer, timeout_ms);
}

bool run_control_record(const RtlControlRecord& record, uint8_t request = 0,
                        bool expect_stall = false) {
  for (uint8_t attempt = 0; attempt < 2; ++attempt) {
    const bool is_in = (record.request_type & USB_BM_REQUEST_TYPE_DIR_IN) != 0;
    const size_t transfer_bytes = sizeof(usb_setup_packet_t) +
        (is_in ? usb_round_up_to_mps(record.length, kRtlControlMps) : record.length);
    usb_transfer_t* transfer = nullptr;
    if (usb_host_transfer_alloc(transfer_bytes, 0, &transfer) != ESP_OK) {
      strlcpy(rtl_capture_error, "control allocation failed", sizeof(rtl_capture_error));
      return false;
    }
    auto* setup = reinterpret_cast<usb_setup_packet_t*>(transfer->data_buffer);
    setup->bmRequestType = record.request_type;
    setup->bRequest = request;
    setup->wValue = record.value;
    setup->wIndex = record.index;
    setup->wLength = record.length;
    if (record.request_type == 0x40 && record.length > 0) {
      memcpy(transfer->data_buffer + sizeof(*setup), record.data, record.length);
    }
    transfer->num_bytes = transfer_bytes;
    transfer->device_handle = rtl_sdr_device;
    transfer->bEndpointAddress = 0;
    transfer->callback = usb_transfer_complete;
    transfer->context = nullptr;
    usb_transfer_done.store(false, std::memory_order_release);
    const esp_err_t submitted = usb_host_transfer_submit_control(usb_client, transfer);
    const bool completed = submitted == ESP_OK &&
                           wait_for_usb_transfer(transfer, kRtlControlTimeoutMs);
    const size_t expected_bytes = sizeof(*setup) + record.length;
    const bool exact = completed &&
                       transfer->actual_num_bytes == static_cast<int>(expected_bytes);
    const bool stalled = submitted == ESP_OK &&
                         usb_transfer_done.load(std::memory_order_acquire) &&
                         transfer->status == USB_TRANSFER_STATUS_STALL;
    if (!exact) {
      snprintf(rtl_capture_error, sizeof(rtl_capture_error),
               "control failed status=%d actual=%d expected=%u",
               submitted == ESP_OK ? static_cast<int>(transfer->status) : -1,
               transfer->actual_num_bytes, static_cast<unsigned>(expected_bytes));
    }
    if (usb_transfer_done.load(std::memory_order_acquire) || submitted != ESP_OK) {
      usb_host_transfer_free(transfer);
    }
    if (exact) {
      if (!expect_stall) return true;
      strlcpy(rtl_capture_error, "expected control STALL completed", sizeof(rtl_capture_error));
      return false;
    }
    if (stalled) {
      Serial.printf(
          "RTL_EP0_STALL bm=%02x request=%02x value=%04x index=%04x length=%u expected=%s\n",
          record.request_type, request, record.value, record.index, record.length,
          expect_stall ? "true" : "false");
      if (expect_stall) return true;
    }
    if (expect_stall || !stalled || attempt != 0 || rtl_sdr_gone) return false;
    Serial.println("RTL_EP0_STALL_RETRY attempt=1");
  }
  return false;
}

bool run_rtl_initialization() {
  for (size_t index = 0; index < std::size(kRtlInitTransfers); ++index) {
    const bool expect_stall = index >= kRtlInitExpectedStallFirst &&
                              index <= kRtlInitExpectedStallLast;
    if (!run_control_record(kRtlInitTransfers[index], 0, expect_stall)) return false;
  }
  return true;
}

bool set_rtl_sample_rate_960k() {
  for (size_t index = 462; index <= 477; ++index) {
    RtlControlRecord record = kRtlInitTransfers[index];
    if (index == 464) {
      record.data[0] = 0x07;
      record.data[1] = 0x80;
    }
    if (!run_control_record(record)) return false;
  }
  return true;
}

template <size_t Count>
bool run_control_records(const RtlControlRecord (&records)[Count], bool best_effort) {
  bool ok = true;
  for (const auto& record : records) {
    if (!run_control_record(record)) {
      ok = false;
      if (!best_effort) break;
    }
  }
  return ok;
}
#endif /* RTL_USE_LEGACY_USB — USB control helpers only */

const char* rtl_band_name(RtlBand band) {
  switch (band) {
    case RtlBand::am: return "AM";
    case RtlBand::wx: return "WX";
    case RtlBand::cb: return "CB";
    case RtlBand::lora: return "LORA";
    case RtlBand::browse: return "BROWSE";
    case RtlBand::adsb: return "ADSB";
    case RtlBand::p25: return "P25";
    default: return "FM";
  }
}

/** Reverse of rtl_band_name(), for serial commands. Uppercase only, matching
 * the existing RTL_LISTEN FM/AM/WX convention elsewhere in this file. */
bool rtl_band_from_name(const char* name, RtlBand* out_band) {
  if (strcmp(name, "FM") == 0) { *out_band = RtlBand::fm; return true; }
  if (strcmp(name, "AM") == 0) { *out_band = RtlBand::am; return true; }
  if (strcmp(name, "WX") == 0) { *out_band = RtlBand::wx; return true; }
  if (strcmp(name, "CB") == 0) { *out_band = RtlBand::cb; return true; }
  if (strcmp(name, "LORA") == 0) { *out_band = RtlBand::lora; return true; }
  if (strcmp(name, "BROWSE") == 0) { *out_band = RtlBand::browse; return true; }
  if (strcmp(name, "ADSB") == 0) { *out_band = RtlBand::adsb; return true; }
  if (strcmp(name, "P25") == 0) { *out_band = RtlBand::p25; return true; }
  return false;
}

const char* rtl_mode_name(RtlBand band) {
  switch (band) {
    case RtlBand::am: return "AM";
    case RtlBand::wx: return "NFM";
    case RtlBand::cb:
      return cb_mode.load(std::memory_order_relaxed) == CbMode::usb ? "USB"
             : cb_mode.load(std::memory_order_relaxed) == CbMode::lsb ? "LSB"
                                                                      : "AM";
    case RtlBand::lora: return "CSS";
    case RtlBand::browse: return "NFM";
    case RtlBand::adsb: return "1090";
    case RtlBand::p25: return "P25 C4FM";
    default: return "WBFM";
  }
}

uint32_t rtl_band_default_frequency(RtlBand band) {
  switch (band) {
    case RtlBand::am: return kRtlAmDefaultHz;
    case RtlBand::wx: return kRtlWxHz;
    case RtlBand::cb: return kCbDefaultHz;
    case RtlBand::lora: return kLoraDefaultHz;
    case RtlBand::browse: return kRtlBrowseDefaultHz;
    case RtlBand::adsb: return kAdsbDefaultHz;
    case RtlBand::p25: return p25_control_frequency_hz;
    default: return rtl_saved_fm_hz;
  }
}

uint32_t rtl_filter_default_hz(RtlBand band) {
  if (band == RtlBand::lora) return lora_bandwidth_hz.load(std::memory_order_relaxed);
  if (band == RtlBand::am || band == RtlBand::cb) return kRtlAmFilterDefaultHz;
  if (band == RtlBand::p25) return kP25StepHz;
  if (band == RtlBand::wx || band == RtlBand::browse || band == RtlBand::adsb)
    return kRtlWxFilterDefaultHz;
  return kRtlFmFilterDefaultHz;
}

uint32_t rtl_clamp_filter_hz(RtlBand band, uint32_t bandwidth_hz) {
  if (band == RtlBand::lora) {
    if (bandwidth_hz <= 93750) return 62500;
    if (bandwidth_hz <= 187500) return 125000;
    if (bandwidth_hz <= 375000) return 250000;
    return 500000;
  }
  if (band == RtlBand::p25) return kP25StepHz;
  const bool am = band == RtlBand::am;
  const uint32_t low = band == RtlBand::cb ? 2400 : am ? 4000 : band == RtlBand::fm ? 50000 : 8000;
  const uint32_t high = band == RtlBand::cb ? 12000 : am ? 30000 : band == RtlBand::fm ? 300000 : 100000;
  return constrain((bandwidth_hz / 1000u) * 1000u, low, high);
}

float rtl_filter_alpha(RtlBand band) {
  constexpr float kPi = 3.14159265358979323846f;
  const float bandwidth = static_cast<float>(
      rtl_clamp_filter_hz(band, rtl_filter_bandwidth_hz.load(std::memory_order_relaxed)));
  return 1.0f - expf(-kPi * bandwidth / static_cast<float>(kRtlSampleRateSps));
}

uint32_t rtl_clamp_frequency(RtlBand band, uint32_t frequency_hz) {
  if (band == RtlBand::cb) {
    size_t best = 0;
    uint32_t distance = UINT32_MAX;
    for (size_t channel = 0; channel < std::size(kCbChannelsHz); ++channel) {
      const uint32_t d = kCbChannelsHz[channel] > frequency_hz
                             ? kCbChannelsHz[channel] - frequency_hz
                             : frequency_hz - kCbChannelsHz[channel];
      if (d < distance) {
        best = channel;
        distance = d;
      }
    }
    return kCbChannelsHz[best];
  }
  if (band == RtlBand::lora) return constrain(frequency_hz, kLoraMinHz, kLoraMaxHz);
  switch (band) {
    case RtlBand::am:
      if (frequency_hz < kRtlAmMinHz) return kRtlAmMinHz;
      if (frequency_hz > kRtlAmMaxHz) return kRtlAmMaxHz;
      return frequency_hz;
    case RtlBand::wx:
      return kRtlWxHz;
    case RtlBand::adsb:
      return kAdsbDefaultHz;
    case RtlBand::p25:
      return constrain(frequency_hz, kP25MinHz, kP25MaxHz);
    case RtlBand::browse:
      return constrain(frequency_hz, kRtlBrowseMinHz, kRtlBrowseMaxHz);
    default:
      if (frequency_hz < kRtlFmMinHz) return kRtlFmMinHz;
      if (frequency_hz > kRtlFmMaxHz) return kRtlFmMaxHz;
      return frequency_hz;
  }
}

void persist_fm_frequency(uint32_t frequency_hz) {
  frequency_hz = rtl_clamp_frequency(RtlBand::fm, frequency_hz);
  if (frequency_hz == rtl_saved_fm_hz) return;
  rtl_saved_fm_hz = frequency_hz;
  // Preferences opened in load_state(); keep writes off the bulk-IQ path.
  preferences.putUInt("sdr_fm_hz", frequency_hz);
  fm_config_save_pending.store(true, std::memory_order_release);
  Serial.printf("RTL_FM_SAVE frequency_hz=%u\n", frequency_hz);
}

void persist_fm_presets() {
  // Called only at scan completion (not the per-sample IQ path) — same tier
  // as persist_fm_frequency's NVS write.
  preferences.putBytes("fm_presets", fm_presets, sizeof(fm_presets));
  preferences.putUChar("fm_preset_count", static_cast<uint8_t>(fm_preset_count));
  fm_config_save_pending.store(true, std::memory_order_release);
  Serial.printf("RTL_PRESETS_SAVE count=%d\n", fm_preset_count);
}

uint32_t rtl_step_frequency(RtlBand band, uint32_t frequency_hz, int direction) {
  if (band == RtlBand::wx) return kRtlWxHz;
  if (band == RtlBand::cb) {
    const uint32_t current = rtl_clamp_frequency(band, frequency_hz);
    size_t channel = 0;
    while (channel + 1 < std::size(kCbChannelsHz) && kCbChannelsHz[channel] != current) {
      ++channel;
    }
    channel = direction < 0 ? (channel + 39) % 40 : (channel + 1) % 40;
    return kCbChannelsHz[channel];
  }
  if (band == RtlBand::lora) {
    constexpr uint32_t step = 125000;
    return direction < 0
               ? (frequency_hz <= kLoraMinHz + step ? kLoraMinHz : frequency_hz - step)
               : min(frequency_hz + step, kLoraMaxHz);
  }
  if (band == RtlBand::p25) {
    if (direction < 0)
      return frequency_hz <= kP25MinHz + kP25StepHz
                 ? kP25MinHz
                 : rtl_clamp_frequency(band, frequency_hz - kP25StepHz);
    return rtl_clamp_frequency(band, frequency_hz + kP25StepHz);
  }
  const uint32_t step = band == RtlBand::am ? rtl_am_step_hz : rtl_fm_step_hz;
  if (direction < 0) {
    if (frequency_hz <= step) return rtl_clamp_frequency(band, 0);
    return rtl_clamp_frequency(band, frequency_hz - step);
  }
  return rtl_clamp_frequency(band, frequency_hz + step);
}

size_t cb_channel_index(uint32_t frequency_hz) {
  const uint32_t snapped = rtl_clamp_frequency(RtlBand::cb, frequency_hz);
  for (size_t channel = 0; channel < std::size(kCbChannelsHz); ++channel) {
    if (kCbChannelsHz[channel] == snapped) return channel;
  }
  return 18;
}

void format_frequency(char* output, size_t output_size, uint32_t frequency_hz) {
  if (frequency_hz >= 1000000) {
    // 0.001 MHz (1 kHz) resolution for fine tuning / dipole peaking.
    snprintf(output, output_size, "%.3f MHz", frequency_hz / 1000000.0);
  } else {
    // 0.1 kHz display on MW so AM steps stay readable.
    snprintf(output, output_size, "%.1f kHz", frequency_hz / 1000.0);
  }
}

void apply_speaker_volume(uint8_t volume) {
  // Keep master and virtual-channel levels aligned; some M5 paths only honor one.
  M5.Speaker.setVolume(volume);
  M5.Speaker.setChannelVolume(0, volume);
}

bool ensure_speaker_running(uint8_t volume) {
  if (!rtl_audio_enabled.load(std::memory_order_acquire)) return false;
  if (!rtl_speaker_start_allowed.load(std::memory_order_acquire)) return false;
  apply_speaker_volume(volume);
  if (!M5.Speaker.isEnabled()) return false;
  if (!M5.Speaker.isRunning()) {
    // Speaker.begin() deletes and recreates the shared I2S TX channel.  The
    // RTL delivery task and UI can both request it during stream startup.
    static SemaphoreHandle_t begin_mutex = xSemaphoreCreateMutex();
    if (begin_mutex == nullptr || xSemaphoreTake(begin_mutex, pdMS_TO_TICKS(250)) != pdTRUE) return false;
    const bool started = M5.Speaker.isRunning() || M5.Speaker.begin();
    xSemaphoreGive(begin_mutex);
    if (!started) return false;
  }
  apply_speaker_volume(volume);
  return M5.Speaker.isRunning() || M5.Speaker.isEnabled();
}

void allow_boot_speaker() {
  if (!rtl_speaker_start_allowed.exchange(true, std::memory_order_acq_rel)) {
    Serial.println("BOOT_STAGE speaker_start");
    begin_power_monitor("speaker_start");
  }
  if (rtl_audio_enabled.load(std::memory_order_acquire))
    (void)ensure_speaker_running(rtl_live_volume.load(std::memory_order_acquire));
}

bool rtl_band_has_audio(RtlBand band) {
  return band != RtlBand::adsb && band != RtlBand::lora;
}

void sync_rtl_audio_for_band(RtlBand band) {
  const bool enabled = rtl_audio_user_enabled.load(std::memory_order_acquire) &&
                       rtl_band_has_audio(band);
  rtl_audio_enabled.store(enabled, std::memory_order_release);
  rtl_audio_play_count = 0;
  if (!enabled) M5.Speaker.stop();
}

void schedule_rtl_audio_settings_persist() {
  uint32_t due = millis() + 1500;
  if (due == 0) due = 1;
  rtl_audio_settings_persist_due_ms.store(due, std::memory_order_release);
}

void set_rtl_audio_user_enabled(bool enabled) {
  rtl_audio_user_enabled.store(enabled, std::memory_order_release);
  sync_rtl_audio_for_band(rtl_ui_band);
  if (rtl_audio_enabled.load(std::memory_order_acquire)) {
    (void)ensure_speaker_running(rtl_live_volume.load(std::memory_order_acquire));
  }
  schedule_rtl_audio_settings_persist();
}

bool rtl_audio_block_ready(size_t index, uint32_t now) {
  return static_cast<int32_t>(now - rtl_audio_play_block_ready_ms[index]) >= 0;
}

bool rtl_audio_select_writable_block(uint32_t now) {
  if (rtl_audio_play_count != 0 || rtl_audio_block_ready(rtl_audio_play_block_index, now)) {
    return true;
  }
  for (size_t offset = 1; offset < kRtlAudioPlayBlockCount; ++offset) {
    const size_t candidate = (rtl_audio_play_block_index + offset) % kRtlAudioPlayBlockCount;
    if (rtl_audio_block_ready(candidate, now)) {
      rtl_audio_play_block_index = candidate;
      return true;
    }
  }
  rtl_audio_ring_overruns.fetch_add(1, std::memory_order_relaxed);
  return false;
}

void flush_audio_play_batch(bool force) {
  if (rtl_audio_play_count == 0) return;
  if (!force && rtl_audio_play_count < kRtlAudioPlayBatchSamples) return;
  if (rtl_volume_changed.exchange(false, std::memory_order_acq_rel)) {
    apply_speaker_volume(rtl_live_volume.load(std::memory_order_acquire));
  }
  if (!M5.Speaker.isRunning()) {
    if (!ensure_speaker_running(rtl_live_volume.load(std::memory_order_acquire))) {
      rtl_audio_play_count = 0;
      return;
    }
  }
  if (!rtl_audio_enabled.load(std::memory_order_acquire)) {
    rtl_audio_play_count = 0;
    return;
  }
  const uint32_t now = millis();
  if (!rtl_audio_select_writable_block(now)) {
    rtl_audio_play_count = 0;
    return;
  }
  const size_t submitted_index = rtl_audio_play_block_index;
  if (M5.Speaker.playRaw(rtl_audio_play_blocks[submitted_index], rtl_audio_play_count * 2,
                         48000, true, 1, 0, false)) {
    ++rtl_audio.queued_chunks;
    const uint32_t duration_ms = static_cast<uint32_t>(
        (rtl_audio_play_count * 1000u + 47999u) / 48000u);
    rtl_audio_play_block_ready_ms[submitted_index] = now + duration_ms + kRtlAudioPlaySafetyMs;
    rtl_audio_play_block_index = (submitted_index + 1) % kRtlAudioPlayBlockCount;
  } else {
    ++rtl_audio.dropped_chunks;
    rtl_audio_submit_failures.fetch_add(1, std::memory_order_relaxed);
    if (kStreamDiagnosticsEnabled && (rtl_audio.dropped_chunks % 32) == 1) {
      Serial.printf("RTL_AUDIO_DROP chunks_ok=%u dropped=%u peak=%d volume=%u running=%s\n",
                    rtl_audio.queued_chunks, rtl_audio.dropped_chunks, rtl_audio.peak,
                    rtl_live_volume.load(std::memory_order_acquire),
                    M5.Speaker.isRunning() ? "true" : "false");
    }
  }
  rtl_audio_play_count = 0;
}

const char* rtl_audio_test_mode_name() {
  if (rtl_audio_test_tone.load(std::memory_order_acquire)) return "tone";
  if (rtl_audio_test_metrics.load(std::memory_order_acquire)) return "fm";
  return "idle";
}

void rtl_audio_test_emit_status() {
  rtl_sdr_v4_esp_metrics_t metrics{};
  if (g_rtl != nullptr) (void)rtl_sdr_v4_esp_get_metrics(g_rtl, &metrics);
  const auto speaker = M5.Speaker.config();
  const uint32_t elapsed = rtl_audio_test_started_ms == 0 ? 0 : millis() - rtl_audio_test_started_ms;
  Serial.printf(
      "{\"type\":\"rtl_audio_test\",\"mode\":\"%s\",\"elapsed_ms\":%u,"
      "\"speaker_enabled\":%u,\"speaker_running\":%u,\"sample_rate\":%u,"
      "\"stereo\":%u,\"speaker_core\":%u,\"speaker_priority\":%u,"
      "\"ring_blocks\":%u,\"ring_overruns\":%u,\"submit_failures\":%u,"
      "\"audio_chunks\":%u,\"audio_drops\":%u,\"effective_sps\":%u,"
      "\"usb_overruns\":%u,\"usb_drops\":%u,\"dsp_block_us_max\":%u,"
      "\"dsp_gate_us\":13653,\"task_count\":%u,\"task_delta\":%d,\"free_heap\":%u}\n",
      rtl_audio_test_mode_name(), elapsed, M5.Speaker.isEnabled() ? 1 : 0,
      M5.Speaker.isRunning() ? 1 : 0, static_cast<unsigned>(speaker.sample_rate),
      speaker.stereo ? 1 : 0, static_cast<unsigned>(speaker.task_pinned_core),
      static_cast<unsigned>(speaker.task_priority), static_cast<unsigned>(kRtlAudioPlayBlockCount),
      rtl_audio_ring_overruns.load(std::memory_order_relaxed),
      rtl_audio_submit_failures.load(std::memory_order_relaxed), rtl_audio.queued_chunks,
      rtl_audio.dropped_chunks, metrics.effective_sps, metrics.overruns, metrics.consumer_drops,
      rtl_dsp_block_us_max.load(std::memory_order_relaxed),
      static_cast<unsigned>(uxTaskGetNumberOfTasks()),
      static_cast<int>(uxTaskGetNumberOfTasks()) - static_cast<int>(rtl_audio_test_task_baseline),
      ESP.getFreeHeap());
}

void rtl_audio_test_start_tone() {
  rtl_audio_test_metrics.store(true, std::memory_order_release);
  rtl_audio_test_tone.store(true, std::memory_order_release);
  rtl_audio_test_started_ms = millis();
  rtl_audio_test_last_report_ms = 0;
  rtl_audio_ring_overruns.store(0, std::memory_order_relaxed);
  rtl_audio_submit_failures.store(0, std::memory_order_relaxed);
  rtl_audio_play_count = 0;
  rtl_audio_play_block_index = 0;
  for (auto& ready_ms : rtl_audio_play_block_ready_ms) ready_ms = 0;
  rtl_audio_enabled.store(true, std::memory_order_release);
  allow_boot_speaker();
  const bool speaker_ok = ensure_speaker_running(rtl_live_volume.load(std::memory_order_acquire));
  rtl_audio_test_task_baseline = uxTaskGetNumberOfTasks();
  Serial.printf("RTL_AUDIO_TEST_TONE %s\n", speaker_ok ? "OK" : "ERR");
  rtl_audio_test_emit_status();
}

void rtl_audio_test_start_fm() {
  if (!rtl_ui_active.load(std::memory_order_acquire) || g_stream_band != RtlBand::fm) {
    Serial.println("RTL_AUDIO_TEST_FM_ERR active_fm_required");
    return;
  }
  rtl_audio_test_tone.store(false, std::memory_order_release);
  rtl_audio_test_metrics.store(true, std::memory_order_release);
  rtl_audio_test_started_ms = millis();
  rtl_audio_test_last_report_ms = 0;
  rtl_audio_ring_overruns.store(0, std::memory_order_relaxed);
  rtl_audio_submit_failures.store(0, std::memory_order_relaxed);
  rtl_audio_play_count = 0;
  rtl_audio_play_block_index = 0;
  for (auto& ready_ms : rtl_audio_play_block_ready_ms) ready_ms = 0;
  rtl_audio_enabled.store(true, std::memory_order_release);
  allow_boot_speaker();
  if (!ensure_speaker_running(rtl_live_volume.load(std::memory_order_acquire))) {
    rtl_audio_test_metrics.store(false, std::memory_order_release);
    Serial.println("RTL_AUDIO_TEST_FM_ERR speaker_not_running");
    return;
  }
  rtl_audio_test_task_baseline = uxTaskGetNumberOfTasks();
  Serial.println("RTL_AUDIO_TEST_FM_OK");
  rtl_audio_test_emit_status();
}

void rtl_audio_test_stop() {
  rtl_audio_test_tone.store(false, std::memory_order_release);
  rtl_audio_test_metrics.store(false, std::memory_order_release);
  rtl_audio_play_count = 0;
  M5.Speaker.stop();
  Serial.println("RTL_AUDIO_TEST_STOP_OK");
}

void rtl_audio_test_service() {
  if (!rtl_audio_test_metrics.load(std::memory_order_acquire)) return;
  const uint32_t now = millis();
  if (rtl_audio_test_tone.load(std::memory_order_acquire)) {
    static constexpr int16_t kTonePeriod[] = {
        0, 1175, 2329, 3444, 4500, 5479, 6364, 7140, 7794, 8315, 8693, 8923,
        9000, 8923, 8693, 8315, 7794, 7140, 6364, 5479, 4500, 3444, 2329, 1175,
        0, -1175, -2329, -3444, -4500, -5479, -6364, -7140, -7794, -8315, -8693,
        -8923, -9000, -8923, -8693, -8315, -7794, -7140, -6364, -5479, -4500,
        -3444, -2329, -1175};
    static int16_t tone_block[480];
    static size_t tone_index = 0;
    static uint32_t next_tone_ms = 0;
    if (next_tone_ms == 0) next_tone_ms = now;
    for (uint8_t block = 0; block < 3 && static_cast<int32_t>(now - next_tone_ms) >= 0; ++block) {
      for (auto& sample : tone_block) sample = kTonePeriod[tone_index++ % std::size(kTonePeriod)];
      queue_audio_samples(tone_block, std::size(tone_block));
      next_tone_ms += 10;
    }
  }
  if (now - rtl_audio_test_last_report_ms >= 1000) {
    rtl_audio_test_last_report_ms = now;
    rtl_audio_test_emit_status();
  }
}

bool audio_rec_ensure_buffer() {
  if (g_audio_rec_buf != nullptr && g_audio_rec_capacity >= kAudioRecMaxSamples) {
    return true;
  }
  if (g_audio_rec_buf != nullptr) {
    heap_caps_free(g_audio_rec_buf);
    g_audio_rec_buf = nullptr;
    g_audio_rec_capacity = 0;
  }
  g_audio_rec_buf = static_cast<int16_t*>(
      heap_caps_malloc(kAudioRecMaxSamples * sizeof(int16_t),
                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (g_audio_rec_buf == nullptr) {
    g_audio_rec_buf = static_cast<int16_t*>(
        heap_caps_malloc(kAudioRecMaxSamples * sizeof(int16_t), MALLOC_CAP_8BIT));
  }
  if (g_audio_rec_buf == nullptr) {
    Serial.println("RTL_REC_ERR no_buffer");
    return false;
  }
  g_audio_rec_capacity = kAudioRecMaxSamples;
  Serial.printf("RTL_REC_BUF samples=%u bytes=%u psram=%s\n",
                static_cast<unsigned>(g_audio_rec_capacity),
                static_cast<unsigned>(g_audio_rec_capacity * sizeof(int16_t)),
                heap_caps_get_free_size(MALLOC_CAP_SPIRAM) > 0 ? "yes" : "no");
  return true;
}

void audio_rec_append(const int16_t* samples, size_t count) {
  if (samples == nullptr || count == 0 || g_audio_rec_buf == nullptr) return;
  if (!g_audio_rec_active.load(std::memory_order_relaxed)) return;
  size_t w = g_audio_rec_write.load(std::memory_order_relaxed);
  const size_t cap = g_audio_rec_capacity;
  if (w >= cap) {
    g_audio_rec_full.store(true, std::memory_order_release);
    g_audio_rec_active.store(false, std::memory_order_release);
    return;
  }
  const size_t n = (w + count > cap) ? (cap - w) : count;
  memcpy(g_audio_rec_buf + w, samples, n * sizeof(int16_t));
  w += n;
  g_audio_rec_write.store(w, std::memory_order_release);
  if (w >= cap) {
    g_audio_rec_full.store(true, std::memory_order_release);
    g_audio_rec_active.store(false, std::memory_order_release);
    g_audio_rec_export_pending.store(true, std::memory_order_release);
    Serial.printf("RTL_REC_FULL samples=%u sec=%.1f\n", static_cast<unsigned>(w),
                  static_cast<double>(w) / static_cast<double>(kAudioRecRateHz));
  }
}

bool ensure_tab5_sd() {
  if (g_sd_ready) return true;
  if (g_sd_tried && !g_sd_ready) return false;
  g_sd_tried = true;
  SD_MMC.setPowerChannel(-1);  // Tab5 card power is external; LDO channel 4 is in use.
  SD_MMC.setPins(43, 44, 39, 40, 41, 42);
  if (SD_MMC.begin("/sd", false, false, SDMMC_FREQ_HIGHSPEED)) {
    g_sd_fs = &SD_MMC;
    g_sd_ready = true;
    Serial.println("RTL_REC_SD ready bus=sdmmc");
    return true;
  }
  SPI.begin(kTab5SdSckPin, kTab5SdMisoPin, kTab5SdMosiPin, kTab5SdCsPin);
  /* 10 MHz is safer on long-ish Tab5 SPI than 25 MHz during concurrent USB. */
  if (!SD.begin(kTab5SdCsPin, SPI, 10000000)) {
    Serial.println("RTL_REC_SD missing_or_fail");
    g_sd_fs = nullptr;
    g_sd_ready = false;
    return false;
  }
  g_sd_fs = &SD;
  g_sd_ready = true;
  Serial.println("RTL_REC_SD ready bus=spi");
  return true;
}

uint64_t sd_total_bytes() {
  if (!g_sd_ready || g_sd_fs == nullptr) return 0;
  return g_sd_fs == &SD_MMC ? SD_MMC.totalBytes() : SD.totalBytes();
}

void apply_p25_config(const orcsdr::p25config::Config& config, const char* status) {
  p25_config = config;
  p25_auto_follow.store(config.auto_follow, std::memory_order_release);
  p25_encryption_skip.store(config.encryption_skip, std::memory_order_release);
  p25_hold_talkgroup = config.hold_talkgroup;
  p25_hold.store(config.hold_talkgroup != 0, std::memory_order_release);
  p25_control_frequency_hz = config.last_control_channel_hz != 0
                                 ? config.last_control_channel_hz
                                 : config.control_channels_hz[0];
  p25_candidate_index = 0;
  for (size_t i = 0; i < config.control_channel_count; ++i) {
    if (config.control_channels_hz[i] == p25_control_frequency_hz) {
      p25_candidate_index = static_cast<uint8_t>(i);
      break;
    }
  }
  std::fill(std::begin(p25_candidate_levels), std::end(p25_candidate_levels), -120.0f);
  std::fill(std::begin(p25_candidate_tsbk_good), std::end(p25_candidate_tsbk_good), 0);
  strlcpy(p25_config_status, status, sizeof(p25_config_status));
  ++p25_config_revision;
  Serial.printf("RTL_P25_CONFIG_LOAD status=\"%s\" channels=%u talkgroups=%u control_hz=%lu\n",
                p25_config_status, static_cast<unsigned>(config.control_channel_count),
                static_cast<unsigned>(config.talkgroup_count),
                static_cast<unsigned long>(p25_control_frequency_hz));
}

void load_p25_config() {
  orcsdr::p25config::Config config{};
  char error[64]{};
  if (!ensure_tab5_sd() || g_sd_fs == nullptr) {
    orcsdr::p25config::defaults(&config);
    const uint32_t stored = preferences.getUInt("p25_ctrl_hz", 0);
    for (size_t i = 0; i < config.control_channel_count; ++i)
      if (config.control_channels_hz[i] == stored) config.last_control_channel_hz = stored;
    apply_p25_config(config, "SD unavailable; starter profile");
    return;
  }
  const auto result = orcsdr::p25config::load(*g_sd_fs, orcsdr::p25config::kPath,
                                               &config, error, sizeof(error));
  if (result == orcsdr::p25config::LoadResult::ok) {
    apply_p25_config(config, "P25.cfg loaded");
    return;
  }
  if (result == orcsdr::p25config::LoadResult::missing) {
    orcsdr::p25config::defaults(&config);
    if (orcsdr::p25config::save(*g_sd_fs, config, error, sizeof(error))) {
      apply_p25_config(config, "P25.cfg created; edit on SD");
    } else {
      apply_p25_config(config, "Starter profile; SD save failed");
    }
    return;
  }
  char backup[48]{};
  snprintf(backup, sizeof(backup), "%s.bak", orcsdr::p25config::kPath);
  if (orcsdr::p25config::load(*g_sd_fs, backup, &config, error, sizeof(error)) ==
      orcsdr::p25config::LoadResult::ok) {
    apply_p25_config(config, "P25.cfg invalid; backup loaded");
    return;
  }
  orcsdr::p25config::defaults(&config);
  apply_p25_config(config, "P25.cfg invalid; starter active");
  Serial.printf("RTL_P25_CONFIG_ERROR detail=\"%s\"\n", error);
}

void request_p25_config_save() {
  p25_config.last_control_channel_hz = p25_control_frequency_hz;
  p25_config.auto_follow = p25_auto_follow.load(std::memory_order_acquire);
  p25_config.encryption_skip = p25_encryption_skip.load(std::memory_order_acquire);
  p25_config.hold_talkgroup = p25_hold.load(std::memory_order_acquire) ? p25_hold_talkgroup : 0;
  p25_config_save_pending.store(true, std::memory_order_release);
  ++p25_config_revision;
}

orcsdr::fmconfig::Config fm_config_from_runtime() {
  orcsdr::fmconfig::Config config{};
  config.startup_frequency_hz = rtl_saved_fm_hz;
  for (int i = 0; i < fm_preset_count && i < static_cast<int>(orcsdr::fmconfig::kMaxPresets); ++i)
    config.presets_hz[config.preset_count++] = fm_presets[i].freq_hz;
  return config;
}

void apply_fm_config(const orcsdr::fmconfig::Config& config, const char* status) {
  rtl_saved_fm_hz = config.startup_frequency_hz;
  fm_preset_count = config.preset_count;
  fm_preset_scroll_top = 0;
  for (int i = 0; i < fm_preset_count; ++i) {
    fm_presets[i].freq_hz = config.presets_hz[i];
    fm_presets[i].level_dbfs = -120.0f;
  }
  Serial.printf("RTL_FM_CONFIG_LOAD status=\"%s\" frequency_hz=%lu presets=%d\n", status,
                static_cast<unsigned long>(rtl_saved_fm_hz), fm_preset_count);
}

void load_fm_config() {
  if (!ensure_tab5_sd() || g_sd_fs == nullptr) return;
  orcsdr::fmconfig::Config config{};
  char error[64]{};
  const auto result = orcsdr::fmconfig::load(*g_sd_fs, orcsdr::fmconfig::kPath,
                                              &config, error, sizeof(error));
  if (result == orcsdr::fmconfig::LoadResult::ok) {
    apply_fm_config(config, "FM.cfg loaded");
    return;
  }
  if (result == orcsdr::fmconfig::LoadResult::missing) {
    config = fm_config_from_runtime();
    if (orcsdr::fmconfig::save(*g_sd_fs, config, error, sizeof(error)))
      Serial.println("RTL_FM_CONFIG_CREATE path=/orcsdr/FM.cfg");
  } else {
    Serial.printf("RTL_FM_CONFIG_ERROR detail=\"%s\" retaining NVS state\n", error);
}

#pragma pack(push, 1)
struct AdsbIndexHeader {
  char magic[8];
  uint32_t record_size;
  uint32_t record_count;
};

struct AdsbIndexRecord {
  uint32_t icao;
  char registration[9];
  char type[49];
  char owner[51];
};
#pragma pack(pop)

bool lookup_adsb_metadata(uint32_t icao, AdsbIndexRecord* result) {
  constexpr const char* kPath = "/orcsdr/adsb_aircraft.idx";
  if (!result || !ensure_tab5_sd() || !g_sd_fs->exists(kPath)) return false;
  File file = g_sd_fs->open(kPath, FILE_READ);
  AdsbIndexHeader header{};
  if (!file || file.read(reinterpret_cast<uint8_t*>(&header), sizeof(header)) != sizeof(header) ||
      std::memcmp(header.magic, "ORCADSB1", 8) != 0 ||
      header.record_size != sizeof(AdsbIndexRecord)) {
    file.close();
    return false;
  }
  uint32_t low = 0, high = header.record_count;
  while (low < high) {
    const uint32_t middle = low + (high - low) / 2;
    if (!file.seek(sizeof(header) + middle * sizeof(AdsbIndexRecord)) ||
        file.read(reinterpret_cast<uint8_t*>(result), sizeof(*result)) != sizeof(*result)) {
      file.close();
      return false;
    }
    if (result->icao < icao) low = middle + 1;
    else high = middle;
  }
  const bool found = low < header.record_count && result->icao == icao;
  file.close();
  return found;
}

void enrich_one_adsb_track() {
  uint32_t icao = 0;
  portENTER_CRITICAL(&adsb_tracks_mux);
  for (auto& track : adsb_tracks) {
    if (!track.used || !track.metadata_pending) continue;
    track.metadata_pending = false;
    icao = track.icao;
    break;
  }
  portEXIT_CRITICAL(&adsb_tracks_mux);
  if (!icao) return;

  AdsbIndexRecord metadata{};
  const bool found = lookup_adsb_metadata(icao, &metadata);
  if (found) {
    portENTER_CRITICAL(&adsb_tracks_mux);
    for (auto& track : adsb_tracks) {
      if (!track.used || track.icao != icao) continue;
      strlcpy(track.registration, metadata.registration, sizeof(track.registration));
      strlcpy(track.type, metadata.type, sizeof(track.type));
      strlcpy(track.owner, metadata.owner, sizeof(track.owner));
      break;
    }
    portEXIT_CRITICAL(&adsb_tracks_mux);
    adsb_track_revision.fetch_add(1, std::memory_order_release);
  }
  Serial.printf("RTL_ADSB_META icao=%06lX found=%d registration=%s\n",
                static_cast<unsigned long>(icao), found ? 1 : 0,
                found ? metadata.registration : "-");
}

size_t format_lora_csv(char* output, size_t output_size,
                       const LoraLogRecord& record) {
  if (output == nullptr || output_size < 4) return 0;
  const LoraDisplayPacket& packet = record.packet;
  char destination[16];
  if (packet.destination == UINT32_MAX) {
    strlcpy(destination, "broadcast", sizeof(destination));
  } else {
    snprintf(destination, sizeof(destination), "!%08lx",
             static_cast<unsigned long>(packet.destination));
  }
  int used = snprintf(
      output, output_size,
      "%lu,%lu,!%08lx,%s,%08lx,%u,%d,%d,%ld,%ld,\"",
      static_cast<unsigned long>(packet.received_ms),
      static_cast<unsigned long>(record.frequency_hz),
      static_cast<unsigned long>(packet.sender),
      destination,
      static_cast<unsigned long>(packet.packet_id),
      static_cast<unsigned>(packet.port), static_cast<int>(packet.snr_tenths),
      static_cast<int>(packet.signal_tenths), static_cast<long>(packet.latitude_e7),
      static_cast<long>(packet.longitude_e7));
  if (used < 0 || static_cast<size_t>(used) >= output_size) return 0;
  size_t position = static_cast<size_t>(used);
  for (const char* text = packet.text; *text && position + 4 < output_size; ++text) {
    if (*text == '"') output[position++] = '"';
    output[position++] = *text;
  }
  output[position++] = '"';
  output[position++] = '\n';
  output[position] = '\0';
  return position;
}

void lora_sd_log_task(void*) {
  File file;
  static char batch[4096];
  static char line[512];
  size_t batch_bytes = 0;
  uint32_t last_flush_ms = millis();
  for (;;) {
    LoraLogRecord record;
    if (xQueueReceive(lora_log_queue, &record, pdMS_TO_TICKS(200)) == pdTRUE) {
      const size_t line_bytes = format_lora_csv(line, sizeof(line), record);
      if (line_bytes > 0 && batch_bytes + line_bytes <= sizeof(batch)) {
        memcpy(batch + batch_bytes, line, line_bytes);
        batch_bytes += line_bytes;
      } else {
        lora_log_dropped.fetch_add(1, std::memory_order_relaxed);
      }
    }

    const bool requested = lora_log_requested.load(std::memory_order_acquire);
    if (requested && !file && !lora_log_error.load(std::memory_order_relaxed)) {
      if (ensure_tab5_sd() &&
          (g_sd_fs->exists("/orcsdr") || g_sd_fs->mkdir("/orcsdr"))) {
        file = g_sd_fs->open(kLoraLogPath, FILE_APPEND, true);
      }
      if (file) {
        if (file.size() == 0) {
          file.print("uptime_ms,frequency_hz,from,to,packet_id,port,snr_tenths,"
                     "signal_tenths,latitude_e7,longitude_e7,text\n");
          file.flush();
        }
        lora_log_ready.store(true, std::memory_order_release);
        Serial.printf("LORA_SD_LOG_READY path=\"%s\"\n", kLoraLogPath);
      } else {
        lora_log_error.store(true, std::memory_order_release);
        Serial.println("LORA_SD_LOG_ERROR open_failed");
      }
    }

    const uint32_t now = millis();
    const bool idle_after_packet =
        now - lora_log_last_packet_ms.load(std::memory_order_relaxed) >= 500u;
    const bool flush_due = batch_bytes > 0 &&
                           (!requested || batch_bytes >= 3072u ||
                            now - last_flush_ms >= 5000u);
    if (file && flush_due && (idle_after_packet || !requested)) {
      if (file.write(reinterpret_cast<const uint8_t*>(batch), batch_bytes) !=
          batch_bytes) {
        lora_log_error.store(true, std::memory_order_release);
        Serial.println("LORA_SD_LOG_ERROR write_failed");
      }
      file.flush();
      batch_bytes = 0;
      last_flush_ms = now;
    }
    if (!requested && file) {
      file.close();
      lora_log_ready.store(false, std::memory_order_release);
      Serial.printf("LORA_SD_LOG_STOPPED dropped=%lu\n",
                    static_cast<unsigned long>(
                        lora_log_dropped.load(std::memory_order_relaxed)));
    }
  }
}

void set_lora_sd_logging(bool enabled) {
  if (enabled) {
    if (lora_log_queue == nullptr) {
      lora_log_queue = xQueueCreate(kLoraLogQueueDepth, sizeof(LoraLogRecord));
    }
    if (lora_log_queue == nullptr ||
        (lora_log_task_handle == nullptr &&
         xTaskCreatePinnedToCore(lora_sd_log_task, "lora_sd_log", 6144, nullptr, 1,
                                 &lora_log_task_handle, 0) != pdPASS)) {
      lora_log_error.store(true, std::memory_order_release);
      Serial.println("LORA_SD_LOG_ERROR task_failed");
      return;
    }
    if (!g_sd_ready) g_sd_tried = false;
    lora_log_error.store(false, std::memory_order_release);
  }
  lora_log_requested.store(enabled, std::memory_order_release);
  bump_rtl_ui();
  Serial.printf("LORA_SD_LOG %s\n", enabled ? "ON" : "OFF");
}

void enqueue_lora_sd_log(const LoraDisplayPacket& packet) {
  if (!lora_log_requested.load(std::memory_order_relaxed) ||
      lora_log_queue == nullptr) return;
  LoraLogRecord record{packet, rtl_ui_frequency_hz};
  lora_log_last_packet_ms.store(millis(), std::memory_order_relaxed);
  if (xQueueSend(lora_log_queue, &record, 0) != pdTRUE) {
    lora_log_dropped.fetch_add(1, std::memory_order_relaxed);
  }
}

static void write_le16(File& f, uint16_t v) {
  uint8_t b[2] = {static_cast<uint8_t>(v & 0xff), static_cast<uint8_t>((v >> 8) & 0xff)};
  f.write(b, 2);
}

static void write_le32(File& f, uint32_t v) {
  uint8_t b[4] = {static_cast<uint8_t>(v & 0xff), static_cast<uint8_t>((v >> 8) & 0xff),
                  static_cast<uint8_t>((v >> 16) & 0xff),
                  static_cast<uint8_t>((v >> 24) & 0xff)};
  f.write(b, 4);
}

bool iq_rec_ensure_buffers() {
  if (g_iq_rec_buf == nullptr) {
    g_iq_rec_buf = static_cast<uint8_t*>(
        heap_caps_malloc(kIqRecMaxBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (g_lora_pre_roll_buf == nullptr) {
    g_lora_pre_roll_buf = static_cast<uint8_t*>(
        heap_caps_malloc(kLoraPreRollBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  return g_iq_rec_buf != nullptr && g_lora_pre_roll_buf != nullptr;
}

void lora_iq_reset_detector() {
  g_lora_pre_roll_write = 0;
  g_lora_pre_roll_fill = 0;
  g_lora_noise_floor_dbfs = -90.0f;
  g_lora_noise_samples = 0;
  g_lora_trigger_armed = false;
  lora_noise_dbfs.store(-90.0f, std::memory_order_relaxed);
  lora_trigger_dbfs.store(-75.0f, std::memory_order_relaxed);
  if (!iq_rec_ensure_buffers()) Serial.println("RTL_IQ_ERROR no_psram_buffer");
}

void lora_pre_roll_append(const uint8_t* iq, size_t bytes) {
  if (g_lora_pre_roll_buf == nullptr || iq == nullptr || bytes == 0) return;
  if (bytes >= kLoraPreRollBytes) {
    memcpy(g_lora_pre_roll_buf, iq + bytes - kLoraPreRollBytes, kLoraPreRollBytes);
    g_lora_pre_roll_write = 0;
    g_lora_pre_roll_fill = kLoraPreRollBytes;
    return;
  }
  const size_t first = min(bytes, kLoraPreRollBytes - g_lora_pre_roll_write);
  memcpy(g_lora_pre_roll_buf + g_lora_pre_roll_write, iq, first);
  if (first < bytes) memcpy(g_lora_pre_roll_buf, iq + first, bytes - first);
  g_lora_pre_roll_write = (g_lora_pre_roll_write + bytes) % kLoraPreRollBytes;
  g_lora_pre_roll_fill = min(kLoraPreRollBytes, g_lora_pre_roll_fill + bytes);
}

size_t lora_copy_pre_roll() {
  if (g_iq_rec_buf == nullptr || g_lora_pre_roll_buf == nullptr ||
      g_lora_pre_roll_fill == 0) return 0;
  if (g_lora_pre_roll_fill < kLoraPreRollBytes) {
    memcpy(g_iq_rec_buf, g_lora_pre_roll_buf, g_lora_pre_roll_fill);
    return g_lora_pre_roll_fill;
  }
  const size_t tail = kLoraPreRollBytes - g_lora_pre_roll_write;
  memcpy(g_iq_rec_buf, g_lora_pre_roll_buf + g_lora_pre_roll_write, tail);
  if (g_lora_pre_roll_write > 0) {
    memcpy(g_iq_rec_buf + tail, g_lora_pre_roll_buf, g_lora_pre_roll_write);
  }
  return kLoraPreRollBytes;
}

void iq_rec_begin(bool automatic, size_t initial_bytes) {
  g_iq_rec_frequency_hz = rtl_ui_frequency_hz;
  g_iq_rec_sf = lora_sf.load(std::memory_order_relaxed);
  g_iq_rec_bandwidth_hz = lora_bandwidth_hz.load(std::memory_order_relaxed);
  g_iq_rec_last_path[0] = '\0';
  g_iq_rec_ready.store(false, std::memory_order_release);
  g_iq_rec_auto_triggered.store(automatic, std::memory_order_release);
  g_iq_rec_write.store(initial_bytes, std::memory_order_release);
  g_iq_rec_active.store(true, std::memory_order_release);
  Serial.printf("RTL_IQ_START mode=%s bytes=%u seconds=%u rate=%u frequency_hz=%u sf=%u bw=%u\n",
                automatic ? "energy" : "manual", static_cast<unsigned>(kIqRecMaxBytes),
                static_cast<unsigned>(kIqRecSeconds), kRtlSampleRateSps,
                g_iq_rec_frequency_hz, static_cast<unsigned>(g_iq_rec_sf),
                static_cast<unsigned>(g_iq_rec_bandwidth_hz));
}

bool iq_rec_start() {
  if (rtl_ui_band != RtlBand::lora) {
    Serial.println("RTL_IQ_ERROR lora_mode_required");
    return false;
  }
  if (g_iq_rec_active.load(std::memory_order_acquire)) return true;
  if (!iq_rec_ensure_buffers()) {
    Serial.println("RTL_IQ_ERROR no_psram_buffer");
    return false;
  }
  iq_rec_begin(false, 0);
  return true;
}

void iq_rec_append(const uint8_t* iq, size_t bytes) {
  if (!g_iq_rec_active.load(std::memory_order_relaxed) || iq == nullptr || bytes == 0) return;
  size_t written = g_iq_rec_write.load(std::memory_order_relaxed);
  if (written >= kIqRecMaxBytes) return;
  const size_t count = min(bytes, kIqRecMaxBytes - written);
  memcpy(g_iq_rec_buf + written, iq, count);
  written += count;
  g_iq_rec_write.store(written, std::memory_order_release);
  if (written == kIqRecMaxBytes) {
    g_iq_rec_active.store(false, std::memory_order_release);
    g_iq_rec_ready.store(true, std::memory_order_release);
    Serial.printf("RTL_IQ_DONE storage=psram bytes=%u samples=%u rate=%u frequency_hz=%u sf=%u bw=%u mode=%s\n",
                  static_cast<unsigned>(written), static_cast<unsigned>(written / 2),
                  kRtlSampleRateSps, g_iq_rec_frequency_hz,
                  static_cast<unsigned>(g_iq_rec_sf),
                  static_cast<unsigned>(g_iq_rec_bandwidth_hz),
                  g_iq_rec_auto_triggered.load(std::memory_order_relaxed) ? "energy"
                                                                         : "manual");
  }
}

void lora_iq_offer(const uint8_t* iq, size_t bytes) {
  if (iq == nullptr || bytes == 0) return;
  lora_pre_roll_append(iq, bytes);
  if (g_iq_rec_active.load(std::memory_order_relaxed)) {
    iq_rec_append(iq, bytes);
    return;
  }
  if (!lora_detector_enabled.load(std::memory_order_relaxed) ||
      g_iq_rec_ready.load(std::memory_order_relaxed) ||
      g_iq_rec_export_pending.load(std::memory_order_relaxed) ||
      g_iq_rec_export_busy.load(std::memory_order_relaxed) ||
      !iq_rec_ensure_buffers()) return;

  const float level = rtl_signal_dbfs.load(std::memory_order_relaxed);
  if (g_lora_noise_samples == 0) g_lora_noise_floor_dbfs = level;
  if (g_lora_noise_samples < 12) {
    g_lora_noise_floor_dbfs = 0.85f * g_lora_noise_floor_dbfs + 0.15f * level;
    ++g_lora_noise_samples;
    lora_noise_dbfs.store(g_lora_noise_floor_dbfs, std::memory_order_relaxed);
    return;
  }
  const float trigger =
      constrain(g_lora_noise_floor_dbfs + kLoraTriggerMarginDb, -78.0f, -25.0f);
  lora_noise_dbfs.store(g_lora_noise_floor_dbfs, std::memory_order_relaxed);
  lora_trigger_dbfs.store(trigger, std::memory_order_relaxed);
  if (level < trigger - kLoraTriggerHysteresisDb) {
    g_lora_trigger_armed = true;
    g_lora_noise_floor_dbfs = 0.995f * g_lora_noise_floor_dbfs + 0.005f * level;
    return;
  }
  if (!g_lora_trigger_armed || level < trigger) return;

  g_lora_trigger_armed = false;
  const size_t pre_roll = lora_copy_pre_roll();
  lora_rf_events.fetch_add(1, std::memory_order_relaxed);
  iq_rec_begin(true, pre_roll);
  Serial.printf("RTL_LORA_ENERGY level_dbfs=%.1f noise_dbfs=%.1f trigger_dbfs=%.1f preroll_bytes=%u\n",
                static_cast<double>(level), static_cast<double>(g_lora_noise_floor_dbfs),
                static_cast<double>(trigger), static_cast<unsigned>(pre_roll));
}

bool iq_rec_stop_and_export() {
  if (g_iq_rec_export_busy.exchange(true, std::memory_order_acq_rel)) return false;
  const auto finish = [](bool result) {
    g_iq_rec_export_busy.store(false, std::memory_order_release);
    return result;
  };
  g_iq_rec_active.store(false, std::memory_order_release);
  const size_t bytes = g_iq_rec_write.load(std::memory_order_acquire);
  if (g_iq_rec_buf == nullptr || bytes == 0 || !ensure_tab5_sd()) {
    Serial.println("RTL_IQ_ERROR empty_or_sd");
    return finish(false);
  }
  g_sd_fs->mkdir("/orcsdr");
  char path[96];
  do {
    ++g_iq_rec_file_seq;
    snprintf(path, sizeof(path), "/orcsdr/iq_%03u_%lu_sf%u_bw%u.orciq",
             static_cast<unsigned>(g_iq_rec_file_seq),
             static_cast<unsigned long>(g_iq_rec_frequency_hz),
             static_cast<unsigned>(g_iq_rec_sf),
             static_cast<unsigned>(g_iq_rec_bandwidth_hz));
  } while (g_sd_fs->exists(path));
  File file = g_sd_fs->open(path, FILE_WRITE, true);
  if (!file) return finish(false);
  file.write(reinterpret_cast<const uint8_t*>("ORCIQ01\0"), 8);
  write_le32(file, 36);
  write_le32(file, kRtlSampleRateSps);
  write_le32(file, g_iq_rec_frequency_hz);
  write_le32(file, static_cast<uint32_t>(bytes));
  write_le16(file, 1);  // format 1: unsigned 8-bit interleaved I/Q
  const uint8_t sf = g_iq_rec_sf;
  const uint8_t reserved = 0;
  file.write(&sf, 1);
  file.write(&reserved, 1);
  write_le32(file, g_iq_rec_bandwidth_hz);
  write_le32(file, 0);
  const size_t wrote = file.write(g_iq_rec_buf, bytes);
  file.close();
  if (wrote != bytes) {
    Serial.printf("RTL_IQ_ERROR short_write got=%u want=%u\n",
                  static_cast<unsigned>(wrote), static_cast<unsigned>(bytes));
    return finish(false);
  }
  strlcpy(g_iq_rec_last_path, path, sizeof(g_iq_rec_last_path));
  Serial.printf("RTL_IQ_DONE path=\"%s\" bytes=%u samples=%u rate=%u frequency_hz=%u mode=%s\n",
                path, static_cast<unsigned>(bytes), static_cast<unsigned>(bytes / 2),
                kRtlSampleRateSps, g_iq_rec_frequency_hz,
                g_iq_rec_auto_triggered.load(std::memory_order_relaxed) ? "energy" : "manual");
  g_iq_rec_write.store(0, std::memory_order_release);
  g_iq_rec_auto_triggered.store(false, std::memory_order_release);
  return finish(true);
}

bool audio_rec_write_wav(const char* path, const int16_t* pcm, size_t samples) {
  if (path == nullptr || pcm == nullptr || samples == 0) return false;
  if (!ensure_tab5_sd()) return false;
  g_sd_fs->mkdir("/orcsdr");
  // FILE_WRITE appends; sequence numbers restart after boot, so replace collisions.
  if (g_sd_fs->exists(path) && !g_sd_fs->remove(path)) {
    Serial.printf("RTL_REC_WAV_ERR replace path=%s\n", path);
    return false;
  }
  File f = g_sd_fs->open(path, FILE_WRITE, true);
  if (!f) {
    Serial.printf("RTL_REC_WAV_ERR open path=%s\n", path);
    return false;
  }
  const uint32_t data_bytes = static_cast<uint32_t>(samples * sizeof(int16_t));
  const uint32_t byte_rate = kAudioRecRateHz * 2u; /* mono int16 */
  f.write(reinterpret_cast<const uint8_t*>("RIFF"), 4);
  write_le32(f, 36u + data_bytes);
  f.write(reinterpret_cast<const uint8_t*>("WAVE"), 4);
  f.write(reinterpret_cast<const uint8_t*>("fmt "), 4);
  write_le32(f, 16u);
  write_le16(f, 1u); /* PCM */
  write_le16(f, 1u); /* mono */
  write_le32(f, kAudioRecRateHz);
  write_le32(f, byte_rate);
  write_le16(f, 2u); /* block align */
  write_le16(f, 16u); /* bits */
  f.write(reinterpret_cast<const uint8_t*>("data"), 4);
  write_le32(f, data_bytes);
  const size_t wrote =
      f.write(reinterpret_cast<const uint8_t*>(pcm), data_bytes);
  f.close();
  if (wrote != data_bytes) {
    Serial.printf("RTL_REC_WAV_ERR short_write got=%u want=%u\n",
                  static_cast<unsigned>(wrote), static_cast<unsigned>(data_bytes));
    return false;
  }
  return true;
}

bool audio_rec_start() {
  if (rtl_ui_band == RtlBand::lora) {
    Serial.println("RTL_REC_ERROR data_mode_use_RTL_IQ_START");
    return false;
  }
  if (g_audio_rec_active.load(std::memory_order_acquire)) {
    Serial.println("RTL_REC_ALREADY");
    return true;
  }
  if (!audio_rec_ensure_buffer()) return false;
  g_audio_rec_write.store(0, std::memory_order_release);
  g_audio_rec_full.store(false, std::memory_order_release);
  g_audio_rec_band = rtl_ui_band;
  g_audio_rec_freq_hz = rtl_ui_frequency_hz;
  g_audio_rec_last_path[0] = '\0';
  g_audio_rec_active.store(true, std::memory_order_release);
  Serial.printf("RTL_REC_START band=%s frequency_hz=%u rate=%u max_sec=%u\n",
                rtl_band_name(g_audio_rec_band), g_audio_rec_freq_hz, kAudioRecRateHz,
                static_cast<unsigned>(kAudioRecMaxSeconds));
  return true;
}

bool audio_rec_stop_and_export() {
  const bool was = g_audio_rec_active.exchange(false, std::memory_order_acq_rel);
  const size_t samples = g_audio_rec_write.load(std::memory_order_acquire);
  const bool full = g_audio_rec_full.load(std::memory_order_acquire);
  if (!was && samples == 0) {
    Serial.println("RTL_REC_STOP empty");
    return false;
  }
  Serial.printf("RTL_REC_STOP samples=%u sec=%.2f full=%s band=%s frequency_hz=%u\n",
                static_cast<unsigned>(samples),
                static_cast<double>(samples) / static_cast<double>(kAudioRecRateHz),
                full ? "true" : "false", rtl_band_name(g_audio_rec_band),
                g_audio_rec_freq_hz);
  if (samples == 0 || g_audio_rec_buf == nullptr) return false;

  ++g_audio_rec_file_seq;
  char path[64];
  snprintf(path, sizeof(path), "/orcsdr/rec_%03u_%s_%u.wav",
           static_cast<unsigned>(g_audio_rec_file_seq), rtl_band_name(g_audio_rec_band),
           static_cast<unsigned>(g_audio_rec_freq_hz));
  if (audio_rec_write_wav(path, g_audio_rec_buf, samples)) {
    strlcpy(g_audio_rec_last_path, path, sizeof(g_audio_rec_last_path));
    Serial.printf("RTL_REC_WAV ok path=%s samples=%u rate=%u channels=1 bits=16 "
                  "note=post_demod_pcm\n",
                  path, static_cast<unsigned>(samples), kAudioRecRateHz);
    return true;
  }
  /* No SD: keep PCM in PSRAM; operator can re-insert card and RTL_REC_SAVE. */
  Serial.printf("RTL_REC_PSRAM_HOLD samples=%u rate=%u note=insert_sd_then_RTL_REC_SAVE\n",
                static_cast<unsigned>(samples), kAudioRecRateHz);
  return false;
}

void audio_rec_status_print() {
  const size_t samples = g_audio_rec_write.load(std::memory_order_acquire);
  Serial.printf(
      "RTL_REC_STATUS active=%s full=%s samples=%u sec=%.2f max_sec=%u rate=%u "
      "band=%s frequency_hz=%u last_path=\"%s\" sd=%s\n",
      g_audio_rec_active.load(std::memory_order_acquire) ? "true" : "false",
      g_audio_rec_full.load(std::memory_order_acquire) ? "true" : "false",
      static_cast<unsigned>(samples),
      static_cast<double>(samples) / static_cast<double>(kAudioRecRateHz),
      static_cast<unsigned>(kAudioRecMaxSeconds), kAudioRecRateHz,
      rtl_band_name(g_audio_rec_band), g_audio_rec_freq_hz,
      g_audio_rec_last_path[0] ? g_audio_rec_last_path : "none",
      g_sd_ready ? "ready" : (g_sd_tried ? "missing" : "untried"));
}

bool rds_capture_start() {
  if (rtl_ui_band != RtlBand::fm) {
    Serial.println("RTL_RDS_CAPTURE_ERROR fm_mode_required");
    return false;
  }
  if (g_rds_capture_active.load(std::memory_order_acquire)) {
    Serial.println("RTL_RDS_CAPTURE_ALREADY");
    return true;
  }
  if (g_rds_capture_buf == nullptr) {
    g_rds_capture_buf = static_cast<int16_t*>(heap_caps_malloc(
        kRdsCaptureSamples * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (g_rds_capture_buf == nullptr) {
    Serial.println("RTL_RDS_CAPTURE_ERROR no_psram_buffer");
    return false;
  }
  g_rds_capture_frequency_hz = rtl_ui_frequency_hz;
  g_rds_capture_started_ms = millis();
  g_rds_capture_last_path[0] = '\0';
  g_rds_capture_write.store(0, std::memory_order_release);
  g_rds_capture_ready.store(false, std::memory_order_release);
  g_rds_capture_active.store(true, std::memory_order_release);
  Serial.printf("RTL_RDS_CAPTURE_START samples=%u seconds=%u rate=%u frequency_hz=%u\n",
                static_cast<unsigned>(kRdsCaptureSamples),
                static_cast<unsigned>(kRdsCaptureSeconds), kRdsMpxRateHz,
                g_rds_capture_frequency_hz);
  return true;
}

bool rds_capture_write_files(size_t samples) {
  if (g_rds_capture_buf == nullptr || samples == 0 || !ensure_tab5_sd()) return false;
  g_sd_fs->mkdir("/orcsdr");
  g_sd_fs->mkdir("/orcsdr/rds_debug");
  char raw_path[96];
  char json_path[96];
  do {
    ++g_rds_capture_file_seq;
    snprintf(raw_path, sizeof(raw_path), "/orcsdr/rds_debug/%03u_%lu_mpx.s16",
              static_cast<unsigned>(g_rds_capture_file_seq),
              static_cast<unsigned long>(g_rds_capture_frequency_hz));
  } while (g_sd_fs->exists(raw_path));
  snprintf(json_path, sizeof(json_path), "/orcsdr/rds_debug/%03u_%lu_mpx.json",
            static_cast<unsigned>(g_rds_capture_file_seq),
            static_cast<unsigned long>(g_rds_capture_frequency_hz));

  File raw = g_sd_fs->open(raw_path, FILE_WRITE, true);
  if (!raw) {
    Serial.println("RTL_RDS_CAPTURE_ERROR raw_open_failed");
    return false;
  }
  const size_t bytes = samples * sizeof(int16_t);
  const size_t wrote = raw.write(reinterpret_cast<const uint8_t*>(g_rds_capture_buf), bytes);
  raw.close();
  if (wrote != bytes) {
    g_sd_fs->remove(raw_path);
    Serial.printf("RTL_RDS_CAPTURE_ERROR short_write got=%u want=%u\n",
                  static_cast<unsigned>(wrote), static_cast<unsigned>(bytes));
    return false;
  }

  File metadata = g_sd_fs->open(json_path, FILE_WRITE, true);
  if (!metadata) {
    Serial.printf("RTL_RDS_CAPTURE_ERROR metadata_open_failed raw=\"%s\"\n", raw_path);
    return false;
  }
  metadata.printf(
      "{\"format\":\"s16le_mpx\",\"sample_rate_hz\":%lu,\"frequency_hz\":%lu,"
      "\"samples\":%u,\"scale_radians_per_lsb\":%.10g,\"start_uptime_ms\":%lu}\n",
      static_cast<unsigned long>(kRdsMpxRateHz),
      static_cast<unsigned long>(g_rds_capture_frequency_hz), static_cast<unsigned>(samples),
      static_cast<double>(kRdsInt16ToMpx), static_cast<unsigned long>(g_rds_capture_started_ms));
  metadata.close();
  strlcpy(g_rds_capture_last_path, raw_path, sizeof(g_rds_capture_last_path));
  Serial.printf("RTL_RDS_CAPTURE_SAVED path=\"%s\" metadata=\"%s\" samples=%u rate=%u\n",
                raw_path, json_path, static_cast<unsigned>(samples), kRdsMpxRateHz);
  return true;
}

bool rds_capture_stop_and_export() {
  g_rds_capture_active.store(false, std::memory_order_release);
  while (g_rds_capture_writing.load(std::memory_order_acquire)) delay(1);
  const size_t samples = g_rds_capture_write.load(std::memory_order_acquire);
  if (samples == 0) {
    Serial.println("RTL_RDS_CAPTURE_STOP empty");
    return false;
  }
  g_rds_capture_ready.store(true, std::memory_order_release);
  Serial.printf("RTL_RDS_CAPTURE_STOP samples=%u seconds=%.3f\n",
                static_cast<unsigned>(samples),
                static_cast<double>(samples) / static_cast<double>(kRdsMpxRateHz));
  if (rds_capture_write_files(samples)) return true;
  Serial.printf("RTL_RDS_CAPTURE_PSRAM_HOLD samples=%u note=insert_sd_then_stop_again\n",
                static_cast<unsigned>(samples));
  return false;
}

void rds_capture_status_print() {
  const size_t samples = g_rds_capture_write.load(std::memory_order_acquire);
  Serial.printf(
      "RTL_RDS_CAPTURE_STATUS active=%s ready=%s samples=%u seconds=%.3f max_seconds=%u "
      "rate=%u frequency_hz=%u last_path=\"%s\" sd=%s\n",
      g_rds_capture_active.load(std::memory_order_acquire) ? "true" : "false",
      g_rds_capture_ready.load(std::memory_order_acquire) ? "true" : "false",
      static_cast<unsigned>(samples),
      static_cast<double>(samples) / static_cast<double>(kRdsMpxRateHz),
      static_cast<unsigned>(kRdsCaptureSeconds), kRdsMpxRateHz,
      g_rds_capture_frequency_hz,
      g_rds_capture_last_path[0] ? g_rds_capture_last_path : "none",
      g_sd_ready ? "ready" : (g_sd_tried ? "missing" : "untried"));
}

bool rds_replay(const char* path) {
  const size_t path_len = path == nullptr ? 0 : strlen(path);
  if (!sd_put_path_allowed(path) || path_len < 4 || strcmp(path + path_len - 4, ".s16") != 0) {
    Serial.println("RTL_RDS_REPLAY_ERROR invalid_path");
    return false;
  }
  if (rtl_capture_state.load(std::memory_order_acquire) == RtlCaptureState::running) {
    Serial.println("RTL_RDS_REPLAY_ERROR radio_busy");
    return false;
  }
  if (!ensure_tab5_sd()) {
    Serial.println("RTL_RDS_REPLAY_ERROR sd_unavailable");
    return false;
  }
  File file = g_sd_fs->open(path, FILE_READ);
  if (!file || file.isDirectory() || file.size() == 0 || (file.size() & 1u) != 0) {
    if (file) file.close();
    Serial.println("RTL_RDS_REPLAY_ERROR invalid_file");
    return false;
  }

  rtl_rds_reset();
  uint32_t samples = 0;
  int16_t chunk[512];
  while (file.available()) {
    const size_t bytes = file.read(reinterpret_cast<uint8_t*>(chunk), sizeof(chunk));
    if ((bytes & 1u) != 0) {
      file.close();
      Serial.println("RTL_RDS_REPLAY_ERROR short_sample");
      return false;
    }
    const size_t count = bytes / sizeof(int16_t);
    for (size_t index = 0; index < count; ++index) {
      rds_process_mpx_sample(static_cast<float>(chunk[index]) * kRdsInt16ToMpx);
    }
    samples += static_cast<uint32_t>(count);
  }
  file.close();
  rds_publish_state();
  Serial.printf("RTL_RDS_REPLAY_DONE path=\"%s\" samples=%u seconds=%.3f\n", path,
                samples, static_cast<double>(samples) / static_cast<double>(kRdsMpxRateHz));
  return true;
}

void spectrum_offer_iq_snapshot(const uint8_t* iq, size_t bytes) {
  if (iq == nullptr || bytes < kRtlSpectrumBins * 2) return;
  if (!rtl_graphics_enabled.load(std::memory_order_relaxed)) return;
  const size_t need = sizeof(rtl_spectrum_iq_snap);
  const size_t n = bytes < need ? bytes : need;
  portENTER_CRITICAL(&rtl_spectrum_snap_mux);
  memcpy(rtl_spectrum_iq_snap, iq, n);
  rtl_spectrum_iq_snap_bytes = n;
  portEXIT_CRITICAL(&rtl_spectrum_snap_mux);
}

const char* orc_tool_name(OrcTool tool) {
  switch (tool) {
    case OrcTool::Scope: return "SCOPE";
    case OrcTool::Capture: return "CAPTURE";
    case OrcTool::Radio:
    default: return "RADIO";
  }
}

OrcTool orc_tool_current() {
  const uint8_t v = g_orc_tool.load(std::memory_order_acquire);
  if (v >= static_cast<uint8_t>(OrcTool::Count)) return OrcTool::Radio;
  return static_cast<OrcTool>(v);
}

void set_orc_tool(OrcTool tool) {
  g_orc_tool.store(static_cast<uint8_t>(tool), std::memory_order_release);
  Serial.printf("RTL_TOOL %s\n", orc_tool_name(tool));
  /* Scope tool wants live graphics; Capture can keep audio-only GFX off. */
  if (tool == OrcTool::Scope) {
    rtl_graphics_enabled.store(true, std::memory_order_release);
    reset_spectrum_renderer();
  }
  if (rtl_ui_active.load(std::memory_order_acquire)) {
    draw_tool_tabs();
    if (tool == OrcTool::Capture) draw_capture_tool_panel();
    else if (tool == OrcTool::Scope &&
             !rtl_graphics_enabled.load(std::memory_order_acquire)) {
      /* should not happen */
    }
    const bool running =
        rtl_capture_state.load(std::memory_order_acquire) == RtlCaptureState::running;
    draw_sdr_controls(rtl_ui_band, running);
  }
}

void draw_lora_view_tabs() {
  static constexpr const char* kLabels[] = {"LIVE", "MESSAGES", "MAP"};
  constexpr int x0 = 520;
  constexpr int width = 160;
  constexpr int gap = 10;
  M5.Display.fillRect(510, kToolTabY - 2, 520, kToolTabH + 4, TFT_BLACK);
  for (uint8_t index = 0; index < 3; ++index) {
    const int x = x0 + index * (width + gap);
    const bool selected = static_cast<uint8_t>(lora_view) == index;
    const uint32_t fill = selected ? TFT_DARKCYAN : 0x2104;
    M5.Display.fillRoundRect(x, kToolTabY, width, kToolTabH, 8, fill);
    M5.Display.drawRoundRect(x, kToolTabY, width, kToolTabH, 8,
                             selected ? TFT_CYAN : TFT_DARKGREY);
    M5.Display.setTextDatum(middle_center);
    M5.Display.setTextSize(3);
    M5.Display.setTextColor(TFT_WHITE, fill);
    M5.Display.drawString(kLabels[index], x + width / 2, kToolTabY + kToolTabH / 2);
  }
}

void draw_tool_tabs() {
  static const char* kLabels[] = {"RADIO", "SCOPE", "CAPTURE"};
  const OrcTool cur = orc_tool_current();
  M5.Display.fillRect(0, kToolTabY - 2, 1280, kToolTabH + 4, TFT_BLACK);
  int x = kSdrEdge;
  for (uint8_t i = 0; i < static_cast<uint8_t>(OrcTool::Count); ++i) {
    const bool on = static_cast<OrcTool>(i) == cur;
    const uint32_t fill = on ? TFT_DARKGREEN : TFT_DARKGREY;
    M5.Display.fillRoundRect(x, kToolTabY, kToolTabW, kToolTabH, 8, fill);
    M5.Display.drawRoundRect(x, kToolTabY, kToolTabW, kToolTabH, 8, TFT_WHITE);
    M5.Display.setTextDatum(middle_center);
    M5.Display.setTextSize(3);
    M5.Display.setTextColor(TFT_WHITE, fill);
    M5.Display.drawString(kLabels[i], x + kToolTabW / 2, kToolTabY + kToolTabH / 2);
    x += kToolTabW + kToolTabGap;
  }
  const uint32_t toggle_color = rtl_nav_open ? TFT_MAROON : TFT_DARKCYAN;
  M5.Display.fillRoundRect(kPinchToggleX, kPinchToggleY, kPinchToggleW,
                           kPinchToggleH, 8, toggle_color);
  M5.Display.drawRoundRect(kPinchToggleX, kPinchToggleY, kPinchToggleW,
                           kPinchToggleH, 8, TFT_WHITE);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(3);
  M5.Display.setTextColor(TFT_WHITE, toggle_color);
  M5.Display.drawString(rtl_nav_open ? "CLOSE" : "NAV",
                        kPinchToggleX + kPinchToggleW / 2,
                        kPinchToggleY + kPinchToggleH / 2);
  if (rtl_ui_band == RtlBand::lora && !rtl_nav_open) draw_lora_view_tabs();
  else draw_rf_band_guide(rtl_ui_frequency_hz);
  if (rtl_nav_open) draw_nav_panel();
}

const RfBandGuide* rf_band_guide_at(uint32_t frequency_hz) {
  for (const auto& entry : kRfBandGuide) {
    if (frequency_hz >= entry.low_hz && frequency_hz <= entry.high_hz) return &entry;
  }
  return nullptr;
}

const RfBandGuide* rf_quick_band_at(size_t wanted) {
  size_t found = 0;
  for (const auto& entry : kRfBandGuide) {
    if (!entry.quick) continue;
    if (found++ == wanted) return &entry;
  }
  return nullptr;
}

const char* rf_region_name(uint32_t frequency_hz) {
  if (frequency_hz < 30000000) return "HF";
  if (frequency_hz < 300000000) return "VHF";
  if (frequency_hz < 1000000000) return "UHF";
  return "L-BAND";
}

void draw_rf_band_guide(uint32_t frequency_hz) {
  const RfBandGuide* entry = rf_band_guide_at(frequency_hz);
  char text[96];
  if (entry != nullptr) snprintf(text, sizeof(text), "%s | %s", entry->label, entry->description);
  else snprintf(text, sizeof(text), "%s - mixed or unlisted services (US guide)",
                rf_region_name(frequency_hz));
  M5.Display.fillRect(520, kToolTabY - 2, 510, kToolTabH + 4, TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(entry != nullptr ? TFT_YELLOW : TFT_LIGHTGREY, TFT_BLACK);
  M5.Display.drawString(text, 775, kToolTabY + kToolTabH / 2);
}

bool handle_tool_tab_touch(int32_t x, int32_t y) {
  if (x >= kPinchToggleX && x < kPinchToggleX + kPinchToggleW &&
      y >= kPinchToggleY && y < kPinchToggleY + kPinchToggleH) {
    rtl_nav_open = !rtl_nav_open;
    rtl_nav_dropdown = SdrNavDropdown::None;
    rtl_frequency_keypad_open = false;
    if (rtl_nav_open) {
      M5.Display.setScrollRect(kSpectrumX + 1, kWaterfallY + 1,
                               spectrum_draw_width() - 2, kWaterfallHeight - 2,
                               TFT_BLACK);
      draw_spectrum_axis();
      draw_tool_tabs();
    }
    else draw_sdr_screen(rtl_ui_band, rtl_ui_frequency_hz, rtl_ui_volume);
    return true;
  }
  if (y < kToolTabY || y >= kToolTabY + kToolTabH) return false;
  if (rtl_ui_band == RtlBand::lora && x >= 520 && x < 1020) {
    const int index = (x - 520) / 170;
    if (index >= 0 && index < 3 && (x - 520) % 170 < 160) {
      lora_view = static_cast<LoraView>(index);
      draw_sdr_screen(rtl_ui_band, rtl_ui_frequency_hz, rtl_ui_volume);
      return true;
    }
  }
  int tab_x = kSdrEdge;
  for (uint8_t i = 0; i < static_cast<uint8_t>(OrcTool::Count); ++i) {
    if (x >= tab_x && x < tab_x + kToolTabW) {
      set_orc_tool(static_cast<OrcTool>(i));
      return true;
    }
    tab_x += kToolTabW + kToolTabGap;
  }
  return false;
}

void draw_nav_panel() {
  auto button = [](int x, int y, int w, int h, const char* text, uint32_t color,
                   uint8_t size = 2) {
    M5.Display.fillRoundRect(x, y, w, h, 8, color);
    M5.Display.drawRoundRect(x, y, w, h, 8, TFT_WHITE);
    M5.Display.setTextDatum(middle_center);
    M5.Display.setTextSize(size);
    M5.Display.setTextColor(TFT_WHITE, color);
    M5.Display.drawString(text, x + w / 2, y + h / 2);
  };

  M5.Display.fillRoundRect(kNavPanelX, kNavPanelY, kNavPanelW, kNavPanelH, 12,
                           TFT_BLACK);
  M5.Display.drawRoundRect(kNavPanelX, kNavPanelY, kNavPanelW, kNavPanelH, 12,
                           TFT_CYAN);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(3);
  M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
  M5.Display.drawString(rtl_frequency_keypad_open ? "DIRECT FREQUENCY" : "NAVIGATION",
                        kNavPanelX + kNavPanelW / 2, kNavPanelY + 25);

  if (rtl_frequency_keypad_open) {
    char field[32];
    snprintf(field, sizeof(field), "%s%s", rtl_frequency_entry,
             rtl_ui_band == RtlBand::am ? " kHz" : " MHz");
    button(780, 145, 416, 54, field, TFT_NAVY, 3);
    static const char* keys[] = {"1", "2", "3", "4", "5", "6",
                                 "7", "8", "9", ".", "0", "<"};
    for (int index = 0; index < 12; ++index) {
      const int col = index % 3;
      const int row = index / 3;
      button(780 + col * 138, 211 + row * 66, 128, 56, keys[index], TFT_DARKGREY, 3);
    }
    button(780, 481, 200, 56, "CANCEL", TFT_MAROON, 3);
    button(996, 481, 200, 56, "TUNE", TFT_DARKGREEN, 3);
    return;
  }

  char label[40];
  button(780, 145, 416, 48, "DIRECT FREQUENCY", TFT_NAVY, 3);
  button(780, 205, 416, 48, "US BAND GUIDE  v", TFT_DARKGREEN, 3);
  if (rtl_nav_dropdown == SdrNavDropdown::Band) {
    for (size_t index = 0; index < std::size(kRfQuickLabels); ++index) {
      button(780 + (index % 2) * 216, 265 + (index / 2) * 45, 200, 39,
             kRfQuickLabels[index], TFT_DARKGREY, 2);
    }
    return;
  }
  snprintf(label, sizeof(label), "PINCH: %s  v",
           rtl_pinch_mode == SdrPinchMode::Span ? "SPAN" : "FILTER");
  button(780, 265, 416, 48, label, TFT_DARKCYAN, 3);
  const uint32_t step = rtl_ui_band == RtlBand::am ? rtl_am_step_hz : rtl_fm_step_hz;
  snprintf(label, sizeof(label), "STEP: %lu kHz  v",
           static_cast<unsigned long>(step / 1000u));
  button(780, 325, 416, 48, label, TFT_DARKCYAN, 3);

  if (rtl_nav_dropdown == SdrNavDropdown::Pinch) {
    button(780, 385, 200, 58, "SPAN", TFT_NAVY, 3);
    button(996, 385, 200, 58, "FILTER", TFT_DARKCYAN, 3);
    return;
  }
  if (rtl_nav_dropdown == SdrNavDropdown::Step) {
    static const uint32_t steps[] = {1000, 5000, 10000, 50000, 100000, 1000000};
    for (int index = 0; index < 6; ++index) {
      snprintf(label, sizeof(label), "%lu kHz",
               static_cast<unsigned long>(steps[index] / 1000u));
      button(780 + (index % 2) * 216, 385 + (index / 2) * 54, 200, 48, label,
             TFT_DARKGREY, 3);
    }
    return;
  }

  button(780, 385, 128, 54, "ZOOM IN", TFT_DARKGREY, 2);
  button(924, 385, 128, 54, "RESET", TFT_DARKGREY, 3);
  button(1068, 385, 128, 54, "ZOOM OUT", TFT_DARKGREY, 2);
  button(780, 451, 128, 54, "PEAK", TFT_DARKCYAN, 3);
  button(924, 451, 128, 54, "AUTO FM", TFT_DARKCYAN, 2);
  button(1068, 451, 128, 54, "CENTER", TFT_DARKCYAN, 2);
  button(780, 514, 416, 32, "HOME", TFT_DARKGREEN, 2);
}

bool handle_nav_touch(int32_t x, int32_t y) {
  auto hit = [x, y](int bx, int by, int bw, int bh) {
    return x >= bx && x < bx + bw && y >= by && y < by + bh;
  };
  if (!rtl_nav_open || !hit(kNavPanelX, kNavPanelY, kNavPanelW, kNavPanelH)) return false;

  if (rtl_frequency_keypad_open) {
    if (hit(780, 481, 200, 56)) {
      rtl_frequency_keypad_open = false;
      rtl_frequency_entry[0] = '\0';
      draw_nav_panel();
      return true;
    }
    if (hit(996, 481, 200, 56)) {
      char* end = nullptr;
      const double entered = strtod(rtl_frequency_entry, &end);
      if (end != rtl_frequency_entry && entered > 0.0) {
        const double scale = rtl_ui_band == RtlBand::am ? 1000.0 : 1000000.0;
        const double requested_hz = entered * scale;
        const uint32_t band_max = rtl_ui_band == RtlBand::am
                                      ? kRtlAmMaxHz
                                      : rtl_ui_band == RtlBand::wx ? kRtlWxHz
                                      : rtl_ui_band == RtlBand::browse
                                          ? kRtlBrowseMaxHz
                                          : kRtlFmMaxHz;
        const uint32_t frequency = rtl_clamp_frequency(
            rtl_ui_band, requested_hz >= static_cast<double>(band_max)
                             ? band_max
                             : static_cast<uint32_t>(requested_hz));
        rtl_nav_open = false;
        rtl_frequency_keypad_open = false;
        const RtlCaptureState state = rtl_capture_state.load(std::memory_order_acquire);
        draw_sdr_screen(rtl_ui_band, frequency, rtl_ui_volume);
        if (state == RtlCaptureState::running) request_hot_retune(frequency);
        else queue_local_rtl_listen(rtl_ui_band, frequency);
      }
      return true;
    }
    static const char keys[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '.', '0', '\b'};
    for (int index = 0; index < 12; ++index) {
      if (!hit(780 + (index % 3) * 138, 211 + (index / 3) * 66, 128, 56)) continue;
      const size_t length = strlen(rtl_frequency_entry);
      if (keys[index] == '\b') {
        if (length > 0) rtl_frequency_entry[length - 1] = '\0';
      } else if (length + 1 < sizeof(rtl_frequency_entry) &&
                 (keys[index] != '.' || strchr(rtl_frequency_entry, '.') == nullptr)) {
        rtl_frequency_entry[length] = keys[index];
        rtl_frequency_entry[length + 1] = '\0';
      }
      draw_nav_panel();
      return true;
    }
    return true;
  }

  if (rtl_nav_dropdown == SdrNavDropdown::Band) {
    for (size_t index = 0; index < std::size(kRfQuickLabels); ++index) {
      if (!hit(780 + (index % 2) * 216, 265 + (index / 2) * 45, 200, 39)) continue;
      const RfBandGuide* entry = rf_quick_band_at(index);
      if (entry == nullptr) break;
      rtl_nav_open = false;
      rtl_nav_dropdown = SdrNavDropdown::None;
      const uint32_t preset_hz = entry->mode == RtlBand::p25
                                     ? p25_control_frequency_hz
                                     : entry->preset_hz;
      const RtlCaptureState state = rtl_capture_state.load(std::memory_order_acquire);
      if (state == RtlCaptureState::running && rtl_ui_band == entry->mode) {
        request_hot_retune(preset_hz);
      } else {
        queue_local_rtl_listen(entry->mode, preset_hz);
      }
      draw_sdr_screen(rtl_ui_band, rtl_ui_frequency_hz, rtl_ui_volume);
      return true;
    }
    return true;
  }
  if (rtl_nav_dropdown == SdrNavDropdown::Pinch) {
    if (hit(780, 385, 200, 58)) rtl_pinch_mode = SdrPinchMode::Span;
    else if (hit(996, 385, 200, 58)) rtl_pinch_mode = SdrPinchMode::Filter;
    rtl_nav_dropdown = SdrNavDropdown::None;
    draw_nav_panel();
    return true;
  }
  if (rtl_nav_dropdown == SdrNavDropdown::Step) {
    static const uint32_t steps[] = {1000, 5000, 10000, 50000, 100000, 1000000};
    for (int index = 0; index < 6; ++index) {
      if (!hit(780 + (index % 2) * 216, 385 + (index / 2) * 54, 200, 48)) continue;
      if (rtl_ui_band == RtlBand::am) rtl_am_step_hz = steps[index];
      else rtl_fm_step_hz = steps[index];
      break;
    }
    rtl_nav_dropdown = SdrNavDropdown::None;
    draw_nav_panel();
    return true;
  }
  if (hit(780, 514, 416, 32)) {
    rtl_nav_open = false;
    show_home();
    return true;
  }
  if (hit(780, 145, 416, 48)) {
    rtl_frequency_keypad_open = true;
    rtl_frequency_entry[0] = '\0';
    draw_nav_panel();
  } else if (hit(780, 205, 416, 48)) {
    rtl_nav_dropdown = SdrNavDropdown::Band;
    draw_nav_panel();
  } else if (hit(780, 265, 416, 48)) {
    rtl_nav_dropdown = SdrNavDropdown::Pinch;
    draw_nav_panel();
  } else if (hit(780, 325, 416, 48)) {
    rtl_nav_dropdown = SdrNavDropdown::Step;
    draw_nav_panel();
  } else if (hit(780, 385, 128, 54) || hit(924, 385, 128, 54) ||
             hit(1068, 385, 128, 54)) {
    const uint32_t current = rtl_scope_span_hz.load(std::memory_order_relaxed);
    uint32_t next = kRtlScopeSpanMaxHz;
    if (hit(780, 385, 128, 54)) next = max(kRtlScopeSpanMinHz, current / 2);
    else if (hit(1068, 385, 128, 54)) next = min(kRtlScopeSpanMaxHz, current * 2);
    rtl_scope_span_hz.store(next, std::memory_order_relaxed);
    redraw_spectrum_panel();
    draw_spectrum_axis();
    draw_nav_panel();
  } else if (hit(780, 451, 128, 54)) {
    if (rtl_ui_band == RtlBand::wx) return true;
    const int64_t target = static_cast<int64_t>(rtl_ui_frequency_hz) +
                           rtl_scope_peak_offset_hz.load(std::memory_order_relaxed);
    const uint32_t frequency = rtl_clamp_frequency(
        rtl_ui_band, target > 0 ? static_cast<uint32_t>(target) : 0u);
    rtl_nav_open = false;
    const RtlCaptureState state = rtl_capture_state.load(std::memory_order_acquire);
    if (state == RtlCaptureState::running) request_hot_retune(frequency);
    else queue_local_rtl_listen(rtl_ui_band, frequency);
    draw_sdr_screen(rtl_ui_band, frequency, rtl_ui_volume);
  } else if (hit(924, 451, 128, 54)) {
    rtl_nav_open = false;
    rtl_graphics_enabled.store(true, std::memory_order_release);
    rtl_auto_fm_requested.store(true, std::memory_order_release);
    rtl_auto_fm_active.store(true, std::memory_order_release);
    const RtlCaptureState state = rtl_capture_state.load(std::memory_order_acquire);
    if (rtl_ui_band != RtlBand::fm || state != RtlCaptureState::running) {
      queue_local_rtl_listen(RtlBand::fm, kRtlFmMinHz + kRtlFmAutoStepHz / 2);
    } else {
      draw_sdr_screen(rtl_ui_band, rtl_ui_frequency_hz, rtl_ui_volume);
    }
  } else if (hit(1068, 451, 128, 54)) {
    if (rtl_ui_band != RtlBand::wx) {
      const uint32_t step = rtl_ui_band == RtlBand::am ? rtl_am_step_hz : rtl_fm_step_hz;
      const uint32_t frequency = rtl_clamp_frequency(
          rtl_ui_band, ((rtl_ui_frequency_hz + step / 2) / step) * step);
      const RtlCaptureState state = rtl_capture_state.load(std::memory_order_acquire);
      if (state == RtlCaptureState::running) request_hot_retune(frequency);
      else queue_local_rtl_listen(rtl_ui_band, frequency);
    }
    rtl_nav_open = false;
    draw_sdr_screen(rtl_ui_band, rtl_ui_frequency_hz, rtl_ui_volume);
  }
  return true;
}

bool sd_put_path_allowed(const char* path) {
  if (path == nullptr || strncmp(path, "/orcsdr/", 8) != 0 ||
      strlen(path) >= sizeof(g_sd_put.target) || strstr(path, "..") != nullptr ||
      strchr(path, '\\') != nullptr) {
    return false;
  }
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(path); *p; ++p) {
    if (*p < 0x20 || *p > 0x7e) return false;
  }
  return path[8] != '\0';
}

bool sd_remove_path_allowed(const char* path) {
  return sd_put_path_allowed(path) ||
         strcmp(path, "/OrcSDR_Splash_1280x720_60fps_10s.orsplash") == 0;
}

bool sd_transfer_radio_busy() {
  return rtl_capture_state.load(std::memory_order_acquire) == RtlCaptureState::running ||
         g_audio_rec_active.load(std::memory_order_acquire);
}

void sd_list() {
  if (sd_transfer_radio_busy()) {
    Serial.println("SD_LIST_ERROR radio_busy");
    return;
  }
  if (g_sd_put.active || g_sd_get.active) {
    Serial.println("SD_LIST_ERROR transfer_busy");
    return;
  }
  if (orcsdr_splash_is_active()) orcsdr_splash_end();
  if (!ensure_tab5_sd()) {
    Serial.println("SD_LIST_ERROR sd_unavailable");
    return;
  }
  File directory = g_sd_fs->open("/orcsdr");
  if (!directory || !directory.isDirectory()) {
    Serial.println("SD_LIST_ERROR open_failed");
    return;
  }
  size_t count = 0;
  for (File entry = directory.openNextFile(); entry; entry = directory.openNextFile()) {
    if (!entry.isDirectory()) {
      char path[128];
      const char* name = entry.name();
      if (name[0] == '/') strlcpy(path, name, sizeof(path));
      else snprintf(path, sizeof(path), "/orcsdr/%s", name);
      if (strlen(path) < sizeof(path) - 1) {
        Serial.printf("SD_LIST_ENTRY bytes=%llu modified=%llu pathhex=",
                      static_cast<unsigned long long>(entry.size()),
                      static_cast<unsigned long long>(entry.getLastWrite()));
        print_hex(reinterpret_cast<const uint8_t*>(path), strlen(path));
        Serial.println();
        ++count;
      }
    }
    entry.close();
  }
  directory.close();
  Serial.printf("SD_LIST_DONE count=%u\n", static_cast<unsigned>(count));
}

void reset_sd_get_state() {
  g_sd_get.active = false;
  g_sd_get.size = 0;
  g_sd_get.sent = 0;
  g_sd_get.path[0] = '\0';
}

void reset_sd_put_state() {
  g_sd_put.active = false;
  g_sd_put.expected = 0;
  g_sd_put.received = 0;
  g_sd_put.target[0] = '\0';
  g_sd_put.temporary[0] = '\0';
  std::memset(g_sd_put.expected_sha, 0, sizeof(g_sd_put.expected_sha));
}

void sd_get_abort(const char* reason) {
  if (g_sd_get.file) g_sd_get.file.close();
  if (g_sd_get.active) mbedtls_sha256_free(&g_sd_get.sha);
  reset_sd_get_state();
  Serial.printf("SD_GET_ERROR %s\n", reason ? reason : "aborted");
  g_sd_transfer_active.store(false, std::memory_order_release);
}

void sd_get_begin(const char* path_hex) {
  if (g_sd_get.active || g_sd_put.active) {
    Serial.println("SD_GET_ERROR transfer_busy");
    return;
  }
  g_sd_transfer_active.store(true, std::memory_order_release);
  if (sd_transfer_radio_busy()) {
    Serial.println("SD_GET_ERROR radio_busy");
    g_sd_transfer_active.store(false, std::memory_order_release);
    return;
  }
  char path[sizeof(g_sd_get.path)];
  if (!decode_hex_text(path_hex, path, sizeof(path)) || !sd_put_path_allowed(path)) {
    Serial.println("SD_GET_ERROR invalid_path");
    g_sd_transfer_active.store(false, std::memory_order_release);
    return;
  }
  if (orcsdr_splash_is_active()) orcsdr_splash_end();
  if (!ensure_tab5_sd()) {
    Serial.println("SD_GET_ERROR sd_unavailable");
    g_sd_transfer_active.store(false, std::memory_order_release);
    return;
  }
  g_sd_get.file = g_sd_fs->open(path, FILE_READ);
  if (!g_sd_get.file || g_sd_get.file.isDirectory()) {
    if (g_sd_get.file) g_sd_get.file.close();
    reset_sd_get_state();
    Serial.println("SD_GET_ERROR open_failed");
    g_sd_transfer_active.store(false, std::memory_order_release);
    return;
  }
  g_sd_get.size = g_sd_get.file.size();
  if (g_sd_get.size == 0 || g_sd_get.size > kSdPutMaxBytes) {
    g_sd_get.file.close();
    reset_sd_get_state();
    Serial.println("SD_GET_ERROR invalid_size");
    g_sd_transfer_active.store(false, std::memory_order_release);
    return;
  }
  strlcpy(g_sd_get.path, path, sizeof(g_sd_get.path));
  mbedtls_sha256_init(&g_sd_get.sha);
  if (mbedtls_sha256_starts(&g_sd_get.sha, 0) != 0) {
    g_sd_get.active = true;
    sd_get_abort("sha_start");
    return;
  }
  g_sd_get.active = true;
  Serial.printf("SD_GET_READY chunk=%u bytes=%llu path=\"%s\"\n",
                static_cast<unsigned>(kSdGetChunkBytes),
                static_cast<unsigned long long>(g_sd_get.size), g_sd_get.path);
}

void sd_get_chunk() {
  if (!g_sd_get.active) {
    Serial.println("SD_GET_ERROR not_active");
    return;
  }
  const uint64_t remaining = g_sd_get.size - g_sd_get.sent;
  const size_t wanted = static_cast<size_t>(
      remaining < kSdGetChunkBytes ? remaining : kSdGetChunkBytes);
  const size_t got = g_sd_get.file.read(g_sd_put_chunk, wanted);
  if (got != wanted || mbedtls_sha256_update(&g_sd_get.sha, g_sd_put_chunk, got) != 0) {
    sd_get_abort("read_failed");
    return;
  }
  Serial.printf("SD_GET_DATA bytes=%u\n", static_cast<unsigned>(got));
  if (Serial.writeBytes(g_sd_put_chunk, got) != got) {
    sd_get_abort("serial_write");
    return;
  }
  g_sd_get.sent += got;
  if (g_sd_get.sent != g_sd_get.size) return;

  uint8_t digest[32];
  if (mbedtls_sha256_finish(&g_sd_get.sha, digest) != 0) {
    sd_get_abort("sha_finish");
    return;
  }
  mbedtls_sha256_free(&g_sd_get.sha);
  g_sd_get.file.close();
  g_sd_get.active = false;
  Serial.printf("SD_GET_DONE bytes=%llu sha256=",
                static_cast<unsigned long long>(g_sd_get.sent));
  print_hex(digest, sizeof(digest));
  Serial.printf(" path=\"%s\"\n", g_sd_get.path);
  reset_sd_get_state();
  g_sd_transfer_active.store(false, std::memory_order_release);
}

void iq_get_reset() {
  if (g_iq_get.active) mbedtls_sha256_free(&g_iq_get.sha);
  g_iq_get = {};
}

void iq_get_abort(const char* reason) {
  iq_get_reset();
  Serial.printf("RTL_IQ_GET_ERROR %s\n", reason ? reason : "aborted");
}

void iq_get_begin() {
  if (g_iq_get.active) {
    Serial.println("RTL_IQ_GET_ERROR already_active");
    return;
  }
  const size_t bytes = g_iq_rec_write.load(std::memory_order_acquire);
  if (!g_iq_rec_ready.load(std::memory_order_acquire) || g_iq_rec_buf == nullptr ||
      bytes == 0) {
    Serial.println("RTL_IQ_GET_ERROR capture_not_ready");
    return;
  }
  mbedtls_sha256_init(&g_iq_get.sha);
  if (mbedtls_sha256_starts(&g_iq_get.sha, 0) != 0) {
    g_iq_get.active = true;
    iq_get_abort("sha_start");
    return;
  }
  g_iq_get.active = true;
  g_iq_get.sent = 0;
  Serial.printf("RTL_IQ_GET_READY chunk=%u bytes=%u\n",
                static_cast<unsigned>(kSdGetChunkBytes), static_cast<unsigned>(bytes));
}

void iq_get_chunk() {
  if (!g_iq_get.active) {
    Serial.println("RTL_IQ_GET_ERROR not_active");
    return;
  }
  const size_t total = g_iq_rec_write.load(std::memory_order_acquire);
  const size_t remaining = total - g_iq_get.sent;
  const size_t count = min(remaining, kSdGetChunkBytes);
  const uint8_t* data = g_iq_rec_buf + g_iq_get.sent;
  if (mbedtls_sha256_update(&g_iq_get.sha, data, count) != 0) {
    iq_get_abort("sha_update");
    return;
  }
  Serial.printf("RTL_IQ_GET_DATA bytes=%u\n", static_cast<unsigned>(count));
  if (Serial.writeBytes(data, count) != count) {
    iq_get_abort("serial_write");
    return;
  }
  g_iq_get.sent += count;
  if (g_iq_get.sent != total) return;

  uint8_t digest[32];
  if (mbedtls_sha256_finish(&g_iq_get.sha, digest) != 0) {
    iq_get_abort("sha_finish");
    return;
  }
  mbedtls_sha256_free(&g_iq_get.sha);
  g_iq_get.active = false;
  Serial.printf("RTL_IQ_GET_DONE bytes=%u sha256=", static_cast<unsigned>(total));
  print_hex(digest, sizeof(digest));
  Serial.println();
  g_iq_get = {};
}

void sd_remove(const char* path_hex) {
  if (sd_transfer_radio_busy()) {
    Serial.println("SD_REMOVE_ERROR radio_busy");
    return;
  }
  char path[sizeof(g_sd_put.target)];
  if (!decode_hex_text(path_hex, path, sizeof(path)) || !sd_remove_path_allowed(path)) {
    Serial.println("SD_REMOVE_ERROR invalid_path");
    return;
  }
  if (orcsdr_splash_is_active()) orcsdr_splash_end();
  if (!ensure_tab5_sd()) {
    Serial.println("SD_REMOVE_ERROR sd_unavailable");
    return;
  }
  if (!g_sd_fs->exists(path)) {
    Serial.printf("SD_REMOVE_MISSING path=\"%s\"\n", path);
    return;
  }
  Serial.printf(g_sd_fs->remove(path) ? "SD_REMOVE_DONE path=\"%s\"\n"
                                     : "SD_REMOVE_ERROR remove_failed path=\"%s\"\n",
                path);
}

void sd_put_abort(const char* reason) {
  if (g_sd_put.file) g_sd_put.file.close();
  if (g_sd_put.temporary[0]) g_sd_fs->remove(g_sd_put.temporary);
  if (g_sd_put.active) mbedtls_sha256_free(&g_sd_put.sha);
  reset_sd_put_state();
  Serial.printf("SD_PUT_ERROR %s\n", reason ? reason : "aborted");
  g_sd_transfer_active.store(false, std::memory_order_release);
}

bool sd_put_commit() {
  uint8_t digest[32];
  if (mbedtls_sha256_finish(&g_sd_put.sha, digest) != 0) {
    sd_put_abort("sha_finish");
    return false;
  }
  mbedtls_sha256_free(&g_sd_put.sha);
  g_sd_put.active = false;
  g_sd_put.file.flush();
  g_sd_put.file.close();

  uint8_t difference = 0;
  for (size_t i = 0; i < sizeof(digest); ++i) {
    difference |= digest[i] ^ g_sd_put.expected_sha[i];
  }
  if (difference != 0) {
    g_sd_fs->remove(g_sd_put.temporary);
    Serial.println("SD_PUT_ERROR sha_mismatch");
    reset_sd_put_state();
    g_sd_transfer_active.store(false, std::memory_order_release);
    return false;
  }

  char backup[136];
  snprintf(backup, sizeof(backup), "%s.bak", g_sd_put.target);
  g_sd_fs->remove(backup);
  const bool had_target = g_sd_fs->exists(g_sd_put.target);
  if (had_target && !g_sd_fs->rename(g_sd_put.target, backup)) {
    g_sd_fs->remove(g_sd_put.temporary);
    Serial.println("SD_PUT_ERROR backup_failed");
    reset_sd_put_state();
    g_sd_transfer_active.store(false, std::memory_order_release);
    return false;
  }
  if (!g_sd_fs->rename(g_sd_put.temporary, g_sd_put.target)) {
    if (had_target) (void)g_sd_fs->rename(backup, g_sd_put.target);
    g_sd_fs->remove(g_sd_put.temporary);
    Serial.println("SD_PUT_ERROR rename_failed");
    reset_sd_put_state();
    g_sd_transfer_active.store(false, std::memory_order_release);
    return false;
  }
  if (had_target) g_sd_fs->remove(backup);
  Serial.printf("SD_PUT_DONE bytes=%llu sha256=",
                static_cast<unsigned long long>(g_sd_put.received));
  print_hex(digest, sizeof(digest));
  Serial.printf(" path=\"%s\"\n", g_sd_put.target);
  reset_sd_put_state();
  g_sd_transfer_active.store(false, std::memory_order_release);
  return true;
}

void sd_put_begin(char* arguments) {
  if (g_sd_put.active || g_sd_get.active) {
    Serial.println("SD_PUT_ERROR already_active");
    return;
  }
  g_sd_transfer_active.store(true, std::memory_order_release);
  if (sd_transfer_radio_busy()) {
    Serial.println("SD_PUT_ERROR radio_busy");
    g_sd_transfer_active.store(false, std::memory_order_release);
    return;
  }
  char* sha_text = strchr(arguments, ' ');
  if (sha_text == nullptr) {
    Serial.println("SD_PUT_ERROR invalid_begin");
    g_sd_transfer_active.store(false, std::memory_order_release);
    return;
  }
  *sha_text++ = '\0';
  char* path_hex = strchr(sha_text, ' ');
  if (path_hex == nullptr) {
    Serial.println("SD_PUT_ERROR invalid_begin");
    g_sd_transfer_active.store(false, std::memory_order_release);
    return;
  }
  *path_hex++ = '\0';
  char target[sizeof(g_sd_put.target)];
  const uint64_t size = strtoull(arguments, nullptr, 10);
  uint8_t digest[32];
  if (size == 0 || size > kSdPutMaxBytes || !decode_hex(sha_text, digest, sizeof(digest)) ||
      !decode_hex_text(path_hex, target, sizeof(target)) || !sd_put_path_allowed(target)) {
    Serial.println("SD_PUT_ERROR invalid_begin");
    g_sd_transfer_active.store(false, std::memory_order_release);
    return;
  }
  /* Stop the SD-backed loading animation before taking exclusive card ownership. */
  if (orcsdr_splash_is_active()) orcsdr_splash_end();
  if (!ensure_tab5_sd() ||
      (!g_sd_fs->exists("/orcsdr") && !g_sd_fs->mkdir("/orcsdr"))) {
    Serial.println("SD_PUT_ERROR sd_unavailable");
    g_sd_transfer_active.store(false, std::memory_order_release);
    return;
  }

  strlcpy(g_sd_put.target, target, sizeof(g_sd_put.target));
  snprintf(g_sd_put.temporary, sizeof(g_sd_put.temporary), "%s.part", target);
  g_sd_fs->remove(g_sd_put.temporary);
  g_sd_put.file = g_sd_fs->open(g_sd_put.temporary, FILE_WRITE);
  if (!g_sd_put.file) {
    reset_sd_put_state();
    Serial.println("SD_PUT_ERROR open_failed");
    g_sd_transfer_active.store(false, std::memory_order_release);
    return;
  }
  g_sd_put.expected = size;
  memcpy(g_sd_put.expected_sha, digest, sizeof(digest));
  mbedtls_sha256_init(&g_sd_put.sha);
  if (mbedtls_sha256_starts(&g_sd_put.sha, 0) != 0) {
    g_sd_put.active = true;
    sd_put_abort("sha_start");
    return;
  }
  g_sd_put.active = true;
  Serial.printf("SD_PUT_READY chunk=%u bytes=%llu path=\"%s\"\n",
                static_cast<unsigned>(kSdPutChunkBytes),
                static_cast<unsigned long long>(size), target);
}

void sd_put_chunk(const char* length_text) {
  if (!g_sd_put.active) {
    Serial.println("SD_PUT_ERROR not_active");
    return;
  }
  const size_t length = static_cast<size_t>(strtoul(length_text, nullptr, 10));
  if (length == 0 || length > kSdPutChunkBytes ||
      g_sd_put.received + length > g_sd_put.expected) {
    sd_put_abort("invalid_chunk");
    return;
  }
  Serial.printf("SD_PUT_DATA bytes=%u\n", static_cast<unsigned>(length));
  const size_t got = Serial.readBytes(g_sd_put_chunk, length, 3000);
  if (got != length || g_sd_put.file.write(g_sd_put_chunk, length) != length ||
      mbedtls_sha256_update(&g_sd_put.sha, g_sd_put_chunk, length) != 0) {
    sd_put_abort(got != length ? "chunk_timeout" : "write_failed");
    return;
  }
  g_sd_put.received += length;
  Serial.printf("SD_PUT_ACK bytes=%llu\n",
                static_cast<unsigned long long>(g_sd_put.received));
  if (g_sd_put.received == g_sd_put.expected) (void)sd_put_commit();
}

void draw_capture_tool_panel() {
  /* Overlay lower waterfall region with capture status — audio path untouched. */
  const int panel_y = kWaterfallY + 40;
  const int panel_h = kWaterfallHeight - 50;
  M5.Display.fillRect(kSpectrumX + 4, panel_y, kSpectrumWidth - 8, panel_h, TFT_BLACK);
  M5.Display.drawRect(kSpectrumX + 4, panel_y, kSpectrumWidth - 8, panel_h, TFT_ORANGE);
  const size_t samples = g_audio_rec_write.load(std::memory_order_acquire);
  const bool active = g_audio_rec_active.load(std::memory_order_acquire);
  const bool full = g_audio_rec_full.load(std::memory_order_acquire);
  const float sec = static_cast<float>(samples) / static_cast<float>(kAudioRecRateHz);
  char line[128];
  M5.Display.setTextDatum(top_left);
  M5.Display.setTextSize(3);
  M5.Display.setTextColor(TFT_ORANGE, TFT_BLACK);
  M5.Display.drawString("CAPTURE tool — post-demod audio (48 kHz mono PCM)",
                        kSpectrumX + 16, panel_y + 12);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  snprintf(line, sizeof(line), "state: %s%s", active ? "RECORDING" : "idle",
           full ? " (buffer full)" : "");
  M5.Display.drawString(line, kSpectrumX + 16, panel_y + 48);
  snprintf(line, sizeof(line), "buffered: %.2f / %u s   samples=%u   rate=%lu",
            static_cast<double>(sec), static_cast<unsigned>(kAudioRecMaxSeconds),
            static_cast<unsigned>(samples), static_cast<unsigned long>(kAudioRecRateHz));
  M5.Display.drawString(line, kSpectrumX + 16, panel_y + 72);
  snprintf(line, sizeof(line), "LO meta: %s  %lu Hz", rtl_band_name(g_audio_rec_band),
            static_cast<unsigned long>(g_audio_rec_freq_hz));
  M5.Display.drawString(line, kSpectrumX + 16, panel_y + 96);
  snprintf(line, sizeof(line), "last file: %s",
           g_audio_rec_last_path[0] ? g_audio_rec_last_path : "(none yet)");
  M5.Display.drawString(line, kSpectrumX + 16, panel_y + 120);
  snprintf(line, sizeof(line),
           "SD: %s   |  REC button or RTL_REC_START/STOP   |  WAV for Audacity/etc",
           g_sd_ready ? "ready" : (g_sd_tried ? "missing — PCM held in PSRAM" : "will try on export"));
  M5.Display.drawString(line, kSpectrumX + 16, panel_y + 144);
  M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  M5.Display.drawString(
      "Future: IQ dump, gain lab, band scan — same tool shell, separate tabs.",
      kSpectrumX + 16, panel_y + 176);
}

void bump_rtl_ui() {
  rtl_ui_revision.fetch_add(1, std::memory_order_release);
}

#if RTL_USE_LEGACY_USB
// Public R820T2-style Nint packing validated against clean-room KZEL/100 MHz/NOAA.
bool encode_r820_pll(uint32_t frequency_hz, uint16_t* mix_div, uint8_t* r16_setup,
                     uint8_t* r16_active, uint8_t* r20, uint8_t* r21, uint8_t* r22) {
  const double lo_hz = static_cast<double>(frequency_hz) + kRtlIfOffsetHz;
  // 16/32 are clean-room proven. Higher powers of two are extrapolated for HF/MW attempts.
  static constexpr uint16_t kMixCandidates[] = {2, 4, 8, 16, 32, 64, 128, 256, 512, 1024};
  uint16_t chosen = 0;
  for (const uint16_t candidate : kMixCandidates) {
    const double vco = lo_hz * candidate;
    if (vco >= 1.77e9 && vco <= 3.90e9) {
      chosen = candidate;
      break;
    }
  }
  if (chosen == 0) return false;

  const double n = (lo_hz * chosen) / (2.0 * kRtlXtalHz);
  int nint = static_cast<int>(floor(n));
  int nfra = static_cast<int>(lround((n - nint) * 65536.0));
  if (nfra >= 65536) {
    ++nint;
    nfra = 0;
  }
  if (nfra < 0 || nint < 13) return false;

  const int packed = nint - 13;
  const int ni2c = packed >> 2;
  const int si2c = packed & 3;
  if (ni2c < 0 || ni2c > 63) return false;

  // Active R16 pattern measured for /16 and /32; extended for other powers of two.
  int mix_log = 0;
  for (uint16_t value = chosen; value > 1; value >>= 1) ++mix_log;
  // Keep R16 in the measured 8-bit style; mix>64 is experimental.
  const uint8_t active =
      static_cast<uint8_t>((((mix_log - 1) & 0x07) << 5) | 0x04);
  *mix_div = chosen;
  *r16_active = active;
  *r16_setup = static_cast<uint8_t>(active + 0x20);
  *r20 = static_cast<uint8_t>((si2c << 6) | ni2c);
  *r21 = static_cast<uint8_t>(nfra & 0xff);
  *r22 = static_cast<uint8_t>((nfra >> 8) & 0xff);
  return true;
}

bool run_rtl_tune(uint32_t frequency_hz) {
  uint16_t mix_div = 0;
  uint8_t r16_setup = 0;
  uint8_t r16_active = 0;
  uint8_t r20 = 0;
  uint8_t r21 = 0;
  uint8_t r22 = 0;
  if (!encode_r820_pll(frequency_hz, &mix_div, &r16_setup, &r16_active, &r20, &r21,
                       &r22)) {
    strlcpy(rtl_capture_error, "frequency outside tuner VCO range",
            sizeof(rtl_capture_error));
    return false;
  }
  Serial.printf("RTL_TUNE frequency_hz=%u mix_div=%u r16=%02x/%02x r20=%02x "
                "r21=%02x r22=%02x\n",
                frequency_hz, mix_div, r16_setup, r16_active, r20, r21, r22);
  for (size_t index = 0; index < std::size(kRtlFinalTuneTemplate); ++index) {
    RtlControlRecord record = kRtlFinalTuneTemplate[index];
    if (index == 3 || index == 7) record.data[1] = r16_setup;
    if (index == 12) record.data[1] = r16_active;
    if (index == 13) record.data[1] = r20;
    if (index == 15) record.data[1] = r22;
    if (index == 16) record.data[1] = r21;
    if (!run_control_record(record)) return false;
  }
  return true;
}
#endif /* RTL_USE_LEGACY_USB — PLL / final tune */

const char* rtl_capture_state_name(RtlCaptureState state) {
  switch (state) {
    case RtlCaptureState::ready: return "ready";
    case RtlCaptureState::queued: return "queued";
    case RtlCaptureState::running: return "running";
    case RtlCaptureState::complete: return "complete";
    case RtlCaptureState::failed: return "failed";
    default: return "disconnected";
  }
}

struct SdrButton {
  int x;
  int width;
  const char* text;
  uint32_t color;
};

void draw_sdr_button_row(int y, const SdrButton* buttons, size_t count) {
  int x = kSdrEdge;
  for (size_t index = 0; index < count; ++index) {
    const SdrButton& button = buttons[index];
    const int width = button.width;
    M5.Display.fillRoundRect(x, y, width, kSdrControlsHeight, 10, button.color);
    M5.Display.drawRoundRect(x, y, width, kSdrControlsHeight, 10, TFT_WHITE);
    M5.Display.setTextColor(TFT_WHITE, button.color);
    M5.Display.setTextDatum(middle_center);
    M5.Display.setTextSize(3);
    M5.Display.drawString(button.text, x + width / 2, y + kSdrControlsHeight / 2);
    x += width + kSdrGap;
  }
}

int sdr_button_at(int y, int touch_x, int touch_y, const int* widths, size_t count) {
  if (touch_y < y || touch_y >= y + kSdrControlsHeight) return -1;
  int x = kSdrEdge;
  for (size_t index = 0; index < count; ++index) {
    if (touch_x >= x && touch_x < x + widths[index]) return static_cast<int>(index);
    x += widths[index] + kSdrGap;
  }
  return -1;
}

void draw_sdr_controls(RtlBand band, bool running) {
  const int first_row_y = band == RtlBand::lora ? kSdrTuneY : kSdrBandY;
  M5.Display.fillRect(0, first_row_y - 6, 1280, 720 - (first_row_y - 6), TFT_BLACK);
  if (band == RtlBand::lora) {
    char bandwidth[20];
    char spreading_factor[16];
    snprintf(bandwidth, sizeof(bandwidth), "BW %uk",
             static_cast<unsigned>(lora_bandwidth_hz.load(std::memory_order_relaxed) /
                                   1000u));
    snprintf(spreading_factor, sizeof(spreading_factor), "SF %u",
             static_cast<unsigned>(lora_sf.load(std::memory_order_relaxed)));
    const bool iq_on = g_iq_rec_active.load(std::memory_order_acquire);
    const SdrButton lora_row[] = {
        {0, 170, "FREQ -", TFT_DARKGREY},
        {0, 170, "FREQ +", TFT_DARKGREY},
        {0, 220, bandwidth, TFT_DARKCYAN},
        {0, 150, spreading_factor, TFT_NAVY},
        {0, 150, iq_on ? "IQ STOP" : "IQ CAP",
         static_cast<uint32_t>(iq_on ? TFT_MAROON : TFT_NAVY)},
        {0, 220, running ? "STOP" : "START",
         static_cast<uint32_t>(running ? TFT_MAROON : TFT_DARKGREEN)},
    };
    draw_sdr_button_row(kSdrTuneY, lora_row, std::size(lora_row));
    return;
  }
  const bool rec_on = g_audio_rec_active.load(std::memory_order_acquire);
  const SdrButton band_row[] = {
      {0, 110, "FM",
       static_cast<uint32_t>(band == RtlBand::fm ? TFT_DARKGREEN : TFT_DARKGREY)},
      {0, 110, "AM",
       static_cast<uint32_t>(band == RtlBand::am ? TFT_DARKGREEN : TFT_DARKGREY)},
      {0, 110, "WX",
       static_cast<uint32_t>(band == RtlBand::wx ? TFT_DARKGREEN : TFT_DARKGREY)},
      {0, 120, "CB",
       static_cast<uint32_t>(band == RtlBand::cb ? TFT_DARKGREEN : TFT_DARKGREY)},
      {0, 140, "LORA",
       static_cast<uint32_t>(band == RtlBand::lora ? TFT_DARKGREEN : TFT_DARKGREY)},
      {0, 160, "BROWSE",
       static_cast<uint32_t>(band == RtlBand::browse ? TFT_DARKGREEN : TFT_DARKGREY)},
      {0, 170, rec_on ? "REC*" : "REC",
       static_cast<uint32_t>(rec_on ? TFT_MAROON : TFT_DARKGREY)},
      {0, 200, running ? "STOP" : "START",
       static_cast<uint32_t>(running ? TFT_MAROON : TFT_DARKGREEN)},
  };
  const bool gfx_on = rtl_graphics_enabled.load(std::memory_order_acquire);
  const bool sound_on = rtl_audio_enabled.load(std::memory_order_acquire);
  const SdrButton tune_row[] = {
      {0, 170, "FREQ -", TFT_DARKGREY},
      {0, 170, "FREQ +", TFT_DARKGREY},
      {0, 220, sound_on ? "SOUND ON" : "SOUND OFF",
       static_cast<uint32_t>(sound_on ? TFT_DARKGREEN : TFT_MAROON)},
      {0, 150, "VOL -", TFT_NAVY},
      {0, 150, "VOL +", TFT_NAVY},
      {0, 220, gfx_on ? "GFX ON" : "GFX OFF",
       static_cast<uint32_t>(gfx_on ? TFT_DARKGREEN : TFT_MAROON)},
  };
  draw_sdr_button_row(kSdrBandY, band_row, std::size(band_row));
  draw_sdr_button_row(kSdrTuneY, tune_row, std::size(tune_row));
}

/** Freeze scope/waterfall with a clear banner (audio keeps running). */
void paint_graphics_paused_banner() {
  const int width = spectrum_draw_width();
  M5.Display.fillRect(kSpectrumX, kSpectrumY, width,
                      (kWaterfallY + kWaterfallHeight) - kSpectrumY, TFT_BLACK);
  M5.Display.drawRect(kSpectrumX, kSpectrumY, width, kSpectrumHeight, TFT_DARKGREY);
  M5.Display.drawRect(kSpectrumX, kWaterfallY, width, kWaterfallHeight, TFT_DARKGREY);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(4);
  M5.Display.setTextColor(TFT_ORANGE, TFT_BLACK);
  M5.Display.drawString("GRAPHICS OFF", kSpectrumX + width / 2,
                        kSpectrumY + kSpectrumHeight / 2);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  M5.Display.drawString(
      "scope paused  |  audio + SIG + REC still live  |  tap GFX OFF to resume",
      kSpectrumX + width / 2, kWaterfallY + kWaterfallHeight / 2);
}

/**
 * Update relative signal level from CU8 IQ (cheap, call from IQ path).
 * 0 dBFS ≈ full-scale samples; noise floor often ~-40…-70 depending on gain.
 */
void update_signal_level_from_iq(const uint8_t* iq, size_t bytes) {
  if (iq == nullptr || bytes < 4) return;
  uint64_t sum = 0;
  size_t pairs = 0;
  /* Stride keeps this light on the audio path. */
  for (size_t i = 0; i + 1 < bytes; i += 32) {
    const int32_t ii = static_cast<int32_t>(iq[i]) - 128;
    const int32_t qq = static_cast<int32_t>(iq[i + 1]) - 128;
    sum += static_cast<uint32_t>(ii * ii + qq * qq);
    ++pairs;
  }
  if (pairs == 0) return;
  const float power = static_cast<float>(sum) / static_cast<float>(pairs);
  /* Full-scale CU8 complex: 2 * 127.5^2 */
  constexpr float kFullScale = 2.0f * 127.5f * 127.5f;
  const float dbfs = 10.0f * log10f((power / kFullScale) + 1.0e-12f);
  rtl_signal_dbfs.store(dbfs, std::memory_order_relaxed);
}

/**
 * Top-of-screen horizontal SIG meter (left → right).
 * 6× old vertical travel (42 → 252 px). Redraws cheaply (fill only when width changes).
 * Strip is y=0..28 only — never paints frequency/VOL/help.
 */
void draw_signal_meter(bool force_chrome = false) {
  if (orcsdr::settings::active()) return;
  const float raw = rtl_signal_dbfs.load(std::memory_order_relaxed);
  rtl_signal_dbfs_smooth = 0.88f * rtl_signal_dbfs_smooth + 0.12f * raw;

  float t = (rtl_signal_dbfs_smooth + 70.0f) / 70.0f;
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;

  constexpr int kLabelX = 8;
  constexpr int kBarX = 44;
  constexpr int kBarY = 8;
  constexpr int kBarH = 18;
  constexpr int kBarW = 42 * 6; /* 252 px — 6× former vertical travel */
  constexpr int kDbX = kBarX + kBarW + 10;

  static int s_last_fill_w = -1;
  static int s_last_db_i = 999;
  static uint32_t s_last_fill_color = 0;
  static bool s_chrome_drawn = false;

  const int fill_w = static_cast<int>(t * (kBarW - 2));
  const int db_i = static_cast<int>(lroundf(rtl_signal_dbfs_smooth));
  uint32_t fill_color = TFT_GREEN;
  if (t > 0.85f) fill_color = TFT_RED;
  else if (t > 0.65f) fill_color = TFT_YELLOW;

  if (force_chrome || !s_chrome_drawn) {
    M5.Display.fillRect(0, 0, kDbX + 130, 30, TFT_BLACK);
    M5.Display.setTextDatum(middle_left);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    M5.Display.drawString("SIG", kLabelX, kBarY + kBarH / 2);
    M5.Display.drawRect(kBarX, kBarY, kBarW, kBarH, TFT_DARKGREY);
    for (int tick = 1; tick <= 3; ++tick) {
      const int tx = kBarX + (kBarW * tick) / 4;
      M5.Display.drawFastVLine(tx, kBarY + kBarH, 3, TFT_DARKGREY);
    }
    s_chrome_drawn = true;
    s_last_fill_w = -1;
    s_last_db_i = 999;
    s_last_fill_color = 0;
  }

  if (s_last_fill_w < 0 || fill_color != s_last_fill_color) {
    M5.Display.fillRect(kBarX + 1, kBarY + 1, kBarW - 2, kBarH - 2, TFT_BLACK);
    if (fill_w > 0)
      M5.Display.fillRect(kBarX + 1, kBarY + 1, fill_w, kBarH - 2, fill_color);
  } else if (fill_w > s_last_fill_w) {
    M5.Display.fillRect(kBarX + 1 + s_last_fill_w, kBarY + 1,
                        fill_w - s_last_fill_w, kBarH - 2, fill_color);
  } else if (fill_w < s_last_fill_w) {
    M5.Display.fillRect(kBarX + 1 + fill_w, kBarY + 1,
                        s_last_fill_w - fill_w, kBarH - 2, TFT_BLACK);
  }
  if (fill_w != s_last_fill_w || fill_color != s_last_fill_color) {
    s_last_fill_w = fill_w;
    s_last_fill_color = fill_color;
  }

  if (db_i != s_last_db_i) {
    M5.Display.fillRect(kDbX, 4, 120, 24, TFT_BLACK);
    char db_label[20];
    snprintf(db_label, sizeof(db_label), "%+.0f dB", static_cast<double>(db_i));
    M5.Display.setTextDatum(middle_left);
    M5.Display.setTextSize(3);
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5.Display.drawString(db_label, kDbX, kBarY + kBarH / 2);
    M5.Display.setTextSize(2);
    s_last_db_i = db_i;
  }
}

void draw_sdr_header(RtlBand band, uint32_t frequency_hz, uint8_t volume) {
  if (orcsdr::settings::active()) return;
  char label[96];
  char frequency_text[24];
  format_frequency(frequency_text, sizeof(frequency_text), frequency_hz);
  /* Full header clear; layout: meter (0..28), title (34..58), help (68..84). */
  M5.Display.fillRect(0, 0, 1280, kSpectrumY - 4, TFT_BLACK);
  draw_signal_meter(true);

  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(4);
  M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
  snprintf(label, sizeof(label), "%s  %s", rtl_band_name(band), frequency_text);
  M5.Display.drawString(label, 560, 48);
  M5.Display.setTextSize(3);
  M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
  if (rtl_audio_enabled.load(std::memory_order_relaxed)) {
    snprintf(label, sizeof(label), "VOL %u", volume);
  } else {
    strlcpy(label, "SOUND OFF", sizeof(label));
  }
  M5.Display.drawString(label, 1080, 48);
  draw_tool_tabs();
  orcsdr::audio_header::draw_home_button();
  draw_global_settings_gear();
}

void draw_global_settings_gear() {
  constexpr int cx = 1240, cy = 37;
  M5.Display.fillRoundRect(1211, 8, 58, 58, 8, 0x2104);
  M5.Display.drawRoundRect(1211, 8, 58, 58, 8, TFT_LIGHTGREY);
  M5.Display.drawCircle(cx, cy, 13, TFT_CYAN);
  M5.Display.drawCircle(cx, cy, 5, TFT_CYAN);
  M5.Display.drawLine(cx - 21, cy, cx - 13, cy, TFT_CYAN);
  M5.Display.drawLine(cx + 13, cy, cx + 21, cy, TFT_CYAN);
  M5.Display.drawLine(cx, cy - 21, cx, cy - 13, TFT_CYAN);
  M5.Display.drawLine(cx, cy + 13, cx, cy + 21, TFT_CYAN);
}

void draw_cb_dashboard(bool static_panel) {
  if (rtl_ui_band != RtlBand::cb || rtl_nav_open) return;
  if (static_panel) {
    const bool image_ok = ensure_tab5_sd() && g_sd_fs->exists(kCbDashboardPath) &&
                          M5.Display.drawJpgFile(*g_sd_fs, kCbDashboardPath,
                                                 kCbPanelX, kCbPanelY);
    if (!image_ok) {
      M5.Display.fillRoundRect(kCbPanelX, kCbPanelY, kCbPanelWidth,
                               kCbPanelHeight, 10, 0x632c);
      M5.Display.drawRoundRect(kCbPanelX, kCbPanelY, kCbPanelWidth,
                               kCbPanelHeight, 10, TFT_LIGHTGREY);
      M5.Display.fillRoundRect(kCbPanelX + 80, kCbPanelY + 178, 224, 58,
                               8, TFT_BLACK);
      M5.Display.fillCircle(kCbPanelX + 192, kCbPanelY + 311, 62, TFT_DARKGREY);
    }
    M5.Display.setTextDatum(middle_center);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_WHITE);
    constexpr const char* labels[] = {"CH-", "CH+", "MODE", "CLAR", "SQL-", "SQL+"};
    for (size_t i = 0; i < std::size(labels); ++i) {
      M5.Display.drawString(labels[i], kCbPanelX + 51 + static_cast<int>(i) * 56,
                            kCbPanelY + 410);
    }
  }

  const size_t channel = cb_channel_index(rtl_ui_frequency_hz);
  M5.Display.fillRect(kCbPanelX + 84, kCbPanelY + 183, 216, 45, TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(4);
  M5.Display.setTextColor(TFT_RED, TFT_BLACK);
  char text[24];
  snprintf(text, sizeof(text), "%02u", static_cast<unsigned>(channel + 1));
  M5.Display.drawString(text, kCbPanelX + 126, kCbPanelY + 202);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
  snprintf(text, sizeof(text), "%.3f", rtl_ui_frequency_hz / 1000000.0);
  M5.Display.drawString(text, kCbPanelX + 229, kCbPanelY + 198);
  const CbMode mode = cb_mode.load(std::memory_order_relaxed);
  const int clarifier = cb_clarifier_hz.load(std::memory_order_relaxed);
  const int squelch = cb_squelch_dbfs.load(std::memory_order_relaxed);
  snprintf(text, sizeof(text), "%s %+.1fk SQL%d",
           mode == CbMode::usb ? "USB" : mode == CbMode::lsb ? "LSB" : "AM",
           clarifier / 1000.0, squelch);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(cb_squelch_open.load(std::memory_order_relaxed)
                              ? TFT_GREEN
                              : TFT_LIGHTGREY,
                          TFT_BLACK);
  M5.Display.drawString(text, kCbPanelX + 228, kCbPanelY + 216);
  float level = (rtl_signal_dbfs_smooth + 70.0f) / 70.0f;
  level = constrain(level, 0.0f, 1.0f);
  M5.Display.drawRect(kCbPanelX + 172, kCbPanelY + 223, 112, 4, TFT_DARKGREY);
  M5.Display.fillRect(kCbPanelX + 173, kCbPanelY + 224, 110, 2, TFT_BLACK);
  M5.Display.fillRect(kCbPanelX + 173, kCbPanelY + 224,
                      static_cast<int>(110.0f * level), 2,
                      level > 0.82f ? TFT_RED : level > 0.62f ? TFT_YELLOW : TFT_GREEN);
}

const char* lora_port_label(uint16_t port) {
  switch (port) {
    case 1: return "TEXT";
    case 3: return "POSITION";
    case 4: return "NODE INFO";
    case 5: return "ROUTING";
    case 67: return "TELEMETRY";
    case 70: return "TRACEROUTE";
    default: return "DATA";
  }
}

void format_lora_destination(char* output, size_t output_size,
                             uint32_t destination) {
  if (destination == UINT32_MAX) {
    strlcpy(output, "ALL NODES", output_size);
  } else {
    snprintf(output, output_size, "!%08lx",
             static_cast<unsigned long>(destination));
  }
}

void format_lora_tenths(char* output, size_t output_size, int16_t value,
                        bool show_plus = false) {
  const int magnitude = value < 0 ? -static_cast<int>(value) : value;
  snprintf(output, output_size, "%s%d.%d", value < 0 ? "-" : show_plus ? "+" : "",
           magnitude / 10, magnitude % 10);
}

void format_lora_e7(char* output, size_t output_size, int32_t value) {
  const int64_t magnitude = value < 0 ? -static_cast<int64_t>(value) : value;
  const int64_t rounded = (magnitude + 50) / 100;
  snprintf(output, output_size, "%s%lld.%05lld", value < 0 ? "-" : "",
           static_cast<long long>(rounded / 100000),
           static_cast<long long>(rounded % 100000));
}

void draw_lora_asset(const char* path) {
  const bool image_ok = !ui_documentation_mode && ensure_tab5_sd() && g_sd_fs->exists(path) &&
                        M5.Display.drawJpgFile(*g_sd_fs, path, kSpectrumX, kSpectrumY);
  if (!image_ok) {
    M5.Display.fillRect(kSpectrumX, kSpectrumY, kSpectrumWidth, kCbPanelHeight, TFT_BLACK);
    M5.Display.drawRoundRect(kSpectrumX, kSpectrumY, kSpectrumWidth,
                             kCbPanelHeight, 16, TFT_DARKCYAN);
  }
}

int lora_frequency_slot(uint32_t frequency_hz) {
  if (frequency_hz < 901875000u || frequency_hz > 928000000u) return -1;
  return static_cast<int>((frequency_hz - 901875000u + 125000u) / 250000u);
}

void draw_lora_live_view(bool force) {
  static uint32_t last_count = UINT32_MAX;
  static uint32_t last_frequency = 0;
  const uint32_t count = lora_messages.load(std::memory_order_relaxed);
  const bool packet_changed = force || count != last_count ||
                              rtl_ui_frequency_hz != last_frequency;
  if (!packet_changed) return;
  LoraDisplayPacket packets[kLoraDisplayPacketCount];
  portENTER_CRITICAL(&lora_message_mux);
  memcpy(packets, lora_display_packets, sizeof(packets));
  portEXIT_CRITICAL(&lora_message_mux);

  draw_lora_asset(kLoraLivePath);
  constexpr int left_x = kSpectrumX + 44;
  constexpr int left_w = 620;
  constexpr int scope_x = kSpectrumX + 700;
  constexpr int scope_w = 390;
  M5.Display.fillRect(left_x, kSpectrumY + 42, left_w, 205, TFT_BLACK);
  M5.Display.fillRect(scope_x, kSpectrumY + 44, scope_w, 126, TFT_BLACK);
  M5.Display.fillRect(scope_x, kSpectrumY + 202, scope_w, 172, TFT_BLACK);
  M5.Display.fillRect(left_x, kSpectrumY + 268, left_w, 154, TFT_BLACK);
  M5.Display.drawRect(left_x, kSpectrumY + 268, left_w, 154, TFT_CYAN);
  M5.Display.drawRect(scope_x, kSpectrumY + 44, scope_w, 126, TFT_MAGENTA);
  M5.Display.drawRect(scope_x, kSpectrumY + 202, scope_w, 172, TFT_GREEN);

  M5.Display.setTextDatum(middle_left);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_CYAN);
  M5.Display.drawString("LATEST VERIFIED DECODE", left_x + 14, kSpectrumY + 56);
  if (packets[0].sender == 0) {
    M5.Display.setTextSize(4);
    M5.Display.setTextColor(TFT_LIGHTGREY);
    M5.Display.drawString("WAITING FOR PACKET", left_x + 14, kSpectrumY + 142);
  } else {
    char value[144];
    char destination[16];
    format_lora_destination(destination, sizeof(destination), packets[0].destination);
    snprintf(value, sizeof(value), "FROM !%08lx    TO %s",
             static_cast<unsigned long>(packets[0].sender),
             destination);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_GREEN);
    M5.Display.drawString(value, left_x + 14, kSpectrumY + 84);
    snprintf(value, sizeof(value), "%s    PACKET ID %08lx",
             lora_port_label(packets[0].port),
             static_cast<unsigned long>(packets[0].packet_id));
    M5.Display.setTextColor(TFT_CYAN);
    M5.Display.drawString(value, left_x + 14, kSpectrumY + 108);
    const char* text = packets[0].text[0] ? packets[0].text
                                          : lora_port_label(packets[0].port);
    const size_t text_length = strlen(text);
    const size_t chars_per_line = text_length <= 20 ? 20 : text_length <= 60 ? 27 : 36;
    const size_t rows = text_length <= 20 ? 2 : 3;
    const int text_size = text_length <= 20 ? 5 : text_length <= 60 ? 4 : 3;
    const int line_spacing = text_size == 5 ? 42 : text_size == 4 ? 35 : 31;
    char line[37]{};
    M5.Display.setTextSize(text_size);
    M5.Display.setTextColor(TFT_WHITE);
    for (size_t row = 0; row < rows; ++row) {
      const size_t start = row * chars_per_line;
      if (start >= strlen(text)) break;
      const size_t remaining = min(chars_per_line, strlen(text) - start);
      memcpy(line, text + start, remaining);
      line[remaining] = '\0';
      M5.Display.drawString(line, left_x + 14,
                            kSpectrumY + 142 + static_cast<int>(row) * line_spacing);
    }
    char snr[16];
    char signal[16];
    format_lora_tenths(snr, sizeof(snr),
                       packets[0].snr_tenths == INT16_MAX ? 0 : packets[0].snr_tenths,
                       true);
    format_lora_tenths(signal, sizeof(signal),
                       packets[0].signal_tenths == INT16_MAX ? 0
                                                             : packets[0].signal_tenths);
    snprintf(value, sizeof(value), "SNR %s dB   SIG %s dBFS", snr, signal);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_LIGHTGREY);
    M5.Display.drawString(value, left_x + 14, kSpectrumY + 236);
  }

  M5.Display.setTextColor(TFT_MAGENTA);
  M5.Display.setTextSize(2);
  M5.Display.drawString("LIVE SCOPE", scope_x + 14, kSpectrumY + 58);
  M5.Display.setTextColor(TFT_CYAN);
  M5.Display.drawString("LIVE WATERFALL", left_x + 14, kSpectrumY + 258);

  size_t text_index = 0;
  while (text_index < kLoraDisplayPacketCount && packets[text_index].sender != 0 &&
         packets[text_index].port != 1) ++text_index;
  M5.Display.setTextColor(TFT_GREEN);
  M5.Display.setTextSize(2);
  M5.Display.drawString("LAST MESSAGE", scope_x + 14, kSpectrumY + 218);
  if (text_index < kLoraDisplayPacketCount && packets[text_index].sender != 0) {
    char value[96];
    char destination[16];
    format_lora_destination(destination, sizeof(destination),
                            packets[text_index].destination);
    snprintf(value, sizeof(value), "LONGFAST / %s",
             lora_port_label(packets[text_index].port));
    M5.Display.setTextColor(TFT_CYAN);
    M5.Display.drawString(value, scope_x + 14, kSpectrumY + 246);
    snprintf(value, sizeof(value), "TO %s", destination);
    M5.Display.setTextColor(TFT_LIGHTGREY);
    M5.Display.drawString(value, scope_x + 14, kSpectrumY + 270);
    char line[21]{};
    M5.Display.setTextSize(3);
    M5.Display.setTextColor(TFT_WHITE);
    for (size_t row = 0; row < 2; ++row) {
      const size_t start = row * 20;
      if (start >= strlen(packets[text_index].text)) break;
      strlcpy(line, packets[text_index].text + start, sizeof(line));
      M5.Display.drawString(line, scope_x + 14,
                            kSpectrumY + 302 + static_cast<int>(row) * 32);
    }
  } else {
    M5.Display.setTextSize(3);
    M5.Display.setTextColor(TFT_LIGHTGREY);
    M5.Display.drawString("WAITING FOR TEXT", scope_x + 14, kSpectrumY + 302);
  }
  char status[96];
  snprintf(status, sizeof(status), "SLOT %d   MSG %lu",
           lora_frequency_slot(rtl_ui_frequency_hz),
           static_cast<unsigned long>(count));
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_LIGHTGREY);
  M5.Display.drawString(status, scope_x + 14, kSpectrumY + 360);
  snprintf(status, sizeof(status), "NOISE %ld   TRIG %ld",
           static_cast<long>(lroundf(lora_noise_dbfs.load(std::memory_order_relaxed))),
           static_cast<long>(lroundf(lora_trigger_dbfs.load(std::memory_order_relaxed))));
  M5.Display.setTextDatum(middle_right);
  M5.Display.drawString(status, scope_x + scope_w - 14, kSpectrumY + 360);
  M5.Display.setTextDatum(middle_left);

  last_count = count;
  last_frequency = rtl_ui_frequency_hz;
}

void draw_lora_packets_view(bool force) {
  static uint32_t last_count = UINT32_MAX;
  const uint32_t count = lora_messages.load(std::memory_order_relaxed);
  if (!force && count == last_count) return;
  last_count = count;
  LoraDisplayPacket packets[kLoraDisplayPacketCount];
  portENTER_CRITICAL(&lora_message_mux);
  memcpy(packets, lora_display_packets, sizeof(packets));
  portEXIT_CRITICAL(&lora_message_mux);
  draw_lora_asset(kLoraMessagesPath);

  constexpr int kRowY[] = {28, 169, 310};
  M5.Display.setTextDatum(middle_left);
  for (size_t index = 0; index < 3; ++index) {
    if (packets[index].sender == 0) continue;
    const int y = kSpectrumY + kRowY[index];
    char value[96];
    char destination[16];
    format_lora_destination(destination, sizeof(destination),
                            packets[index].destination);
    snprintf(value, sizeof(value), "!%08lx  >  %s   %s   %lus",
             static_cast<unsigned long>(packets[index].sender),
             destination,
             lora_port_label(packets[index].port),
             static_cast<unsigned long>((millis() - packets[index].received_ms) / 1000u));
    M5.Display.setTextSize(3);
    M5.Display.setTextColor(packets[index].port == 3 ? TFT_GREEN : TFT_CYAN);
    M5.Display.drawString(value, kSpectrumX + 55, y + 28);
    const char* text = packets[index].text[0] ? packets[index].text
                                              : lora_port_label(packets[index].port);
    snprintf(value, sizeof(value), "%.28s", text);
    M5.Display.setTextSize(5);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.drawString(value, kSpectrumX + 55, y + 86);
    M5.Display.setTextDatum(middle_center);
    M5.Display.setTextSize(4);
    M5.Display.setTextColor(TFT_GREEN);
    if (packets[index].snr_tenths == INT16_MAX) strlcpy(value, "SNR --", sizeof(value));
    else {
      char snr[16];
      format_lora_tenths(snr, sizeof(snr), packets[index].snr_tenths, true);
      snprintf(value, sizeof(value), "%s dB", snr);
    }
    M5.Display.drawString(value, kSpectrumX + 1050, y + 48);
    M5.Display.setTextSize(3);
    if (packets[index].signal_tenths == INT16_MAX) strlcpy(value, "SIG --", sizeof(value));
    else snprintf(value, sizeof(value), "SIG %.0f", packets[index].signal_tenths / 10.0);
    M5.Display.drawString(value, kSpectrumX + 1050, y + 90);
    M5.Display.setTextDatum(middle_left);
  }
  if (packets[0].sender == 0) {
    M5.Display.setTextDatum(middle_center);
    M5.Display.setTextSize(5);
    M5.Display.setTextColor(TFT_LIGHTGREY);
    M5.Display.drawString("WAITING FOR VERIFIED MESSAGES",
                          kSpectrumX + kSpectrumWidth / 2, kSpectrumY + 235);
  }
}

void draw_lora_map_view(bool force) {
  static uint32_t last_count = UINT32_MAX;
  const uint32_t count = lora_messages.load(std::memory_order_relaxed);
  if (!force && count == last_count) return;
  last_count = count;
  LoraNodePosition positions[kLoraNodePositionCount];
  LoraDisplayPacket packets[kLoraDisplayPacketCount];
  portENTER_CRITICAL(&lora_message_mux);
  memcpy(positions, lora_node_positions, sizeof(positions));
  memcpy(packets, lora_display_packets, sizeof(packets));
  portEXIT_CRITICAL(&lora_message_mux);
  draw_lora_asset(kLoraMapPath);

  constexpr int map_x = kSpectrumX + 30;
  constexpr int map_y = kSpectrumY + 24;
  constexpr int map_w = 830;
  constexpr int map_h = 420;
  if (positions[0].node == 0) {
    M5.Display.setTextDatum(middle_center);
    M5.Display.setTextSize(5);
    M5.Display.setTextColor(TFT_LIGHTGREY);
    M5.Display.drawString("WAITING FOR GPS", map_x + map_w / 2, map_y + map_h / 2);
    return;
  }
  constexpr float kMapWidthMiles = 5.0f;
  constexpr float kMapHeightMiles = 2.0f;
  const float center_lat = positions[0].latitude_e7 / 10000000.0f;
  const float center_lon = positions[0].longitude_e7 / 10000000.0f;
  const float lon_miles = 69.0f * cosf(center_lat * 0.01745329252f);
  char value[72];
  M5.Display.setTextDatum(middle_left);
  M5.Display.setTextSize(3);
  for (size_t index = 0; index < kLoraNodePositionCount; ++index) {
    if (positions[index].node == 0) continue;
    const float east = (positions[index].longitude_e7 / 10000000.0f - center_lon) * lon_miles;
    const float north = (positions[index].latitude_e7 / 10000000.0f - center_lat) * 69.0f;
    const int x = map_x + map_w / 2 + static_cast<int>(east * map_w / kMapWidthMiles);
    const int y = map_y + map_h / 2 - static_cast<int>(north * map_h / kMapHeightMiles);
    if (x < map_x || x >= map_x + map_w || y < map_y || y >= map_y + map_h) continue;
    const uint32_t color = index == 0 ? TFT_GREEN : TFT_CYAN;
    M5.Display.fillCircle(x, y, index == 0 ? 11 : 8, color);
    snprintf(value, sizeof(value), "!%08lx", static_cast<unsigned long>(positions[index].node));
    M5.Display.setTextColor(color);
    M5.Display.drawString(value, x + 14, y);
  }

  constexpr int info_x = kSpectrumX + 910;
  M5.Display.setTextDatum(middle_left);
  M5.Display.setTextColor(TFT_CYAN);
  M5.Display.setTextSize(2);
  M5.Display.drawString("CENTER NODE", info_x, kSpectrumY + 50);
  snprintf(value, sizeof(value), "!%08lx", static_cast<unsigned long>(positions[0].node));
  M5.Display.setTextSize(4);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.drawString(value, info_x, kSpectrumY + 82);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_CYAN);
  M5.Display.drawString("LATITUDE", info_x, kSpectrumY + 145);
  format_lora_e7(value, sizeof(value), positions[0].latitude_e7);
  M5.Display.setTextSize(3);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.drawString(value, info_x, kSpectrumY + 174);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_CYAN);
  M5.Display.drawString("LONGITUDE", info_x, kSpectrumY + 205);
  format_lora_e7(value, sizeof(value), positions[0].longitude_e7);
  M5.Display.setTextSize(3);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.drawString(value, info_x, kSpectrumY + 234);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_MAGENTA);
  M5.Display.drawString("LAST SIGNAL", info_x, kSpectrumY + 287);
  if (packets[0].snr_tenths != INT16_MAX) {
    snprintf(value, sizeof(value), "SNR %+.1f", packets[0].snr_tenths / 10.0);
    M5.Display.setTextSize(3);
    M5.Display.setTextColor(TFT_GREEN);
    M5.Display.drawString(value, info_x, kSpectrumY + 316);
  }
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_GREEN);
  M5.Display.drawString("MAP AREA", info_x, kSpectrumY + 370);
  M5.Display.setTextSize(3);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.drawString("5 x 2 MI", info_x, kSpectrumY + 402);
  snprintf(value, sizeof(value), "AGE %lus  SLOT %d",
           static_cast<unsigned long>((millis() - positions[0].received_ms) / 1000u),
           lora_frequency_slot(rtl_ui_frequency_hz));
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_LIGHTGREY);
  M5.Display.drawString(value, info_x, kSpectrumY + 436);
}

void draw_lora_dashboard(bool static_panel) {
  if (rtl_ui_band != RtlBand::lora || rtl_nav_open) return;
  if (lora_view == LoraView::Packets) draw_lora_packets_view(static_panel);
  else if (lora_view == LoraView::Map) draw_lora_map_view(static_panel);
  else draw_lora_live_view(static_panel);

  static uint32_t last_second = UINT32_MAX;
  static uint32_t last_count = UINT32_MAX;
  static uint32_t last_frequency = 0;
  static uint8_t last_log_state = UINT8_MAX;
  const uint32_t now_second = millis() / 1000u;
  const uint32_t count = lora_messages.load(std::memory_order_relaxed);
  const uint8_t log_state =
      lora_log_error.load(std::memory_order_relaxed)
          ? 3
          : (lora_log_requested.load(std::memory_order_relaxed)
                 ? (lora_log_ready.load(std::memory_order_relaxed) ? 2 : 1)
                 : 0);
  if (!static_panel && now_second == last_second && count == last_count &&
      rtl_ui_frequency_hz == last_frequency && log_state == last_log_state) return;
  last_second = now_second;
  last_count = count;
  last_frequency = rtl_ui_frequency_hz;
  last_log_state = log_state;

  LoraDisplayPacket packet{};
  LoraNodePosition position{};
  portENTER_CRITICAL(&lora_message_mux);
  packet = lora_display_packets[0];
  position = lora_node_positions[0];
  portEXIT_CRITICAL(&lora_message_mux);
  constexpr int rail_y = 578;
  constexpr int rail_h = 58;
  M5.Display.fillRoundRect(kSpectrumX, rail_y, kSpectrumWidth, rail_h, 10, TFT_BLACK);
  M5.Display.drawRoundRect(kSpectrumX, rail_y, kSpectrumWidth, rail_h, 10, TFT_DARKCYAN);
  M5.Display.drawFastVLine(kSpectrumX + 610, rail_y + 8, rail_h - 16, TFT_DARKGREY);
  M5.Display.drawFastVLine(kSpectrumX + 925, rail_y + 8, rail_h - 16, TFT_DARKGREY);
  char value[128];
  M5.Display.setTextDatum(middle_left);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_CYAN);
  if (position.node != 0) {
    char latitude[20];
    char longitude[20];
    format_lora_e7(latitude, sizeof(latitude), position.latitude_e7);
    format_lora_e7(longitude, sizeof(longitude), position.longitude_e7);
    snprintf(value, sizeof(value), "GPS !%08lx   %s, %s",
             static_cast<unsigned long>(position.node), latitude, longitude);
  } else {
    strlcpy(value, "GPS  WAITING FOR POSITION", sizeof(value));
  }
  M5.Display.drawString(value, kSpectrumX + 18, rail_y + rail_h / 2);
  snprintf(value, sizeof(value), "LF S%d  |  SF%u  |  %uk",
           lora_frequency_slot(rtl_ui_frequency_hz),
           static_cast<unsigned>(lora_sf.load(std::memory_order_relaxed)),
           static_cast<unsigned>(lora_bandwidth_hz.load(std::memory_order_relaxed) / 1000u));
  M5.Display.setTextColor(TFT_GREEN);
  M5.Display.drawString(value, kSpectrumX + 628, rail_y + rail_h / 2);
  const uint32_t age = packet.sender == 0 ? 0 : (millis() - packet.received_ms) / 1000u;
  snprintf(value, sizeof(value), "PACKETS %lu  AGE %lus  DROP %lu",
           static_cast<unsigned long>(count), static_cast<unsigned long>(age),
           static_cast<unsigned long>(
               lora_log_dropped.load(std::memory_order_relaxed)));
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_LIGHTGREY);
  M5.Display.drawString(value, kSpectrumX + 943, rail_y + 16);
  constexpr int log_box_x = kSpectrumX + 943;
  constexpr int log_box_y = rail_y + 31;
  constexpr int log_box_size = 18;
  const uint16_t log_color = log_state == 3   ? TFT_RED
                             : log_state == 2 ? TFT_GREEN
                             : log_state == 1 ? TFT_YELLOW
                                              : TFT_LIGHTGREY;
  M5.Display.drawRect(log_box_x, log_box_y, log_box_size, log_box_size, log_color);
  if (log_state == 2) {
    M5.Display.drawLine(log_box_x + 4, log_box_y + 9, log_box_x + 8,
                        log_box_y + 13, log_color);
    M5.Display.drawLine(log_box_x + 8, log_box_y + 13, log_box_x + 15,
                        log_box_y + 4, log_color);
  }
  const char* log_label = log_state == 3   ? "SD ERROR"
                          : log_state == 2 ? "SD LOG ON"
                          : log_state == 1 ? "SD STARTING"
                                           : "SD LOG OFF";
  M5.Display.setTextColor(log_color);
  M5.Display.drawString(log_label, log_box_x + 27, log_box_y + 9);
}


void draw_fm_dashboard(bool static_panel) {
  const auto snapshot = fm_dashboard_snapshot();
  if (static_panel) orcsdr::fm::enter(snapshot);
  else if (orcsdr::fm::active()) orcsdr::fm::update(snapshot);
}

void draw_p25_dashboard(bool static_panel) {
  const auto snapshot = p25_dashboard_snapshot();
  if (static_panel) orcsdr::p25::enter(snapshot);
  else if (orcsdr::p25::active()) orcsdr::p25::update(snapshot);
}

void draw_sdr_screen(RtlBand band, uint32_t frequency_hz, uint8_t volume) {
  orcsdr::home::leave();
  if (band == RtlBand::adsb) {
    orcsdr::p25::leave();
    if (!orcsdr::adsb::active()) orcsdr::adsb::enter(adsb_settings);
    else orcsdr::adsb::draw();
    draw_global_settings_gear();
    return;
  }
  if (band == RtlBand::fm) {
    orcsdr::p25::leave();
    reset_spectrum_renderer();
    draw_fm_dashboard(true);
    return;
  }
  if (band == RtlBand::p25) {
    orcsdr::fm::leave();
    reset_spectrum_renderer();
    draw_p25_dashboard(true);
    return;
  }
  orcsdr::fm::leave();
  orcsdr::p25::leave();
  M5.Display.fillScreen(TFT_BLACK);
  draw_sdr_header(band, frequency_hz, volume);
  if (band == RtlBand::lora) {
    draw_lora_dashboard(true);
    draw_sdr_controls(band, true);
    return;
  }
  const int width = spectrum_draw_width();
  M5.Display.drawRect(kSpectrumX, kSpectrumY, width, kSpectrumHeight,
                      TFT_DARKGREY);
  for (int line = 1; line < 4; ++line) {
    const int y = kSpectrumY + line * kSpectrumHeight / 4;
    M5.Display.drawFastHLine(kSpectrumX, y, width, 0x2104);
  }
  for (int line = 0; line <= 4; ++line) {
    const int x = kSpectrumX + line * width / 4;
    M5.Display.drawFastVLine(x, kSpectrumY, kSpectrumHeight, 0x2104);
  }
  draw_spectrum_axis();
  M5.Display.fillRect(kSpectrumX, kWaterfallY, width, kWaterfallHeight,
                      TFT_BLACK);
  M5.Display.drawRect(kSpectrumX, kWaterfallY, width, kWaterfallHeight,
                      TFT_DARKGREY);
  M5.Display.setScrollRect(kSpectrumX + 1, kWaterfallY + 1, width - 2,
                           kWaterfallHeight - 2, TFT_BLACK);
  reset_spectrum_renderer();
  draw_spectrum_grid();
  draw_band_edges();
  draw_cb_dashboard(true);
  draw_lora_dashboard(true);
  draw_sdr_controls(band, true);
}

uint16_t waterfall_color(float level) {
  level = constrain(level, 0.0f, 1.0f);
  uint8_t red = 0;
  uint8_t green = 0;
  uint8_t blue = 0;
  if (level < 0.25f) {
    blue = static_cast<uint8_t>(40 + level * 4 * 180);
  } else if (level < 0.5f) {
    const float ramp = (level - 0.25f) * 4;
    green = static_cast<uint8_t>(ramp * 220);
    blue = 220;
  } else if (level < 0.75f) {
    const float ramp = (level - 0.5f) * 4;
    red = static_cast<uint8_t>(ramp * 255);
    green = 220;
    blue = static_cast<uint8_t>((1.0f - ramp) * 220);
  } else {
    const float ramp = (level - 0.75f) * 4;
    red = 255;
    green = static_cast<uint8_t>(220 + ramp * 35);
    blue = static_cast<uint8_t>(ramp * 255);
  }
  return M5.Display.color565(red, green, blue);
}

void draw_documentation_spectrum() {
  const int width = spectrum_draw_width() - 2;
  uint8_t strength[kSpectrumWidth];
  for (int x = 0; x < width; ++x) {
    const float center = static_cast<float>(x - width / 2);
    const float side = static_cast<float>(x - width * 3 / 4);
    const float level = 24.0f + 190.0f * expf(-(center * center) / 850.0f) +
                        95.0f * expf(-(side * side) / 260.0f) +
                        8.0f * sinf(static_cast<float>(x) * 0.17f);
    strength[x] = static_cast<uint8_t>(constrain(level, 0.0f, 255.0f));
  }

  M5.Display.startWrite();
  int previous_y = kSpectrumY + kSpectrumHeight - 2;
  for (int x = 0; x < width; ++x) {
    const int y = kSpectrumY + kSpectrumHeight - 2 -
                  strength[x] * (kSpectrumHeight - 4) / 255;
    if (x != 0)
      M5.Display.drawLine(kSpectrumX + x, previous_y, kSpectrumX + x + 1, y,
                          TFT_CYAN);
    previous_y = y;
  }
  for (int row = 1; row < kWaterfallHeight - 1; ++row) {
    const int shift = (row / 12) % 9 - 4;
    for (int x = 0; x < width; ++x) {
      const int source = constrain(x + shift, 0, width - 1);
      const int noise = ((x * 13 + row * 29) & 31) - 15;
      rtl_waterfall_row[x] = waterfall_color(
          constrain((static_cast<int>(strength[source]) + noise) / 255.0f,
                    0.0f, 1.0f));
    }
    M5.Display.pushImage(kSpectrumX + 1, kWaterfallY + row, width, 1,
                         rtl_waterfall_row);
  }
  M5.Display.drawFastVLine(kSpectrumX + width / 2, kSpectrumY + 1,
                           kSpectrumHeight - 2, TFT_GREEN);
  M5.Display.endWrite();
}

void draw_lora_live_graphs(size_t first_bin, size_t visible_bins, float floor) {
  constexpr int scope_x = kSpectrumX + 712;
  constexpr int scope_y = kSpectrumY + 78;
  constexpr int scope_w = 366;
  constexpr int scope_h = 80;
  constexpr int waterfall_x = kSpectrumX + 46;
  constexpr int waterfall_y = kSpectrumY + 270;
  constexpr int waterfall_w = 616;
  constexpr int waterfall_h = 150;

  M5.Display.startWrite();
  M5.Display.fillRect(scope_x, scope_y, scope_w, scope_h, TFT_BLACK);
  for (int line = 1; line < 4; ++line) {
    M5.Display.drawFastVLine(scope_x + line * scope_w / 4, scope_y, scope_h, 0x2104);
  }
  M5.Display.drawFastHLine(scope_x, scope_y + scope_h / 2, scope_w, 0x2104);
  int previous_x = scope_x;
  int previous_y = scope_y + scope_h - 1;
  for (size_t bin = first_bin; bin < first_bin + visible_bins; ++bin) {
    const float normalized =
        constrain((rtl_spectrum_levels[bin] - floor) / 48.0f, 0.0f, 1.0f);
    const int x = scope_x + static_cast<int>((bin - first_bin) * (scope_w - 1) /
                                              (visible_bins - 1));
    const int y = scope_y + scope_h - 1 - static_cast<int>(normalized * (scope_h - 2));
    if (bin != first_bin) M5.Display.drawLine(previous_x, previous_y, x, y, TFT_CYAN);
    previous_x = x;
    previous_y = y;
  }
  M5.Display.drawFastVLine(scope_x + scope_w / 2, scope_y, scope_h, TFT_GREEN);

  M5.Display.setScrollRect(waterfall_x, waterfall_y, waterfall_w, waterfall_h,
                           TFT_BLACK);
  M5.Display.scroll(0, -1);
  for (size_t bin = first_bin; bin < first_bin + visible_bins; ++bin) {
    const float normalized =
        constrain((rtl_spectrum_levels[bin] - floor) / 48.0f, 0.0f, 1.0f);
    const int cell_x = static_cast<int>((bin - first_bin) * waterfall_w / visible_bins);
    const int next_x = static_cast<int>((bin - first_bin + 1) * waterfall_w /
                                        visible_bins);
    const uint16_t color = waterfall_color(normalized);
    for (int pixel = cell_x; pixel < next_x; ++pixel) rtl_waterfall_row[pixel] = color;
  }
  M5.Display.pushImage(waterfall_x, waterfall_y + waterfall_h - 1,
                       waterfall_w, 1, rtl_waterfall_row);
  M5.Display.drawFastVLine(waterfall_x + waterfall_w / 2, waterfall_y,
                           waterfall_h, TFT_GREEN);
  M5.Display.endWrite();
}

void reset_spectrum_renderer() {
  rtl_spectrum_trace_valid = false;
  rtl_spectrum_last_ms = 0;
  rtl_spectrum_trace_last_ms = 0;
  rtl_spectrum_frames = 0;
  rtl_spectrum_fps_window_ms = millis();
  rtl_spectrum_fps = 0;
  for (size_t index = 0; index < kRtlSpectrumBins; ++index) {
    rtl_spectrum_smooth[index] = -80.0f;
    rtl_spectrum_peak[index] = -120.0f;
    rtl_spectrum_y[index] = kSpectrumY + kSpectrumHeight - 2;
    rtl_spectrum_peak_y[index] = kSpectrumY + kSpectrumHeight - 2;
  }
}

int spectrum_draw_width() {
  if (rtl_nav_open) return kNavPanelX - kSpectrumX - 1;
  return (rtl_ui_band == RtlBand::cb || rtl_ui_band == RtlBand::lora)
             ? kCbSpectrumWidth
             : kSpectrumWidth;
}

void draw_spectrum_grid() {
  const int width = spectrum_draw_width();
  for (int line = 1; line < 4; ++line) {
    const int y = kSpectrumY + line * kSpectrumHeight / 4;
    M5.Display.drawFastHLine(kSpectrumX + 1, y, width - 2, 0x2104);
  }
  M5.Display.drawFastVLine(kSpectrumX + width / 2, kSpectrumY + 1,
                           kSpectrumHeight - 2, TFT_GREEN);
}

void redraw_spectrum_panel() {
  M5.Display.fillRect(kSpectrumX + 1, kSpectrumY + 1, spectrum_draw_width() - 2,
                      kSpectrumHeight - 2, TFT_BLACK);
  reset_spectrum_renderer();
  draw_spectrum_grid();
  draw_band_edges();
}

void draw_spectrum_axis() {
  const uint32_t span_hz = rtl_scope_span_hz.load(std::memory_order_relaxed);
  const int width = spectrum_draw_width();
  const double center = rtl_ui_frequency_hz / 1000000.0;
  const double half_span = static_cast<double>(span_hz) / 2000000.0;
  char label[32];
  M5.Display.fillRect(kSpectrumX - 24, kSpectrumY + kSpectrumHeight + 1,
                      width + 24, 19, TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(2);
  for (int marker = 0; marker <= 4; ++marker) {
    const double mark = center - half_span + marker * (half_span / 2.0);
    if (rtl_ui_band == RtlBand::cb) {
      const size_t channel = cb_channel_index(static_cast<uint32_t>(mark * 1000000.0));
      snprintf(label, sizeof(label), "CH %u", static_cast<unsigned>(channel + 1));
    } else if (rtl_ui_frequency_hz >= 1000000) {
      snprintf(label, sizeof(label), marker == 4 ? "%.3f MHz" : "%.3f", mark);
    } else {
      snprintf(label, sizeof(label), marker == 4 ? "%.1f kHz" : "%.1f", mark * 1000.0);
    }
    M5.Display.setTextColor(marker == 2 ? TFT_GREEN : TFT_LIGHTGREY, TFT_BLACK);
    M5.Display.drawString(label, kSpectrumX + marker * width / 4,
                          kSpectrumY + kSpectrumHeight + 11);
  }
}

void draw_band_edges() {
  const uint32_t span_hz = rtl_scope_span_hz.load(std::memory_order_relaxed);
  const uint32_t bandwidth_hz = rtl_filter_bandwidth_hz.load(std::memory_order_relaxed);
  const int width = spectrum_draw_width();
  const int half_width = constrain(
      static_cast<int>((static_cast<uint64_t>(bandwidth_hz) * width) /
                       (2u * span_hz)),
      3, width / 2 - 2);
  const int center = kSpectrumX + width / 2;
  for (int offset = -1; offset <= 1; ++offset) {
    M5.Display.drawFastVLine(center - half_width + offset, kSpectrumY + 1,
                             kSpectrumHeight - 2, TFT_YELLOW);
    M5.Display.drawFastVLine(center + half_width + offset, kSpectrumY + 1,
                             kSpectrumHeight - 2, TFT_YELLOW);
  }
}

/**
 * RF scope: 256-bin FFT, Welch multi-window average, peak-hold envelope.
 * Prefer a frozen IQ snapshot so demod can keep writing the live buffer.
 * Two-window Welch averaging keeps the single render core responsive.
 */
/**
 * RF scope: 256-bin FFT, Welch multi-window average, peak-hold envelope.
 * Prefer a frozen IQ snapshot so demod can keep writing the live buffer.
 * Two-window Welch averaging keeps the single render core responsive.
 */
void draw_spectrum(const uint8_t* iq, size_t bytes) {
  if (!orcsdr::home::active() && rtl_ui_band == RtlBand::lora &&
      lora_view != LoraView::Live) return;
  if (!orcsdr::home::active() && rtl_ui_band == RtlBand::fm &&
      !orcsdr::fm::spectrum_active()) return;
  if (!orcsdr::home::active() && rtl_ui_band == RtlBand::p25 &&
      !orcsdr::p25::spectrum_active()) return;
  if (!rtl_spectrum_window_ready) {
    constexpr float kPi = 3.14159265358979323846f;
    for (size_t index = 0; index < kRtlSpectrumBins; ++index) {
      rtl_spectrum_window[index] =
          0.5f - 0.5f * cosf(2.0f * kPi * index / (kRtlSpectrumBins - 1));
    }
    rtl_spectrum_window_ready = true;
  }
  uint8_t local_iq[sizeof(rtl_spectrum_iq_snap)];
  size_t local_bytes = 0;
  portENTER_CRITICAL(&rtl_spectrum_snap_mux);
  if (rtl_spectrum_iq_snap_bytes >= kRtlSpectrumBins * 2) {
    local_bytes = rtl_spectrum_iq_snap_bytes;
    memcpy(local_iq, rtl_spectrum_iq_snap, local_bytes);
  }
  portEXIT_CRITICAL(&rtl_spectrum_snap_mux);
  if (local_bytes < kRtlSpectrumBins * 2) {
    if (iq == nullptr || bytes < kRtlSpectrumBins * 2) return;
    local_bytes = bytes < sizeof(local_iq) ? bytes : sizeof(local_iq);
    memcpy(local_iq, iq, local_bytes);
  }

  const uint32_t now = millis();
  constexpr uint32_t spectrum_interval = kRtlSpectrumIntervalMs;
  if (rtl_spectrum_last_ms != 0 &&
      now - rtl_spectrum_last_ms < spectrum_interval) {
    return;
  }
  rtl_spectrum_last_ms = now;

  const OrcTool tool = orc_tool_current();
  const size_t welch_n = kRtlSpectrumWelchWindows;
  const size_t window_bytes = kRtlSpectrumBins * 2;
  const size_t max_windows = local_bytes / window_bytes;
  const size_t windows =
      welch_n < max_windows ? welch_n : (max_windows > 0 ? max_windows : 1);

  float power_acc[kRtlSpectrumBins];
  for (size_t b = 0; b < kRtlSpectrumBins; ++b) power_acc[b] = 0.0f;

  constexpr float kPi = 3.14159265358979323846f;
  for (size_t w = 0; w < windows; ++w) {
    const uint8_t* base = local_iq + w * window_bytes;
    for (size_t index = 0; index < kRtlSpectrumBins; ++index) {
      rtl_spectrum_real[index] =
          (static_cast<int>(base[index * 2]) - 128) * rtl_spectrum_window[index];
      rtl_spectrum_imaginary[index] =
          (static_cast<int>(base[index * 2 + 1]) - 128) * rtl_spectrum_window[index];
    }
    for (size_t index = 1, reversed = 0; index < kRtlSpectrumBins; ++index) {
      size_t bit = kRtlSpectrumBins >> 1;
      for (; reversed & bit; bit >>= 1) reversed ^= bit;
      reversed ^= bit;
      if (index < reversed) {
        std::swap(rtl_spectrum_real[index], rtl_spectrum_real[reversed]);
        std::swap(rtl_spectrum_imaginary[index], rtl_spectrum_imaginary[reversed]);
      }
    }
    for (size_t length = 2; length <= kRtlSpectrumBins; length <<= 1) {
      const float angle = -2.0f * kPi / length;
      const float step_real = cosf(angle);
      const float step_imaginary = sinf(angle);
      for (size_t base_i = 0; base_i < kRtlSpectrumBins; base_i += length) {
        float twiddle_real = 1;
        float twiddle_imaginary = 0;
        for (size_t offset = 0; offset < length / 2; ++offset) {
          const size_t upper = base_i + offset;
          const size_t lower = upper + length / 2;
          const float product_real = rtl_spectrum_real[lower] * twiddle_real -
                                     rtl_spectrum_imaginary[lower] * twiddle_imaginary;
          const float product_imaginary =
              rtl_spectrum_real[lower] * twiddle_imaginary +
              rtl_spectrum_imaginary[lower] * twiddle_real;
          rtl_spectrum_real[lower] = rtl_spectrum_real[upper] - product_real;
          rtl_spectrum_imaginary[lower] =
              rtl_spectrum_imaginary[upper] - product_imaginary;
          rtl_spectrum_real[upper] += product_real;
          rtl_spectrum_imaginary[upper] += product_imaginary;
          const float next_real = twiddle_real * step_real -
                                  twiddle_imaginary * step_imaginary;
          twiddle_imaginary = twiddle_real * step_imaginary +
                              twiddle_imaginary * step_real;
          twiddle_real = next_real;
        }
      }
    }
    for (size_t bin = 0; bin < kRtlSpectrumBins; ++bin) {
      const size_t shifted = (bin + kRtlSpectrumBins / 2) % kRtlSpectrumBins;
      const float p =
          rtl_spectrum_real[shifted] * rtl_spectrum_real[shifted] +
          rtl_spectrum_imaginary[shifted] * rtl_spectrum_imaginary[shifted];
      power_acc[bin] += p;
    }
  }

  size_t visible_bins = static_cast<size_t>(
      (static_cast<uint64_t>(rtl_scope_span_hz.load(std::memory_order_relaxed)) *
       kRtlSpectrumBins) /
      kRtlScopeSpanMaxHz);
  visible_bins = constrain(visible_bins, static_cast<size_t>(32), kRtlSpectrumBins);
  const size_t first_bin = (kRtlSpectrumBins - visible_bins) / 2;
  const size_t last_bin = first_bin + visible_bins;
  const int draw_width = spectrum_draw_width();
  float maximum = -120.0f;
  float strongest = -120.0f;
  size_t strongest_bin = kRtlSpectrumBins / 2;
  const float inv_w = 1.0f / static_cast<float>(windows);
  const bool home_active = orcsdr::home::active();
  for (size_t bin = 0; bin < kRtlSpectrumBins; ++bin) {
    const float level = 10.0f * log10f(power_acc[bin] * inv_w + 1.0f);
    /* EMA average (cyan) + slow peak-hold (orange) for interference spotting. */
    rtl_spectrum_smooth[bin] = rtl_spectrum_trace_valid
                                   ? (0.78f * rtl_spectrum_smooth[bin] + 0.22f * level)
                                   : level;
    if (!rtl_spectrum_trace_valid || level > rtl_spectrum_peak[bin]) {
      rtl_spectrum_peak[bin] = level;
    } else {
      rtl_spectrum_peak[bin] = 0.995f * rtl_spectrum_peak[bin] + 0.005f * level;
    }
    rtl_spectrum_levels[bin] = home_active ? level : rtl_spectrum_smooth[bin];
    if (bin >= first_bin && bin < last_bin) {
      maximum = max(maximum, max(rtl_spectrum_levels[bin], rtl_spectrum_peak[bin]));
      if (rtl_spectrum_levels[bin] > strongest) {
        strongest = rtl_spectrum_levels[bin];
        strongest_bin = bin;
      }
    }
  }
  const int32_t peak_offset_hz = static_cast<int32_t>(
      (static_cast<int64_t>(strongest_bin) - static_cast<int64_t>(kRtlSpectrumBins / 2)) *
      static_cast<int64_t>(kRtlSampleRateSps) / static_cast<int64_t>(kRtlSpectrumBins));
  rtl_scope_peak_offset_hz.store(peak_offset_hz, std::memory_order_relaxed);
  rtl_scope_peak_level.store(strongest, std::memory_order_relaxed);
  const float floor = maximum - 48.0f;
  const bool redraw_trace =
      !rtl_spectrum_trace_valid ||
      (now - rtl_spectrum_trace_last_ms) >= spectrum_interval;
  const bool show_peak = (tool == OrcTool::Scope || tool == OrcTool::Radio);

  if (orcsdr::home::active()) {
    const bool audio_stressed = rtl_audio.dropped_chunks > 0 &&
        rtl_audio.dropped_chunks * 2u > rtl_audio.queued_chunks + 2u;
    orcsdr::home::draw_spectrum(rtl_spectrum_levels, first_bin, visible_bins,
                                floor, audio_stressed);
    rtl_spectrum_trace_last_ms = now;
    rtl_spectrum_trace_valid = true;
    ++rtl_spectrum_frames;
    return;
  }

  if (rtl_ui_band == RtlBand::lora) {
    draw_lora_live_graphs(first_bin, visible_bins, floor);
    rtl_spectrum_trace_last_ms = now;
    rtl_spectrum_trace_valid = true;
    ++rtl_spectrum_frames;
    return;
  }
  if (rtl_ui_band == RtlBand::fm) {
    orcsdr::fm::draw_spectrum(rtl_spectrum_levels, first_bin, visible_bins, floor);
    rtl_spectrum_trace_last_ms = now;
    rtl_spectrum_trace_valid = true;
    ++rtl_spectrum_frames;
    return;
  }
  if (rtl_ui_band == RtlBand::p25) {
    orcsdr::p25::draw_spectrum(rtl_spectrum_levels, first_bin, visible_bins, floor);
    rtl_spectrum_trace_last_ms = now;
    rtl_spectrum_trace_valid = true;
    ++rtl_spectrum_frames;
    return;
  }

  M5.Display.startWrite();
  if (redraw_trace) {
    M5.Display.fillRect(kSpectrumX + 1, kSpectrumY + 1, draw_width - 2,
                        kSpectrumHeight - 2, TFT_BLACK);
    draw_spectrum_grid();
  }

  M5.Display.scroll(0, -1);
  const int waterfall_width = draw_width - 2;
  int previous_x = kSpectrumX;
  int previous_y = kSpectrumY + kSpectrumHeight - 2;
  int prev_peak_x = kSpectrumX;
  int prev_peak_y = kSpectrumY + kSpectrumHeight - 2;
  for (size_t bin = first_bin; bin < last_bin; ++bin) {
    const float normalized =
        constrain((rtl_spectrum_levels[bin] - floor) / 48.0f, 0.0f, 1.0f);
    const float peak_n =
        constrain((rtl_spectrum_peak[bin] - floor) / 48.0f, 0.0f, 1.0f);
    const int x = kSpectrumX + static_cast<int>((bin - first_bin) * draw_width /
                                                 (visible_bins - 1));
    const int y = kSpectrumY + kSpectrumHeight - 2 -
                  static_cast<int>(normalized * (kSpectrumHeight - 4));
    const int py = kSpectrumY + kSpectrumHeight - 2 -
                   static_cast<int>(peak_n * (kSpectrumHeight - 4));
    if (redraw_trace) {
      rtl_spectrum_y[bin] = static_cast<int16_t>(y);
      rtl_spectrum_peak_y[bin] = static_cast<int16_t>(py);
      if (bin != first_bin) {
        if (show_peak) {
          M5.Display.drawLine(prev_peak_x, prev_peak_y, x, py, TFT_ORANGE);
        }
        M5.Display.drawLine(previous_x, previous_y, x, y, TFT_CYAN);
      }
      previous_x = x;
      previous_y = y;
      prev_peak_x = x;
      prev_peak_y = py;
    }
    const int cell_x = static_cast<int>((bin - first_bin) * waterfall_width / visible_bins);
    const int next_x = static_cast<int>((bin - first_bin + 1) * waterfall_width /
                                        visible_bins);
    const uint16_t color = waterfall_color(normalized);
    for (int pixel = cell_x; pixel < next_x; ++pixel) {
      rtl_waterfall_row[pixel] = color;
    }
  }
  /* Capture tool owns waterfall panel — skip scrolling paint there. */
  if (tool != OrcTool::Capture) {
    M5.Display.pushImage(kSpectrumX + 1, kWaterfallY + kWaterfallHeight - 2,
                         waterfall_width, 1, rtl_waterfall_row);
  }
  if (redraw_trace) {
    M5.Display.drawFastVLine(kSpectrumX + draw_width / 2, kSpectrumY + 1,
                             kSpectrumHeight - 2, TFT_GREEN);
    rtl_spectrum_trace_last_ms = now;
    rtl_spectrum_trace_valid = true;
  }
  draw_band_edges();
  M5.Display.endWrite();

  ++rtl_spectrum_frames;
  if (now - rtl_spectrum_fps_window_ms >= 1000) {
    const uint32_t window_ms = now - rtl_spectrum_fps_window_ms;
    rtl_spectrum_fps = static_cast<uint16_t>(rtl_spectrum_frames);
    rtl_spectrum_frames = 0;
    rtl_spectrum_fps_window_ms = now;
    const uint32_t dsp_us = rtl_dsp_window_us.exchange(0, std::memory_order_acq_rel);
    const uint32_t dsp_blocks =
        rtl_dsp_window_blocks.exchange(0, std::memory_order_acq_rel);
    const uint32_t dsp_max_us =
        rtl_dsp_block_us_max.exchange(0, std::memory_order_acq_rel);
    const uint32_t dsp_load_pct =
        window_ms == 0 ? 0 : static_cast<uint32_t>(
                                  (static_cast<uint64_t>(dsp_us) * 100u) /
                                  (static_cast<uint64_t>(window_ms) * 1000u));
    if (kStreamDiagnosticsEnabled) {
      Serial.printf("RTL_SPECTRUM_FPS fps=%u bins=%u welch=%u tool=%s audio_dropped=%u "
                    "audio_chunks=%u audio_peak=%d dsp_load_pct=%u dsp_blocks=%u "
                    "dsp_block_us_max=%u\n",
                    rtl_spectrum_fps, static_cast<unsigned>(kRtlSpectrumBins),
                    static_cast<unsigned>(windows), orc_tool_name(tool),
                    rtl_audio.dropped_chunks, rtl_audio.queued_chunks, rtl_audio.peak,
                    dsp_load_pct, dsp_blocks, dsp_max_us);
    }
  }
}

float fast_phase(float cross, float dot) {
  // ponytail: ~0.07 rad atan2 approximation; use a DSP vector atan2 only if
  // measured demodulation quality requires it and still sustains 2.4 MS/s.
  constexpr float kQuarterPi = 0.78539816339f;
  constexpr float kThreeQuarterPi = 2.35619449019f;
  const float magnitude = fabsf(cross) + 1e-10f;
  float angle;
  if (dot >= 0) {
    const float ratio = (dot - magnitude) / (dot + magnitude);
    angle = kQuarterPi - kQuarterPi * ratio;
  } else {
    const float ratio = (dot + magnitude) / (magnitude - dot);
    angle = kThreeQuarterPi - kQuarterPi * ratio;
  }
  return cross < 0 ? -angle : angle;
}

// Rational tanh approximation of the output soft-knee limiter, factored out
// of shape_audio_sample() so the L/R dashboard meter can clamp to the same
// ceiling the actual speaker/WAV samples are limited to. Without this, the
// meter read the pre-limit signal directly and could swing well past 0 dBFS
// (unbounded), which pinned the VU needle at max regardless of real loudness.
inline float fm_soft_limit(float x) {
  constexpr float kLimit = 12000.0f;
  const float normalized = x / kLimit;
  const float normalized_sq = normalized * normalized;
  if (normalized_sq < 9.0f) {
    return kLimit * normalized * (27.0f + normalized_sq) /
           (27.0f + 9.0f * normalized_sq);
  }
  return x < 0.0f ? -kLimit : kLimit;
}

// Soft AGC + rational tanh approximation (avoids a 48 kHz libm call).
int16_t shape_audio_sample(float demodulated, float base_scale) {
  float x = demodulated * base_scale * rtl_audio.agc_gain;
  const float ax = fabsf(x);
  rtl_audio.agc_level = 0.995f * rtl_audio.agc_level + 0.005f * ax;
  if (rtl_audio.agc_level > 350.0f) {
    const float desired = 4800.0f / rtl_audio.agc_level;
    rtl_audio.agc_gain = 0.98f * rtl_audio.agc_gain + 0.02f * desired;
    if (rtl_audio.agc_gain < 0.15f) rtl_audio.agc_gain = 0.15f;
    if (rtl_audio.agc_gain > 2.8f) rtl_audio.agc_gain = 2.8f;
  }
  x = fm_soft_limit(x);
  if (rtl_audio.fade_in < 192) {
    x *= static_cast<float>(rtl_audio.fade_in) / 192.0f;
    ++rtl_audio.fade_in;
  }
  x = 0.90f * x + 0.10f * rtl_audio.last_out;
  rtl_audio.last_out = x;
  const int32_t sample = lroundf(x);
  return static_cast<int16_t>(constrain(sample, -15000, 15000));
}

void queue_audio_samples(int16_t* audio, size_t audio_count) {
  if (audio_count == 0) return;
  /* Capture the post-DSP mono stream before expanding it for the stereo codec. */
  if (g_audio_rec_active.load(std::memory_order_acquire)) {
    audio_rec_append(audio, audio_count);
  }
  /* M5Unified retains playRaw pointers, so fill one owned stereo block at a time. */
  for (size_t i = 0; i < audio_count; ++i) {
    if (!rtl_audio_select_writable_block(millis())) break;
    if (rtl_audio_play_count >= kRtlAudioPlayBlockFrames) {
      flush_audio_play_batch(true);
      if (!rtl_audio_select_writable_block(millis())) break;
    }
    int16_t* const output = rtl_audio_play_blocks[rtl_audio_play_block_index];
    output[rtl_audio_play_count * 2] = audio[i];
    output[rtl_audio_play_count * 2 + 1] = audio[i];
    ++rtl_audio_play_count;
  }
  flush_audio_play_batch(false);
  rtl_audio.buffer = (rtl_audio.buffer + 1) % std::size(rtl_audio_buffers);
}

// TIA-102.BABA IMBE channel deinterleave: vector bit -> transmitted bit.
// Cross-checked against the Apache-2.0 GopherTrunk implementation and the
// ISC-licensed DSD/mbelib schedule; mbelib then owns FEC and synthesis.
constexpr uint8_t kP25ImbeDeinterleave[orcsdr::p25decoder::kVoiceFrameBits] = {
    132,127,120,115,108,103,96,91,84,79,72,67,60,55,48,43,36,31,24,19,12,7,0,
    126,121,114,109,102,97,90,85,78,73,66,61,54,49,42,37,30,25,18,13,6,1,139,
    122,117,110,105,98,93,86,81,74,69,62,57,50,45,38,33,26,21,14,9,2,138,133,
    116,111,104,99,92,87,80,75,68,63,56,51,44,39,32,27,20,15,8,3,141,134,129,
    64,59,52,47,40,35,28,23,16,11,4,140,135,128,123,10,5,143,136,131,124,119,
    112,107,100,95,88,83,76,71,101,94,89,82,77,70,65,58,53,46,41,34,29,22,17,
    142,137,130,125,118,113,106};

void p25_imbe_matrix(const orcsdr::p25decoder::VoiceFrame& frame, char output[8][23]) {
  constexpr uint8_t kRows[8] = {23, 23, 23, 23, 15, 15, 15, 7};
  size_t vector = 0;
  std::memset(output, 0, 8 * 23);
  for (size_t row = 0; row < std::size(kRows); ++row)
    for (size_t column = 0; column < kRows[row]; ++column)
      output[row][column] = static_cast<char>(frame.bits[kP25ImbeDeinterleave[vector++]]);
}

bool p25_voice_self_check() {
  constexpr uint8_t kOnAir[18] = {
      0x84,0xC6,0xA9,0x94,0x03,0xFF,0x81,0xC8,0x26,
      0x14,0x2C,0x03,0x90,0xEC,0x85,0x33,0x59,0xBC};
  constexpr uint8_t kExpected[11] = {
      0x89,0xEC,0x59,0x0E,0xB5,0x6D,0x85,0xFE,0x76,0xC4,0xC0};
  orcsdr::p25decoder::VoiceFrame frame;
  for (size_t bit = 0; bit < orcsdr::p25decoder::kVoiceFrameBits; ++bit)
    frame.bits[bit] = (kOnAir[bit / 8] >> (7 - bit % 8)) & 1u;
  char matrix[8][23];
  char decoded[88]{};
  p25_imbe_matrix(frame, matrix);
  (void)mbe_eccImbe7200x4400C0(matrix);
  mbe_demodulateImbe7200x4400Data(matrix);
  (void)mbe_eccImbe7200x4400Data(matrix, decoded);
  for (size_t bit = 0; bit < 88; ++bit)
    if ((decoded[bit] & 1) != ((kExpected[bit / 8] >> (7 - bit % 8)) & 1u)) return false;
  return true;
}

void p25_voice_task(void*) {
  auto& context = p25_voice_context;
  mbe_initMbeParms(&context.current, &context.previous, &context.enhanced);
  uint32_t session = p25_voice_session.load(std::memory_order_acquire);
  for (;;) {
    orcsdr::p25decoder::VoiceFrame frame;
    if (!orcsdr::p25decoder::pop_voice_frame(&frame)) {
      vTaskDelay(pdMS_TO_TICKS(2));
      continue;
    }
    const uint32_t next_session = p25_voice_session.load(std::memory_order_acquire);
    if (next_session != session) {
      session = next_session;
      context.previous_sample = 0;
      mbe_initMbeParms(&context.current, &context.previous, &context.enhanced);
    }
    if (g_stream_band != RtlBand::p25 ||
        p25_follow_state.load(std::memory_order_acquire) != P25FollowState::voice ||
        !rtl_audio_user_enabled.load(std::memory_order_acquire)) continue;

    const int64_t started_us = esp_timer_get_time();
    std::memset(context.decoded, 0, sizeof(context.decoded));
    std::memset(context.error_text, 0, sizeof(context.error_text));
    int errors = 0, total_errors = 0;
    p25_imbe_matrix(frame, context.matrix);
    mbe_processImbe7200x4400Frame(
        context.pcm8k, &errors, &total_errors, context.error_text, context.matrix,
        context.decoded, &context.current, &context.previous, &context.enhanced, 1);
    const uint32_t synth_us = static_cast<uint32_t>(esp_timer_get_time() - started_us);
    uint32_t previous_synth_max = p25_imbe_synth_max_us.load(std::memory_order_relaxed);
    while (synth_us > previous_synth_max &&
           !p25_imbe_synth_max_us.compare_exchange_weak(
               previous_synth_max, synth_us, std::memory_order_relaxed)) {}
    size_t output = 0;
    for (const int16_t sample : context.pcm8k) {
      const int32_t delta = static_cast<int32_t>(sample) - context.previous_sample;
      for (int phase = 1; phase <= 6; ++phase)
        context.pcm48k[output++] = static_cast<int16_t>(
            context.previous_sample + delta * phase / 6);
      context.previous_sample = sample;
    }
    p25_imbe_frames.fetch_add(1, std::memory_order_relaxed);
    p25_imbe_errors.fetch_add(static_cast<uint32_t>(std::max(0, total_errors)),
                              std::memory_order_relaxed);
    p25_pcm_frames.fetch_add(output, std::memory_order_relaxed);
    p25_audio_last_ms.store(millis(), std::memory_order_release);
    const uint32_t stack_hwm = uxTaskGetStackHighWaterMark(nullptr);
    uint32_t previous_hwm = p25_voice_stack_hwm.load(std::memory_order_relaxed);
    while ((previous_hwm == 0 || stack_hwm < previous_hwm) &&
           !p25_voice_stack_hwm.compare_exchange_weak(
               previous_hwm, stack_hwm, std::memory_order_relaxed)) {}
    const int64_t audio_started_us = esp_timer_get_time();
    queue_audio_samples(context.pcm48k, output);
    const uint32_t audio_us = static_cast<uint32_t>(esp_timer_get_time() - audio_started_us);
    uint32_t previous_audio_max = p25_audio_queue_max_us.load(std::memory_order_relaxed);
    while (audio_us > previous_audio_max &&
           !p25_audio_queue_max_us.compare_exchange_weak(
               previous_audio_max, audio_us, std::memory_order_relaxed)) {}
    const uint32_t elapsed_us = static_cast<uint32_t>(esp_timer_get_time() - started_us);
    uint32_t previous_max = p25_imbe_max_us.load(std::memory_order_relaxed);
    while (elapsed_us > previous_max && !p25_imbe_max_us.compare_exchange_weak(
               previous_max, elapsed_us, std::memory_order_relaxed)) {}
    if (elapsed_us < 18000)
      vTaskDelay(pdMS_TO_TICKS((18000 - elapsed_us + 999) / 1000));
    else
      taskYIELD();
  }
}

void rds_process_mpx_sample(float phase) {
  /* Keep the resonator only for the UI's 57 kHz energy indicator. */
  const float rds_bp0 = phase + kRdsBpTwoRCos * rtl_audio.rds_bp_y1 -
                        kRdsBpR2 * rtl_audio.rds_bp_y2;
  rtl_audio.rds_bp_y2 = rtl_audio.rds_bp_y1;
  rtl_audio.rds_bp_y1 = rds_bp0;
  rtl_audio.rds_env += kRdsEnvK * (fabsf(rds_bp0) - rtl_audio.rds_env);

  const float new_i = rtl_audio.rds_nco_i * rtl_audio.rds_nco_inc_cos -
                      rtl_audio.rds_nco_q * rtl_audio.rds_nco_inc_sin;
  const float new_q = rtl_audio.rds_nco_i * rtl_audio.rds_nco_inc_sin +
                      rtl_audio.rds_nco_q * rtl_audio.rds_nco_inc_cos;
  rtl_audio.rds_nco_i = new_i;
  rtl_audio.rds_nco_q = new_q;
  if (++rtl_audio.rds_nco_recalc_counter >= kRdsNcoRecalcSamples) {
    rtl_audio.rds_nco_recalc_counter = 0;
    const float mag = sqrtf(rtl_audio.rds_nco_i * rtl_audio.rds_nco_i +
                            rtl_audio.rds_nco_q * rtl_audio.rds_nco_q) +
                      1.0e-9f;
    rtl_audio.rds_nco_i /= mag;
    rtl_audio.rds_nco_q /= mag;
    rtl_audio.rds_nco_inc_cos = cosf(kRdsNcoNominal);
    rtl_audio.rds_nco_inc_sin = sinf(kRdsNcoNominal);
  }

  /*
   * Decode from raw MPX, not the narrow all-pole energy resonator above.
   * Differential complex pairing removes the unknown BPSK carrier phase and
   * slow tuner offset, so no fragile Costas loop is required.
   */
  const float mix_i = phase * rtl_audio.rds_nco_i;
  const float mix_q = phase * rtl_audio.rds_nco_q;
  rtl_audio.rds_i_lpf += kRdsSymLpfK * (mix_i - rtl_audio.rds_i_lpf);
  rtl_audio.rds_q_lpf += kRdsSymLpfK * (mix_q - rtl_audio.rds_q_lpf);
  for (RdsTimingTrack& timing : rtl_audio.rds_timing) {
    timing.chip_i_sum += rtl_audio.rds_i_lpf;
    timing.chip_q_sum += rtl_audio.rds_q_lpf;
    timing.chip_phase += kRdsChipInc;
    if (timing.chip_phase < 1.0f) continue;
    timing.chip_phase -= 1.0f;
    const float chip_i = timing.chip_i_sum;
    const float chip_q = timing.chip_q_sum;
    timing.chip_i_sum = 0.0f;
    timing.chip_q_sum = 0.0f;

    if (timing.have_prev_chip) {
      RdsHypothesis& h = timing.hyp[(timing.chip_index - 1u) & 1u];
      const float pair_i = chip_i - timing.prev_chip_i;
      const float pair_q = chip_q - timing.prev_chip_q;
      if (h.have_prev_pair) {
        const bool data_bit = pair_i * h.prev_pair_i + pair_q * h.prev_pair_q < 0.0f;
        rds_hypothesis_feed(h, data_bit);
      }
      h.prev_pair_i = pair_i;
      h.prev_pair_q = pair_q;
      h.have_prev_pair = true;
    }
    timing.prev_chip_i = chip_i;
    timing.prev_chip_q = chip_q;
    timing.have_prev_chip = true;
    ++timing.chip_index;
  }
  ++rtl_audio.rds_diag_chip_count;
}

void rds_publish_state() {
  if (rtl_audio.rds_carrier_present) {
    if (rtl_audio.rds_env < kRdsCarrierOff) rtl_audio.rds_carrier_present = false;
  } else if (rtl_audio.rds_env > kRdsCarrierOn) {
    rtl_audio.rds_carrier_present = true;
  }
  rtl_rds_signal_dbfs.store(20.0f * log10f(rtl_audio.rds_env + 1.0e-6f),
                            std::memory_order_relaxed);
  rtl_rds_carrier_present.store(rtl_audio.rds_carrier_present,
                                std::memory_order_relaxed);

  RdsSelection selection = rds_select();
  if (selection.locked) {
    rtl_audio.rds_had_block_lock = true;
  } else if (rtl_audio.rds_had_block_lock) {
    /* The verified outage capture locks after a clean replay reset but not
     * from accumulated live state. Reset once when all timing hypotheses
     * lose the established lock so search can reacquire from clean state. */
    rtl_rds_reset();
    selection = rds_select();
  }
  const RdsHypothesis& best = *selection.best;
  rtl_rds_block_locked.store(selection.locked, std::memory_order_relaxed);
  rtl_rds_good_blocks.store(best.good_blocks, std::memory_order_relaxed);
  rtl_rds_total_blocks.store(best.total_blocks, std::memory_order_relaxed);

  static uint32_t rds_diag_last_ms = 0;
  const uint32_t now_diag = millis();
  const uint32_t elapsed_ms = now_diag - rds_diag_last_ms;
  if (!kStreamDiagnosticsEnabled || elapsed_ms < 2000) return;
  rds_diag_last_ms = now_diag;
  const float bler = best.total_blocks > 0
                         ? 100.0f * (1.0f - static_cast<float>(best.good_blocks) /
                                                 static_cast<float>(best.total_blocks))
                         : 100.0f;
  Serial.printf(
      "RDS_STAGE2 locked=%d bler=%.1f%% good=%lu total=%lu "
      "hyp0_locked=%d hyp0_streak=%d hyp1_locked=%d hyp1_streak=%d "
      "nco_freq_off=%.6f i_lpf=%.2f q_lpf=%.2f bp_env=%.5f "
      "A=%04x B=%04x C=%04x D=%04x\n",
      selection.locked, static_cast<double>(bler),
      static_cast<unsigned long>(best.good_blocks),
      static_cast<unsigned long>(best.total_blocks), selection.locked,
      selection.parity_streak[0], selection.locked, selection.parity_streak[1],
      0.0,
      static_cast<double>(rtl_audio.rds_i_lpf),
      static_cast<double>(rtl_audio.rds_q_lpf),
      static_cast<double>(rtl_audio.rds_env), best.group_info[0], best.group_info[1],
      best.group_info[2], best.group_info[3]);

  const float effective_chip_rate = kRdsChipInc * kRdsMpxRateHz;
  const float symbols_per_sec =
      rtl_audio.rds_diag_chip_count * 1000.0f / static_cast<float>(elapsed_ms);
  Serial.printf(
      "RDS_TIMING chip_rate=%.2f mu=%.3f symbols_sec=%.1f correction_ppm=%.1f "
      "freq_off=%.9f\n",
      static_cast<double>(effective_chip_rate),
      static_cast<double>(selection.chip_phase),
      static_cast<double>(symbols_per_sec),
      static_cast<double>(kRdsChipRateCorrectionPpm),
      0.0);
  rtl_audio.rds_diag_chip_count = 0;
}

/**
 * FM / NFM polar demod at kRtlSampleRateSps.
 * wbfm=true: broadcast mono path (channel LPF + 75 µs de-emphasis), plus a
 *   stereo pilot/L-R decoder that runs for METERING ONLY — the speaker still
 *   gets the mono (L+R) sum. Wiring true stereo into playRaw is a separate
 *   step gated on the open audio-drop performance gate (PROJECT_STATUS.md);
 *   doubling the speaker's sample rate belongs behind that gate closing,
 *   not bundled into the first stereo decode.
 * wbfm=false: NFM/WX — tighter audio LPF, light de-emphasis only, no stereo.
 * No blanker / heavy post-LPF (those muffled on prior A/B).
 */
void demodulate_fm(const uint8_t* iq, size_t bytes, float audio_scale, bool wbfm) {
  int16_t* audio = rtl_audio_buffers[rtl_audio.buffer];
  size_t audio_count = 0;
  const float iq_lpf_k = rtl_filter_alpha(wbfm ? RtlBand::fm : RtlBand::wx);
  const float audio_lpf_k = wbfm ? kWbfmAudioLpfK : kNfmAudioLpfK;
  const float deemph_k = wbfm ? kWbfmDeemphK : kNfmDeemphK;
  const float inv_audio_decim = 1.0f / static_cast<float>(kFmAudioDecim);

  /* Stereo meter accumulators, local to this IQ chunk (same call granularity
   * as update_signal_level_from_iq — no persistent window/reset bookkeeping). */
  float meter_peak_l = 0.0f;
  float meter_peak_r = 0.0f;
  bool meter_any = false;
  bool capture_mpx = false;
  size_t capture_write = 0;
  if (wbfm && g_rds_capture_active.load(std::memory_order_acquire)) {
    g_rds_capture_writing.store(true, std::memory_order_release);
    if (g_rds_capture_active.load(std::memory_order_acquire)) {
      capture_mpx = true;
      capture_write = g_rds_capture_write.load(std::memory_order_relaxed);
    } else {
      g_rds_capture_writing.store(false, std::memory_order_release);
    }
  }

  for (size_t offset = 0; offset + 1 < bytes; offset += 2) {
    /* Center CU8 and complex channel LPF (pre-demod adjacent-channel relief). */
    const float i_in = static_cast<float>(static_cast<int32_t>(iq[offset]) - 128);
    const float q_in = static_cast<float>(static_cast<int32_t>(iq[offset + 1]) - 128);
    rtl_audio.iq_i_lpf += iq_lpf_k * (i_in - rtl_audio.iq_i_lpf);
    rtl_audio.iq_q_lpf += iq_lpf_k * (q_in - rtl_audio.iq_q_lpf);

    rtl_audio.i_sum += rtl_audio.iq_i_lpf;
    rtl_audio.q_sum += rtl_audio.iq_q_lpf;
    if (++rtl_audio.rf_phase != kFmRfDecim) continue;

    const float i = rtl_audio.i_sum;
    const float q = rtl_audio.q_sum;
    rtl_audio.i_sum = 0;
    rtl_audio.q_sum = 0;
    rtl_audio.rf_phase = 0;
    if (rtl_audio.have_previous) {
      const float phase = fast_phase(rtl_audio.previous_i * q - rtl_audio.previous_q * i,
                                     rtl_audio.previous_i * i + rtl_audio.previous_q * q);
      if (capture_mpx && capture_write < kRdsCaptureSamples) {
        const float scaled = constrain(phase * kRdsMpxToInt16, -32767.0f, 32767.0f);
        g_rds_capture_buf[capture_write++] = static_cast<int16_t>(scaled);
      }

      /*
       * Stereo pilot/L-R recovery, tapped from the full-bandwidth composite
       * BEFORE the ~16 kHz mono LPF below discards everything past 15 kHz.
       * See the kPilotTwoRCos / kSubTwoRCos comment block for the derivation.
       */
      float diff = 0.0f;
      if (wbfm) {
        /* All-pole resonator at 19 kHz: output is the newest y[n] itself. */
        const float pilot_y0 =
            phase + kPilotTwoRCos * rtl_audio.pilot_y1 - kPilotR2 * rtl_audio.pilot_y2;
        rtl_audio.pilot_y2 = rtl_audio.pilot_y1;
        rtl_audio.pilot_y1 = pilot_y0;

        /* Slow rectified envelope for lock hysteresis (~30 ms time constant). */
        rtl_audio.pilot_env += kStereoEnvK * (fabsf(pilot_y0) - rtl_audio.pilot_env);
        if (rtl_audio.stereo_locked) {
          if (rtl_audio.pilot_env < kStereoLockOff) rtl_audio.stereo_locked = false;
        } else {
          if (rtl_audio.pilot_env > kStereoLockOn) rtl_audio.stereo_locked = true;
        }

        /*
         * Regenerate the 38 kHz demod carrier by squaring the pilot:
         *   cos²θ = ½ + ½cos(2θ)
         * The resonator at 38 kHz rejects the ½ DC term and passes the
         * cos(2θ) term — a clean sinusoid at exactly 2× the pilot frequency,
         * phase-locked to it with no separate PLL loop.
         */
        const float pilot_sq = pilot_y0 * pilot_y0;
        const float sub_y0 =
            pilot_sq + kSubTwoRCos * rtl_audio.sub_y1 - kSubR2 * rtl_audio.sub_y2;
        rtl_audio.sub_y2 = rtl_audio.sub_y1;
        rtl_audio.sub_y1 = sub_y0;

        /*
         * Normalize the regenerated carrier to ~unit amplitude before using it
         * to demodulate: the squarer's AC term has amplitude ½·pilot_env², so
         * dividing by that keeps L-R gain from drifting with signal strength
         * relative to the L+R (mono) path, which is unit-scaled by definition
         * of the discriminator itself.
         */
        const float carrier_amp = 0.5f * rtl_audio.pilot_env * rtl_audio.pilot_env + 1.0e-6f;
        const float carrier = sub_y0 / carrier_amp;

        /*
         * Coherent (synchronous) demod of the 38 kHz DSB-SC L-R subcarrier:
         * multiplying by the in-phase carrier folds it to baseband at half
         * amplitude (cos·cos = ½cos(Δ) + ½cos(Σ)); the ×2 undoes that loss.
         * The Σ image lands at 76 kHz and the mono/pilot/RDS content lands
         * elsewhere in the spectrum — both are rejected by the same ~16 kHz
         * LPF that already shapes the mono path below.
         */
        diff = rtl_audio.stereo_locked ? 2.0f * phase * carrier : 0.0f;

        rds_process_mpx_sample(phase);
      }

      /* Post-discriminator mono audio LPF, then boxcar to 48 kHz. */
      rtl_audio.channel_filter += audio_lpf_k * (phase - rtl_audio.channel_filter);
      rtl_audio.audio_sum += rtl_audio.channel_filter;
      if (wbfm) {
        rtl_audio.channel_filter_diff += audio_lpf_k * (diff - rtl_audio.channel_filter_diff);
        rtl_audio.audio_sum_diff += rtl_audio.channel_filter_diff;
      }
      if (++rtl_audio.audio_phase == kFmAudioDecim) {
        const float demod_sum = rtl_audio.audio_sum * inv_audio_decim;
        const float demod_diff = rtl_audio.audio_sum_diff * inv_audio_decim;
        rtl_audio.audio_sum = 0;
        rtl_audio.audio_sum_diff = 0;
        rtl_audio.audio_phase = 0;

        /* Mono (L+R) path — this is what actually reaches the speaker. */
        rtl_audio.deemphasis += deemph_k * (demod_sum - rtl_audio.deemphasis);
        rtl_audio.dc += 0.0008f * (rtl_audio.deemphasis - rtl_audio.dc);
        const int16_t sample =
            shape_audio_sample(rtl_audio.deemphasis - rtl_audio.dc, audio_scale);
        audio[audio_count++] = sample;
        const int16_t magnitude = sample < 0 ? -sample : sample;
        if (magnitude > rtl_audio.peak) rtl_audio.peak = magnitude;
        rtl_audio.square_sum += static_cast<uint32_t>(sample * sample);
        ++rtl_audio.samples;

        /*
         * L/R for the dashboard meters only — never touches audio[]/playRaw.
         * Reuses agc_gain read-only so the meter sits in the same numeric
         * scale shape_audio_sample uses for the mono readout, without
         * mutating the AGC state that call owns.
         */
        if (wbfm) {
          const float scale = audio_scale * rtl_audio.agc_gain;
          float l, r;
          if (rtl_audio.stereo_locked) {
            rtl_audio.deemphasis_l += deemph_k * ((demod_sum + demod_diff) - rtl_audio.deemphasis_l);
            rtl_audio.deemphasis_r += deemph_k * ((demod_sum - demod_diff) - rtl_audio.deemphasis_r);
            rtl_audio.dc_l += 0.0008f * (rtl_audio.deemphasis_l - rtl_audio.dc_l);
            rtl_audio.dc_r += 0.0008f * (rtl_audio.deemphasis_r - rtl_audio.dc_r);
            l = (rtl_audio.deemphasis_l - rtl_audio.dc_l) * scale;
            r = (rtl_audio.deemphasis_r - rtl_audio.dc_r) * scale;
          } else {
            /* Not locked: mirror mono so L=R, matching the "MONO" UI state. */
            l = r = (rtl_audio.deemphasis - rtl_audio.dc) * scale;
          }
          const float al = fabsf(fm_soft_limit(l)), ar = fabsf(fm_soft_limit(r));
          if (al > meter_peak_l) meter_peak_l = al;
          if (ar > meter_peak_r) meter_peak_r = ar;
          meter_any = true;
        }
      }
    }
    rtl_audio.previous_i = i;
    rtl_audio.previous_q = q;
    rtl_audio.have_previous = true;
  }
  if (capture_mpx) {
    g_rds_capture_write.store(capture_write, std::memory_order_release);
    if (capture_write >= kRdsCaptureSamples) {
      g_rds_capture_active.store(false, std::memory_order_release);
      g_rds_capture_ready.store(true, std::memory_order_release);
      Serial.printf("RTL_RDS_CAPTURE_FULL samples=%u seconds=%u note=send_stop_to_export\n",
                    static_cast<unsigned>(capture_write),
                    static_cast<unsigned>(kRdsCaptureSeconds));
    }
    g_rds_capture_writing.store(false, std::memory_order_release);
  }
  if (wbfm) {
    /* dBFS relative to int16 full scale; matches the mono peak's own units. */
    constexpr float kFullScaleInv = 1.0f / 32768.0f;
    rtl_stereo_locked.store(rtl_audio.stereo_locked, std::memory_order_relaxed);
    if (meter_any) {
      const float l_dbfs = 20.0f * log10f(meter_peak_l * kFullScaleInv + 1.0e-6f);
      const float r_dbfs = 20.0f * log10f(meter_peak_r * kFullScaleInv + 1.0e-6f);
      rtl_audio_left_dbfs.store(l_dbfs, std::memory_order_relaxed);
      rtl_audio_right_dbfs.store(r_dbfs, std::memory_order_relaxed);
    }
    rds_publish_state();
  }
  queue_audio_samples(audio, audio_count);
}

void demodulate_am(const uint8_t* iq, size_t bytes, float audio_scale) {
  int16_t* audio = rtl_audio_buffers[rtl_audio.buffer];
  size_t audio_count = 0;
  const float iq_lpf_k = rtl_filter_alpha(RtlBand::am);
  for (size_t offset = 0; offset + 1 < bytes; offset += 2) {
    const float i_in = static_cast<float>(static_cast<int32_t>(iq[offset]) - 128);
    const float q_in = static_cast<float>(static_cast<int32_t>(iq[offset + 1]) - 128);
    rtl_audio.iq_i_lpf += iq_lpf_k * (i_in - rtl_audio.iq_i_lpf);
    rtl_audio.iq_q_lpf += iq_lpf_k * (q_in - rtl_audio.iq_q_lpf);
    rtl_audio.i_sum += rtl_audio.iq_i_lpf;
    rtl_audio.q_sum += rtl_audio.iq_q_lpf;
    if (++rtl_audio.rf_phase != 4) continue;

    const float i = rtl_audio.i_sum * 0.25f;
    const float q = rtl_audio.q_sum * 0.25f;
    rtl_audio.i_sum = 0;
    rtl_audio.q_sum = 0;
    rtl_audio.rf_phase = 0;
    const float envelope = sqrtf(i * i + q * q);
    rtl_audio.envelope_filter += 0.35f * (envelope - rtl_audio.envelope_filter);
    rtl_audio.audio_sum += rtl_audio.envelope_filter;
    if (++rtl_audio.audio_phase == 5) {
      const float demodulated = rtl_audio.audio_sum * 0.2f;
      rtl_audio.audio_sum = 0;
      rtl_audio.audio_phase = 0;
      rtl_audio.dc += 0.002f * (demodulated - rtl_audio.dc);
      const int16_t sample =
          shape_audio_sample(demodulated - rtl_audio.dc, audio_scale);
      audio[audio_count++] = sample;
      const int16_t magnitude = sample < 0 ? -sample : sample;
      if (magnitude > rtl_audio.peak) rtl_audio.peak = magnitude;
      rtl_audio.square_sum += static_cast<uint32_t>(sample * sample);
      ++rtl_audio.samples;
    }
  }
  queue_audio_samples(audio, audio_count);
}

void demodulate_ssb(const uint8_t* iq, size_t bytes, float audio_scale, CbMode mode) {
  int16_t* audio = rtl_audio_buffers[rtl_audio.buffer];
  size_t audio_count = 0;
  const float iq_lpf_k = rtl_filter_alpha(RtlBand::cb);
  constexpr float kPi = 3.14159265358979323846f;
  const float bfo_hz = 1500.0f + cb_clarifier_hz.load(std::memory_order_relaxed);
  const float direction = mode == CbMode::usb ? -1.0f : 1.0f;
  const float step = direction * 2.0f * kPi * bfo_hz / 240000.0f;
  const float step_cos = cosf(step);
  const float step_sin = sinf(step);

  for (size_t offset = 0; offset + 1 < bytes; offset += 2) {
    const float i_in = static_cast<float>(static_cast<int32_t>(iq[offset]) - 128);
    const float q_in = static_cast<float>(static_cast<int32_t>(iq[offset + 1]) - 128);
    rtl_audio.iq_i_lpf += iq_lpf_k * (i_in - rtl_audio.iq_i_lpf);
    rtl_audio.iq_q_lpf += iq_lpf_k * (q_in - rtl_audio.iq_q_lpf);
    rtl_audio.i_sum += rtl_audio.iq_i_lpf;
    rtl_audio.q_sum += rtl_audio.iq_q_lpf;
    if (++rtl_audio.rf_phase != 4) continue;

    const float i = rtl_audio.i_sum * 0.25f;
    const float q = rtl_audio.q_sum * 0.25f;
    rtl_audio.i_sum = 0;
    rtl_audio.q_sum = 0;
    rtl_audio.rf_phase = 0;
    rtl_audio.audio_sum += i * rtl_audio.ssb_cos - q * rtl_audio.ssb_sin;
    const float next_cos = rtl_audio.ssb_cos * step_cos - rtl_audio.ssb_sin * step_sin;
    rtl_audio.ssb_sin = rtl_audio.ssb_sin * step_cos + rtl_audio.ssb_cos * step_sin;
    rtl_audio.ssb_cos = next_cos;
    if (++rtl_audio.audio_phase != 5) continue;

    const float demodulated = rtl_audio.audio_sum * 0.2f;
    rtl_audio.audio_sum = 0;
    rtl_audio.audio_phase = 0;
    rtl_audio.dc += 0.002f * (demodulated - rtl_audio.dc);
    const int16_t sample = shape_audio_sample(demodulated - rtl_audio.dc, audio_scale);
    audio[audio_count++] = sample;
    const int16_t magnitude = sample < 0 ? -sample : sample;
    if (magnitude > rtl_audio.peak) rtl_audio.peak = magnitude;
    rtl_audio.square_sum += static_cast<uint32_t>(sample * sample);
    ++rtl_audio.samples;
  }
  const float norm = sqrtf(rtl_audio.ssb_cos * rtl_audio.ssb_cos +
                           rtl_audio.ssb_sin * rtl_audio.ssb_sin);
  if (norm > 0.5f) {
    rtl_audio.ssb_cos /= norm;
    rtl_audio.ssb_sin /= norm;
  }
  queue_audio_samples(audio, audio_count);
}

bool cb_audio_gate_open() {
  const int threshold = cb_squelch_dbfs.load(std::memory_order_relaxed);
  if (threshold <= -90) {
    cb_squelch_open.store(true, std::memory_order_relaxed);
    return true;
  }
  const float signal = rtl_signal_dbfs.load(std::memory_order_relaxed);
  bool open = cb_squelch_open.load(std::memory_order_relaxed);
  open = open ? signal >= threshold - 3 : signal >= threshold;
  cb_squelch_open.store(open, std::memory_order_relaxed);
  return open;
}

#if RTL_USE_LEGACY_USB
void run_rtl_capture() {
  const RtlBand band = rtl_requested_band.load(std::memory_order_acquire);
  const bool continuous = rtl_continuous_requested.load(std::memory_order_acquire);
  uint32_t frequency_hz =
      rtl_clamp_frequency(band, rtl_requested_frequency_hz.load(std::memory_order_acquire));
  const uint8_t volume = rtl_requested_volume.load(std::memory_order_acquire);
  rtl_live_volume.store(volume, std::memory_order_release);
  rtl_ui_band = band;
  rtl_ui_frequency_hz = frequency_hz;
  rtl_ui_volume = volume;
  // Base scale is modest; shape_audio_sample AGC + soft limiter set loudness.
  const float audio_scale = (band == RtlBand::wx || band == RtlBand::browse)
                                ? 12000.0f
                                : (band == RtlBand::am || band == RtlBand::cb) ? 9000.0f : 5500.0f;
  rtl_capture_state.store(RtlCaptureState::running, std::memory_order_release);
  rtl_ui_active.store(true, std::memory_order_release);
  set_rtl_sdr_status(continuous ? "RTL-SDR V4: continuous listening"
                                : "RTL-SDR V4: bounded capture running");
  draw_sdr_screen(band, frequency_hz, volume);
  rtl_capture_bytes = 0;
  rtl_capture_min = 0;
  rtl_capture_max = 0;
  rtl_capture_mean = 0;
  rtl_capture_sha256[0] = '\0';
  strlcpy(rtl_capture_error, "none", sizeof(rtl_capture_error));
  rtl_audio = {};

  const usb_config_desc_t* config = nullptr;
  esp_err_t result = usb_host_get_active_config_descriptor(rtl_sdr_device, &config);
  if (result != ESP_OK || config == nullptr || config->bConfigurationValue != 1) {
    strlcpy(rtl_capture_error, "USB configuration 1 is not active",
            sizeof(rtl_capture_error));
    rtl_capture_state.store(RtlCaptureState::failed, std::memory_order_release);
    set_rtl_sdr_status("RTL-SDR V4: configuration error");
    return;
  }
  result = usb_host_interface_claim(usb_client, rtl_sdr_device, 0, 0);
  if (result != ESP_OK) {
    snprintf(rtl_capture_error, sizeof(rtl_capture_error), "interface claim: %s",
             esp_err_to_name(result));
    rtl_capture_state.store(RtlCaptureState::failed, std::memory_order_release);
    set_rtl_sdr_status("RTL-SDR V4: interface claim failed");
    return;
  }

  const RtlControlRecord standard_probe{0x0100, 0x0000, 0x80, 18, {}};
  const bool standard_in_ok = run_control_record(standard_probe, USB_B_REQUEST_GET_DESCRIPTOR);
  const bool initialized = standard_in_ok && run_rtl_initialization();
  const bool rate_set = initialized && set_rtl_sample_rate_960k();
  const bool tuned = rate_set && run_rtl_tune(frequency_hz);
  M5.Speaker.stop();
  const bool sound_on = rtl_audio_enabled.load(std::memory_order_acquire);
  if (sound_on) delay(20);
  const bool speaker_ok = !sound_on || ensure_speaker_running(volume);
  Serial.printf("RTL_EP0_CONTROL_PROBE standard_in=%s captured_init=%s "
                "sample_rate=%s band=%s frequency_hz=%u volume=%u tuned=%s "
                "speaker=%s speaker_running=%s\n",
                standard_in_ok ? "ok" : "failed", initialized ? "ok" : "failed",
                rate_set ? "ok" : "failed", rtl_band_name(band), frequency_hz, volume,
                tuned ? "ok" : "failed", speaker_ok ? "ok" : "failed",
                M5.Speaker.isRunning() ? "true" : "false");
  bool stream_ok = tuned && speaker_ok;
  if (tuned && !speaker_ok) {
    strlcpy(rtl_capture_error, "speaker unavailable", sizeof(rtl_capture_error));
  }
  usb_transfer_t* bulk = nullptr;
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  uint64_t sum = 0;
  uint64_t submitted_bytes = 0;
  uint8_t minimum = UINT8_MAX;
  uint8_t maximum = 0;
  size_t requested = 0;
  bool bulk_in_flight = false;
  const uint32_t stream_started = millis();

  if (stream_ok && (usb_host_transfer_alloc(kRtlBulkBytes, 0, &bulk) != ESP_OK ||
                    (!continuous && mbedtls_sha256_starts(&sha, 0) != 0))) {
    strlcpy(rtl_capture_error, "stream setup failed", sizeof(rtl_capture_error));
    stream_ok = false;
  }

  auto submit_bulk = [&](size_t bytes) {
    requested = bytes;
    bulk->num_bytes = bytes;
    bulk->device_handle = rtl_sdr_device;
    bulk->bEndpointAddress = 0x81;
    bulk->callback = usb_transfer_complete;
    bulk->context = nullptr;
    usb_transfer_done.store(false, std::memory_order_release);
    const esp_err_t submitted = usb_host_transfer_submit(bulk);
    if (submitted != ESP_OK) {
      snprintf(rtl_capture_error, sizeof(rtl_capture_error), "bulk submit: %s",
               esp_err_to_name(submitted));
      return false;
    }
    bulk_in_flight = true;
    submitted_bytes += bytes;
    return true;
  };

  if (stream_ok) {
    stream_ok = submit_bulk(
        continuous ? kRtlBulkBytes
                   : min(kRtlBulkBytes, static_cast<size_t>(kRtlCaptureBytes)));
  }
  while (stream_ok && !rtl_sdr_gone && bulk_in_flight) {
    const uint32_t elapsed = millis() - stream_started;
    if (!continuous && elapsed >= kRtlCaptureTimeoutMs) {
      strlcpy(rtl_capture_error, "capture timeout", sizeof(rtl_capture_error));
      stream_ok = false;
      break;
    }
    const bool completed = wait_for_usb_transfer(
        bulk, continuous ? kRtlControlTimeoutMs : kRtlCaptureTimeoutMs - elapsed);
    bulk_in_flight = false;
    if (!completed || bulk->actual_num_bytes != static_cast<int>(requested)) {
      snprintf(rtl_capture_error, sizeof(rtl_capture_error),
               "bulk failed status=%d actual=%d expected=%u",
               static_cast<int>(bulk->status), bulk->actual_num_bytes,
               static_cast<unsigned>(requested));
      stream_ok = false;
      break;
    }
    const size_t completed_bytes = requested;
    memcpy(rtl_iq_processing, bulk->data_buffer, completed_bytes);

    // Continuous listen: skip full-buffer min/max/sum (32 KB/bulk of pure CPU)
    // so demod gets the budget. Bounded capture still needs stats + SHA.
    if (!continuous) {
      for (size_t index = 0; index < completed_bytes; ++index) {
        const uint8_t value = rtl_iq_processing[index];
        minimum = min(minimum, value);
        maximum = max(maximum, value);
        sum += value;
      }
    } else {
      for (size_t index = 0; index < completed_bytes; index += 64) {
        const uint8_t value = rtl_iq_processing[index];
        minimum = min(minimum, value);
        maximum = max(maximum, value);
        sum += value;
      }
    }
    if (band == RtlBand::lora) lora_iq_offer(rtl_iq_processing, completed_bytes);
    // Audio first.
    if (band == RtlBand::cb) {
      if (cb_audio_gate_open()) {
        const CbMode mode = cb_mode.load(std::memory_order_relaxed);
        if (mode == CbMode::am) demodulate_am(rtl_iq_processing, completed_bytes, audio_scale);
        else demodulate_ssb(rtl_iq_processing, completed_bytes, audio_scale, mode);
      } else {
        rtl_audio_play_count = 0;
      }
    } else if (band == RtlBand::am) {
      demodulate_am(rtl_iq_processing, completed_bytes, audio_scale);
    } else if (band != RtlBand::lora) {
      demodulate_fm(rtl_iq_processing, completed_bytes, audio_scale,
                    band == RtlBand::fm);
    }

    // CRITICAL: never issue EP0 PLL writes while a bulk URB is outstanding.
    // 0.8.30 hot-retuned after re-submit and crashed the USB host stack.
    // Retune only in this gap (bulk complete, next not yet submitted).
    const uint32_t hot = rtl_hot_retune_hz.exchange(0, std::memory_order_acq_rel);
    if (hot != 0 && continuous) {
      const uint32_t next = rtl_clamp_frequency(band, hot);
      if (next != frequency_hz) {
        if (run_rtl_tune(next)) {
          frequency_hz = next;
          rtl_ui_frequency_hz = next;
          rtl_requested_frequency_hz.store(next, std::memory_order_release);
          rtl_audio_reset_demod_filters();
          Serial.printf("RTL_HOT_TUNE frequency_hz=%u\n", frequency_hz);
          bump_rtl_ui();
        } else {
          // Leave stream running on the previous frequency; do not assert/reboot.
          Serial.printf("RTL_HOT_TUNE_FAIL keep_hz=%u tried_hz=%u\n", frequency_hz,
                        next);
          rtl_ui_frequency_hz = frequency_hz;
          rtl_requested_frequency_hz.store(frequency_hz, std::memory_order_release);
        }
      }
    }

    const bool should_queue = continuous
        ? !rtl_stop_requested.load(std::memory_order_acquire)
        : submitted_bytes < kRtlCaptureBytes;
    if (should_queue) {
      const size_t next_bytes = continuous
          ? kRtlBulkBytes
          : min(kRtlBulkBytes,
                static_cast<size_t>(kRtlCaptureBytes - submitted_bytes));
      if (!submit_bulk(next_bytes)) {
        stream_ok = false;
        break;
      }
    }

    // Touch after bulk is re-armed so drag only queues the next retune.
    poll_sdr_touch_from_stream();
    const uint32_t ui_revision = rtl_ui_revision.load(std::memory_order_acquire);
    if (ui_revision != drawn_rtl_ui_revision) {
      const uint8_t live_volume = rtl_live_volume.load(std::memory_order_acquire);
      rtl_ui_volume = live_volume;
      drawn_rtl_ui_revision = ui_revision;
      if (band == RtlBand::p25)
        orcsdr::p25::update(p25_dashboard_snapshot());
      else
        draw_sdr_header(band, frequency_hz, live_volume);
    }
    const uint32_t drops_before_draw = rtl_audio.dropped_chunks;
    if (drops_before_draw == 0 &&
        (millis() - stream_started) >= kRtlAudioPrimeMs) {
      draw_spectrum(rtl_iq_processing, completed_bytes);
    }
    if (!continuous &&
        mbedtls_sha256_update(&sha, rtl_iq_processing, completed_bytes) != 0) {
      strlcpy(rtl_capture_error, "SHA-256 update failed", sizeof(rtl_capture_error));
      stream_ok = false;
      break;
    }
    rtl_capture_bytes += completed_bytes;
    if (continuous && rtl_stop_requested.load(std::memory_order_acquire)) break;
  }
  if (rtl_sdr_gone && stream_ok) {
    strlcpy(rtl_capture_error, "RTL-SDR disconnected", sizeof(rtl_capture_error));
    stream_ok = false;
  }

  const uint32_t stream_elapsed_ms = millis() - stream_started;
  uint8_t digest[32];
  const bool digest_ok = continuous || mbedtls_sha256_finish(&sha, digest) == 0;
  if (stream_ok && rtl_capture_bytes > 0 && digest_ok) {
    static constexpr char kHex[] = "0123456789abcdef";
    if (continuous) {
      strlcpy(rtl_capture_sha256, "not_recorded", sizeof(rtl_capture_sha256));
    } else {
      for (size_t index = 0; index < sizeof(digest); ++index) {
        rtl_capture_sha256[index * 2] = kHex[digest[index] >> 4];
        rtl_capture_sha256[index * 2 + 1] = kHex[digest[index] & 0x0f];
      }
      rtl_capture_sha256[64] = '\0';
    }
    rtl_capture_min = minimum;
    rtl_capture_max = maximum;
    rtl_capture_mean = static_cast<double>(sum) / rtl_capture_bytes;
  } else {
    stream_ok = false;
    if (strcmp(rtl_capture_error, "none") == 0) {
      strlcpy(rtl_capture_error, rtl_capture_bytes == 0 ? "empty capture"
                                                        : "SHA-256 finish failed",
              sizeof(rtl_capture_error));
    }
  }
  mbedtls_sha256_free(&sha);
  if (bulk != nullptr && bulk_in_flight) {
    wait_for_usb_transfer(bulk, kRtlControlTimeoutMs);
    bulk_in_flight = false;
  }
  if (bulk != nullptr && usb_transfer_done.load(std::memory_order_acquire)) {
    usb_host_transfer_free(bulk);
  }

  if (initialized && !rtl_sdr_gone) {
    char stream_error[sizeof(rtl_capture_error)];
    if (!stream_ok) strlcpy(stream_error, rtl_capture_error, sizeof(stream_error));
    const bool cleanup_ok = run_control_records(kRtlCleanupTransfers, !stream_ok);
    if (!stream_ok) {
      strlcpy(rtl_capture_error, stream_error, sizeof(rtl_capture_error));
    } else if (!cleanup_ok) {
      stream_ok = false;
    }
  }
  if (usb_host_interface_release(usb_client, rtl_sdr_device, 0) != ESP_OK && stream_ok) {
    strlcpy(rtl_capture_error, "interface release failed", sizeof(rtl_capture_error));
    stream_ok = false;
  }

  const bool requested_capture_complete =
      continuous ? rtl_stop_requested.load(std::memory_order_acquire)
                 : rtl_capture_bytes == kRtlCaptureBytes;
  if (stream_ok && requested_capture_complete && minimum != maximum &&
      rtl_audio.queued_chunks > 0) {
    rtl_capture_state.store(RtlCaptureState::complete, std::memory_order_release);
    set_rtl_sdr_status("RTL-SDR V4: capture complete");
    draw_sdr_controls(band, false);
    const double audio_rms = sqrt(rtl_audio.square_sum / rtl_audio.samples);
    const double effective_sps = stream_elapsed_ms == 0
        ? 0
        : static_cast<double>(rtl_capture_bytes) * 500.0 / stream_elapsed_ms;
    Serial.printf("RTL_CAPTURE_OK band=\"%s\" frequency_hz=%u volume=%u bytes=%llu "
                  "stream_ms=%u effective_sps=%.0f min=%u max=%u mean=%.3f sha256=%s "
                  "audio_samples=%llu audio_peak=%d audio_rms=%.1f audio_chunks=%u dropped=%u\n",
                  rtl_band_name(band), frequency_hz, rtl_ui_volume,
                  static_cast<unsigned long long>(rtl_capture_bytes),
                  stream_elapsed_ms, effective_sps, rtl_capture_min, rtl_capture_max,
                  rtl_capture_mean, rtl_capture_sha256,
                  static_cast<unsigned long long>(rtl_audio.samples), rtl_audio.peak,
                  audio_rms, rtl_audio.queued_chunks, rtl_audio.dropped_chunks);
  } else {
    if (stream_ok) {
      strlcpy(rtl_capture_error, minimum == maximum ? "constant stream" : "speaker queue failed",
              sizeof(rtl_capture_error));
    }
    rtl_capture_state.store(RtlCaptureState::failed, std::memory_order_release);
    set_rtl_sdr_status("RTL-SDR V4: capture failed");
    draw_sdr_controls(band, false);
    Serial.printf("RTL_CAPTURE_ERROR bytes=%llu reason=\"%s\"\n",
                  static_cast<unsigned long long>(rtl_capture_bytes), rtl_capture_error);
  }
  // Persist drag-settled LO after stream teardown (NVS off the bulk path).
  if (band == RtlBand::fm) {
    persist_fm_frequency(frequency_hz);
  }
}

void inspect_usb_device(uint8_t address) {
  usb_device_handle_t device = nullptr;
  if (usb_host_device_open(usb_client, address, &device) != ESP_OK) return;

  const usb_device_desc_t* descriptor = nullptr;
  usb_device_info_t info{};
  if (usb_host_get_device_descriptor(device, &descriptor) != ESP_OK ||
      usb_host_device_info(device, &info) != ESP_OK) {
    usb_host_device_close(usb_client, device);
    return;
  }

  char manufacturer[48]{};
  char product[48]{};
  char serial[48]{};
  usb_string_to_ascii(info.str_desc_manufacturer, manufacturer, sizeof(manufacturer));
  usb_string_to_ascii(info.str_desc_product, product, sizeof(product));
  usb_string_to_ascii(info.str_desc_serial_num, serial, sizeof(serial));
  const char* speed = info.speed == USB_SPEED_HIGH ? "high" :
                      info.speed == USB_SPEED_FULL ? "full" : "low";
  Serial.printf("RTL_SDR_USB vid=%04x pid=%04x speed=%s manufacturer=\"%s\" product=\"%s\" serial=\"%s\"\n",
                descriptor->idVendor, descriptor->idProduct, speed, manufacturer,
                product, serial);

  if (descriptor->idVendor == 0x0bda && descriptor->idProduct == 0x2838 &&
      strcmp(manufacturer, "RTLSDRBlog") == 0 && strcmp(product, "Blog V4") == 0 &&
      strcmp(serial, "00000001") == 0) {
    rtl_sdr_device = device;
    rtl_sdr_vid = descriptor->idVendor;
    rtl_sdr_pid = descriptor->idProduct;
    strlcpy(rtl_sdr_speed, speed, sizeof(rtl_sdr_speed));
    strlcpy(rtl_sdr_serial, serial, sizeof(rtl_sdr_serial));
    rtl_capture_state.store(RtlCaptureState::ready, std::memory_order_release);
    char status[96];
    snprintf(status, sizeof(status), "RTL-SDR V4 ready: %s USB, serial %s", speed, serial);
    set_rtl_sdr_status(status);
    Serial.printf("RTL_SDR_PROBE_OK v4=true bands=fm,am,wx default_fm_hz=%u "
                  "sample_rate_sps=%u validation_bytes=%u volume_default=%u continuous_touch=true\n",
                  kRtlFmDefaultHz, kRtlSampleRateSps, kRtlCaptureBytes, kRtlVolumeDefault);
  } else {
    if (descriptor->idVendor == 0x0bda && descriptor->idProduct == 0x2838) {
      Serial.println("RTL_SDR_REJECTED reason=official_v4_identity_mismatch");
    }
    usb_host_device_close(usb_client, device);
  }
}

void clear_rtl_sdr_device() {
  if (rtl_sdr_device != nullptr) {
    usb_host_device_close(usb_client, rtl_sdr_device);
    rtl_sdr_device = nullptr;
  }
  rtl_sdr_vid = 0;
  rtl_sdr_pid = 0;
  strlcpy(rtl_sdr_speed, "none", sizeof(rtl_sdr_speed));
  rtl_sdr_serial[0] = '\0';
  rtl_capture_state.store(RtlCaptureState::disconnected, std::memory_order_release);
  set_rtl_sdr_status("RTL-SDR: disconnected");
  Serial.println("RTL_SDR_DISCONNECTED");
}

void usb_host_task(void*) {
  usb_host_config_t host_config{};
  host_config.intr_flags = ESP_INTR_FLAG_LEVEL1;
  host_config.peripheral_map = 0;  // ESP32-P4 default is the High-Speed controller.
  const esp_err_t install_result = usb_host_install(&host_config);
  if (install_result != ESP_OK) {
    Serial.printf("RTL_SDR_HOST_ERROR install=%s\n", esp_err_to_name(install_result));
    set_rtl_sdr_status("RTL-SDR: USB host install failed");
    vTaskDelete(nullptr);
  }

  usb_host_client_config_t client_config{};
  client_config.is_synchronous = false;
  client_config.max_num_event_msg = 4;
  client_config.async.client_event_callback = usb_client_event;
  const esp_err_t client_result = usb_host_client_register(&client_config, &usb_client);
  if (client_result != ESP_OK) {
    Serial.printf("RTL_SDR_HOST_ERROR client=%s\n", esp_err_to_name(client_result));
    set_rtl_sdr_status("RTL-SDR: USB client failed");
    vTaskDelete(nullptr);
  }
  set_rtl_sdr_status("RTL-SDR: USB-A host active, waiting");
  Serial.println("RTL_SDR_HOST_READY controller=high_speed");

  while (true) {
    uint32_t event_flags = 0;
    usb_host_lib_handle_events(pdMS_TO_TICKS(10), &event_flags);
    usb_host_client_handle_events(usb_client, pdMS_TO_TICKS(10));
    if (pending_usb_address != 0) {
      const uint8_t address = pending_usb_address;
      pending_usb_address = 0;
      inspect_usb_device(address);
    }
    if (rtl_sdr_gone) {
      rtl_sdr_gone = false;
      clear_rtl_sdr_device();
    }
    if (rtl_sdr_device != nullptr && !rtl_sdr_gone &&
        rtl_capture_requested.exchange(false, std::memory_order_acq_rel)) {
      run_rtl_capture();
      if (rtl_restart_requested.exchange(false, std::memory_order_acq_rel) &&
          rtl_sdr_device != nullptr && !rtl_sdr_gone) {
        rtl_stop_requested.store(false, std::memory_order_release);
        rtl_capture_requested.store(true, std::memory_order_release);
      }
    }
  }
}

void initialize_rtl_sdr_host() {
  M5.Power.setExtOutput(false, m5::ext_USB);
  delay(100);
  M5.Power.setExtOutput(true, m5::ext_USB);
  set_rtl_sdr_status("RTL-SDR: USB-A power enabled");
  if (xTaskCreate(usb_host_task, "rtl_usb_host", 8192, nullptr, 4, nullptr) != pdPASS) {
    set_rtl_sdr_status("RTL-SDR: host task failed");
  }
}
#else  /* !RTL_USE_LEGACY_USB — Gate 2 component path */

/*
 * Core split (working path):
 *   Core 0 — driver USB (host + client)
 *   Core 1 — driver delivery posts EVT_IQ_BLOCK → demod+speaker here;
 *            rtl_app does touch / retune / spectrum (lower rate)
 * The extra audio-job queue broke both audio and graphics; keep demod inline.
 */
static void on_rtl_driver_event(rtl_sdr_v4_esp_event_t event, const void *payload, void *ctx) {
  (void)ctx;
  switch (event) {
    case RTL_SDR_V4_ESP_EVT_READY:
    case RTL_SDR_V4_ESP_EVT_ENUMERATED: {
      g_rtl_device_ready.store(true, std::memory_order_release);
      rtl_capture_state.store(RtlCaptureState::ready, std::memory_order_release);
      const auto *info = static_cast<const rtl_sdr_v4_esp_device_info_t *>(payload);
      if (info != nullptr) {
        rtl_sdr_vid = info->vid;
        rtl_sdr_pid = info->pid;
        strlcpy(rtl_sdr_serial, info->serial, sizeof(rtl_sdr_serial));
        strlcpy(rtl_sdr_speed, info->high_speed ? "high" : "full", sizeof(rtl_sdr_speed));
      }
      set_rtl_sdr_status("RTL-SDR V4 ready (driver)");
      Serial.printf("RTL_SDR_PROBE_OK v4=true driver=rtl_sdr_v4_esp v%s\n",
                    rtl_sdr_v4_esp_get_version_string());
      break;
    }
    case RTL_SDR_V4_ESP_EVT_DISCONNECTED:
      g_rtl_device_ready.store(false, std::memory_order_release);
      rtl_capture_state.store(RtlCaptureState::disconnected, std::memory_order_release);
      set_rtl_sdr_status("RTL-SDR: disconnected");
      Serial.println("RTL_SDR_DISCONNECTED");
      break;
    case RTL_SDR_V4_ESP_EVT_IQ_BLOCK: {
      const uint32_t dsp_started_us = micros();
      const auto *iq = static_cast<const rtl_sdr_v4_esp_iq_block_t *>(payload);
      if (iq == nullptr || iq->data == nullptr || iq->bytes == 0) break;
      const size_t n =
          iq->bytes <= sizeof(rtl_iq_processing) ? iq->bytes : sizeof(rtl_iq_processing);
      if (g_stream_band == RtlBand::adsb && adsb_iq_free && adsb_iq_ready) {
        uint8_t index = 0;
        if (xQueueReceive(adsb_iq_free, &index, 0) == pdTRUE) {
          std::memcpy(adsb_iq_blocks[index], iq->data, n);
          adsb_iq_sizes[index] = n;
          (void)xQueueSend(adsb_iq_ready, &index, 0);
        } else {
          adsb_iq_drops.fetch_add(1, std::memory_order_relaxed);
        }
      }
      update_signal_level_from_iq(iq->data, n);
      if (g_stream_band == RtlBand::p25) orcsdr::p25decoder::process_cu8(iq->data, n);
      /* ADS-B needs the full callback budget; it has no spectrum or audio path. */
      if (g_stream_band != RtlBand::adsb || orcsdr::home::active())
        spectrum_offer_iq_snapshot(iq->data, n);
      if (g_stream_band == RtlBand::lora) lora_iq_offer(iq->data, n);
      if (g_stream_band != RtlBand::lora && g_stream_band != RtlBand::p25 &&
          (rtl_audio_enabled.load(std::memory_order_relaxed) ||
           g_audio_rec_active.load(std::memory_order_relaxed)) &&
          !rtl_audio_test_tone.load(std::memory_order_relaxed)) {
        if (g_stream_band == RtlBand::cb) {
          if (cb_audio_gate_open()) {
            const CbMode mode = cb_mode.load(std::memory_order_relaxed);
            if (mode == CbMode::am) demodulate_am(iq->data, n, g_stream_audio_scale);
            else demodulate_ssb(iq->data, n, g_stream_audio_scale, mode);
          } else {
            rtl_audio_play_count = 0;
          }
        } else if (g_stream_band == RtlBand::am) {
          demodulate_am(iq->data, n, g_stream_audio_scale);
        } else {
          demodulate_fm(iq->data, n, g_stream_audio_scale,
                        g_stream_band == RtlBand::fm);
        }
      }
      rtl_capture_bytes += n;
      (void)rtl_sdr_v4_esp_release_iq_block(g_rtl, iq);
      const uint32_t dsp_elapsed_us = micros() - dsp_started_us;
      rtl_dsp_window_us.fetch_add(dsp_elapsed_us, std::memory_order_relaxed);
      rtl_dsp_window_blocks.fetch_add(1, std::memory_order_relaxed);
      uint32_t previous_max = rtl_dsp_block_us_max.load(std::memory_order_relaxed);
      while (dsp_elapsed_us > previous_max &&
             !rtl_dsp_block_us_max.compare_exchange_weak(
                 previous_max, dsp_elapsed_us, std::memory_order_relaxed)) {
      }
      break;
    }
    case RTL_SDR_V4_ESP_EVT_STOPPED:
      break;
    case RTL_SDR_V4_ESP_EVT_RETUNED: {
      if (g_stream_band == RtlBand::p25) orcsdr::p25decoder::reset();
      const auto *hz = static_cast<const uint32_t *>(payload);
      if (hz != nullptr) {
        rtl_ui_frequency_hz = *hz;
        rtl_requested_frequency_hz.store(*hz, std::memory_order_release);
      }
      break;
    }
    case RTL_SDR_V4_ESP_EVT_ERROR: {
      const auto *err = static_cast<const rtl_sdr_v4_esp_error_info_t *>(payload);
      Serial.printf("RTL_DRIVER_ERROR %s\n",
                    err ? rtl_sdr_v4_esp_err_to_name(err->code) : "?");
      break;
    }
    default:
      break;
  }
}

static void rtl_driver_app_task(void *) {
  while (true) {
    if (g_sd_transfer_active.load(std::memory_order_acquire)) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    if (g_rtl != nullptr && g_rtl_device_ready.load(std::memory_order_acquire) &&
        rtl_capture_requested.exchange(false, std::memory_order_acq_rel)) {
      const RtlBand band = rtl_requested_band.load(std::memory_order_acquire);
      const uint32_t frequency_hz = rtl_clamp_frequency(
          band, rtl_requested_frequency_hz.load(std::memory_order_acquire));
      const uint8_t volume = rtl_requested_volume.load(std::memory_order_acquire);
      g_stream_band = band;
      g_stream_audio_scale = (band == RtlBand::wx || band == RtlBand::browse)
                                 ? 12000.0f
                                 : (band == RtlBand::am || band == RtlBand::cb) ? 9000.0f : 5500.0f;
      rtl_live_volume.store(volume, std::memory_order_release);
      rtl_ui_band = band;
      rtl_ui_frequency_hz = frequency_hz;
      rtl_ui_volume = volume;
      rtl_session_started_ms = millis();
      rtl_capture_bytes = 0;
      rtl_audio = {};
      rtl_dsp_window_us.store(0, std::memory_order_relaxed);
      rtl_dsp_window_blocks.store(0, std::memory_order_relaxed);
      rtl_dsp_block_us_max.store(0, std::memory_order_relaxed);
      rtl_audio_play_count = 0;
      rtl_audio_play_block_index = 0;
      memset(rtl_audio_play_block_ready_ms, 0, sizeof(rtl_audio_play_block_ready_ms));
      rtl_audio_ring_overruns.store(0, std::memory_order_relaxed);
      rtl_audio_submit_failures.store(0, std::memory_order_relaxed);
      rtl_signal_dbfs.store(-90.0f, std::memory_order_relaxed);
      if (band == RtlBand::p25) {
        orcsdr::p25decoder::reset();
        p25_voice_session.fetch_add(1, std::memory_order_acq_rel);
        p25_imbe_frames.store(0, std::memory_order_relaxed);
        p25_imbe_errors.store(0, std::memory_order_relaxed);
        p25_pcm_frames.store(0, std::memory_order_relaxed);
        p25_audio_last_ms.store(0, std::memory_order_relaxed);
        p25_voice_stack_hwm.store(0, std::memory_order_relaxed);
        p25_imbe_max_us.store(0, std::memory_order_relaxed);
        p25_imbe_synth_max_us.store(0, std::memory_order_relaxed);
        p25_audio_queue_max_us.store(0, std::memory_order_relaxed);
      }
      rtl_signal_dbfs_smooth = -80.0f;
      rtl_signal_meter_last_ms = 0;
      rtl_audio_left_dbfs.store(-90.0f, std::memory_order_relaxed);
      rtl_audio_right_dbfs.store(-90.0f, std::memory_order_relaxed);
      rtl_stereo_locked.store(false, std::memory_order_relaxed);
      rtl_rds_signal_dbfs.store(-90.0f, std::memory_order_relaxed);
      rtl_rds_carrier_present.store(false, std::memory_order_relaxed);
      rtl_rds_block_locked.store(false, std::memory_order_relaxed);
      rtl_rds_good_blocks.store(0, std::memory_order_relaxed);
      rtl_rds_total_blocks.store(0, std::memory_order_relaxed);
      if (band == RtlBand::lora) lora_iq_reset_detector();
      if (band == RtlBand::adsb) {
        adsb_decoder.reset();
        adsb_iq_drops.store(0, std::memory_order_relaxed);
        reset_adsb_tracks();
      }
      rtl_ui_active.store(true, std::memory_order_release);
      if (!orcsdr::home::active() && band != RtlBand::adsb)
        draw_sdr_screen(band, frequency_hz, volume);
      M5.Speaker.stop();

      rtl_sdr_v4_esp_stream_config_t st;
      rtl_sdr_v4_esp_stream_config_default(&st);
      st.preset = RTL_SDR_V4_ESP_PRESET_CUSTOM_HZ;
      st.frequency_hz = frequency_hz;
      st.sample_rate_sps = band == RtlBand::adsb ? RTL_SDR_V4_ESP_RATE_2048K
                                                  : RTL_SDR_V4_ESP_RATE_960K;
      esp_err_t err = rtl_sdr_v4_esp_start(g_rtl, &st);
      Serial.printf("RTL_START %s rate=%u frequency_hz=%u\n",
                    rtl_sdr_v4_esp_err_to_name(err), st.sample_rate_sps, frequency_hz);
      begin_power_monitor("rtl_start");
      if (err == ESP_ERR_NO_MEM) {
        vTaskDelay(pdMS_TO_TICKS(500));
        err = rtl_sdr_v4_esp_start(g_rtl, &st);
        Serial.printf("RTL_START_RETRY %s rate=%u frequency_hz=%u\n",
                      rtl_sdr_v4_esp_err_to_name(err), st.sample_rate_sps, frequency_hz);
      }
      if (err == ESP_OK && band == RtlBand::fm) {
        Serial.printf("RTL_WBFM_DSP rate=%u filter_hz=%u iq_lpf_k=%.2f audio_lpf_k=%.2f "
                      "decim=%u/%u note=app_side_filter\n",
                      kRtlSampleRateSps,
                      rtl_filter_bandwidth_hz.load(std::memory_order_relaxed),
                      static_cast<double>(rtl_filter_alpha(RtlBand::fm)),
                      static_cast<double>(kWbfmAudioLpfK),
                      static_cast<unsigned>(kFmRfDecim),
                      static_cast<unsigned>(kFmAudioDecim));
      }
      if (err != ESP_OK) {
        rtl_capture_state.store(RtlCaptureState::failed, std::memory_order_release);
        set_rtl_sdr_status("RTL-SDR V4: start failed");
        draw_sdr_controls(band, false);
        /* Stay on radio UI so power/home chrome cannot paint over controls. */
      } else {
        rtl_capture_state.store(RtlCaptureState::running, std::memory_order_release);
        sync_rtl_audio_for_band(band);
        if (band == RtlBand::p25 &&
            !p25_survey_active.load(std::memory_order_relaxed)) {
          p25_entry_probe_tsbk_good = orcsdr::p25decoder::snapshot().tsbk_good;
          p25_entry_probe_at_ms = millis() + 2500;
          Serial.printf("RTL_P25_PROBE start control_hz=%lu dwell_ms=2500\n",
                        static_cast<unsigned long>(p25_control_frequency_hz));
        }
        set_rtl_sdr_status("RTL-SDR V4: continuous listening (driver)");
        allow_boot_speaker();
        uint32_t spectrum_last_ms = 0;
        uint32_t adsb_metrics_last_ms = 0;
        uint32_t last_lo_applied_hz = frequency_hz;
        uint32_t last_lo_apply_ms = 0;
        bool auto_fm_scanning = false;
        uint32_t auto_fm_frequency_hz = kRtlFmMinHz + kRtlFmAutoStepHz / 2;
        uint32_t auto_fm_sample_at_ms = 0;
        uint32_t auto_fm_best_hz = frequency_hz;
        float auto_fm_best_level = -120.0f;
        bool preset_scanning = false;
        uint32_t preset_scan_frequency_hz = kRtlFmMinHz + kRtlFmAutoStepHz / 2;
        uint32_t preset_scan_sample_at_ms = 0;
        uint32_t preset_scan_return_hz = frequency_hz;
        int preset_scan_step_index = 0;
        constexpr float kFmPresetMinDbfs = -70.0f;
        while (!rtl_stop_requested.load(std::memory_order_acquire) &&
               g_rtl_device_ready.load(std::memory_order_acquire)) {
          /* Touch + hot retune on this task (not in IQ callback): responsive STOP/FREQ. */
          poll_sdr_touch_from_stream();
          if (orcsdr::settings::active()) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
          }

          /*
           * Coalesce LO applies: only retune when the 5 kHz target changed and
           * at most every kRtlHotRetuneMinIntervalMs. Rapid 1 kHz UI updates
           * must not each drain USB (that caused chop + reverb-like smear).
           */
          const uint32_t now_retune = millis();
          if (g_stream_band == RtlBand::fm && !auto_fm_scanning &&
              rtl_auto_fm_requested.exchange(false, std::memory_order_acq_rel)) {
            auto_fm_scanning = true;
            rtl_auto_fm_active.store(true, std::memory_order_release);
            auto_fm_frequency_hz = kRtlFmMinHz + kRtlFmAutoStepHz / 2;
            auto_fm_best_hz = frequency_hz;
            auto_fm_best_level = -120.0f;
            rtl_scope_span_hz.store(kRtlScopeSpanMaxHz, std::memory_order_relaxed);
            redraw_spectrum_panel();
            draw_spectrum_axis();
            request_hot_retune(auto_fm_frequency_hz);
            auto_fm_sample_at_ms = now_retune + kRtlFmAutoSettleMs;
            orcsdr::fm::update(fm_dashboard_snapshot());
            Serial.println("RTL_AUTO_FM start");
          }
          if (auto_fm_scanning && now_retune >= auto_fm_sample_at_ms) {
            const float level = rtl_scope_peak_level.load(std::memory_order_relaxed);
            const int32_t offset = rtl_scope_peak_offset_hz.load(std::memory_order_relaxed);
            const int64_t found = static_cast<int64_t>(auto_fm_frequency_hz) + offset;
            const uint32_t found_hz = rtl_clamp_frequency(
                RtlBand::fm, found > 0 ? static_cast<uint32_t>(found) : 0u);
            if (level > auto_fm_best_level) {
              auto_fm_best_level = level;
              auto_fm_best_hz = ((found_hz + 50000u) / 100000u) * 100000u;
            }
            Serial.printf("RTL_AUTO_FM sample center=%u peak=%u level=%.1f\n",
                          auto_fm_frequency_hz, found_hz, static_cast<double>(level));
            if (auto_fm_frequency_hz + kRtlFmAutoStepHz / 2 >= kRtlFmMaxHz) {
              auto_fm_scanning = false;
              rtl_auto_fm_active.store(false, std::memory_order_release);
              request_hot_retune(auto_fm_best_hz);
              persist_fm_frequency(auto_fm_best_hz);
              orcsdr::fm::update(fm_dashboard_snapshot());
              Serial.printf("RTL_AUTO_FM done frequency_hz=%u level=%.1f\n",
                            auto_fm_best_hz, static_cast<double>(auto_fm_best_level));
            } else {
              auto_fm_frequency_hz += kRtlFmAutoStepHz;
              reset_spectrum_renderer();
              request_hot_retune(auto_fm_frequency_hz);
              auto_fm_sample_at_ms = now_retune + kRtlFmAutoSettleMs;
            }
          }
          if (g_stream_band == RtlBand::fm && !preset_scanning && !auto_fm_scanning &&
              rtl_fm_preset_scan_requested.exchange(false, std::memory_order_acq_rel)) {
            preset_scanning = true;
            rtl_fm_preset_scan_active.store(true, std::memory_order_release);
            rtl_fm_preset_scan_cancel.store(false, std::memory_order_relaxed);
            preset_scan_frequency_hz = kRtlFmMinHz + kRtlFmAutoStepHz / 2;
            preset_scan_return_hz = rtl_ui_frequency_hz;
            preset_scan_step_index = 0;
            fm_preset_count = 0;
            fm_preset_scroll_top = 0;
            const int total_steps = static_cast<int>(
                (kRtlFmMaxHz - kRtlFmMinHz + kRtlFmAutoStepHz - 1) / kRtlFmAutoStepHz);
            rtl_fm_preset_scan_total_steps.store(total_steps, std::memory_order_relaxed);
            rtl_fm_preset_scan_step.store(0, std::memory_order_relaxed);
            rtl_fm_preset_scan_found.store(0, std::memory_order_relaxed);
            rtl_scope_span_hz.store(kRtlScopeSpanMaxHz, std::memory_order_relaxed);
            redraw_spectrum_panel();
            draw_spectrum_axis();
            request_hot_retune(preset_scan_frequency_hz);
            rtl_fm_preset_scan_freq_hz.store(preset_scan_frequency_hz, std::memory_order_relaxed);
            preset_scan_sample_at_ms = now_retune + kRtlFmAutoSettleMs;
            draw_fm_dashboard(false);
            Serial.println("RTL_PRESET_SCAN start");
          }
          if (preset_scanning && now_retune >= preset_scan_sample_at_ms) {
            const float level = rtl_scope_peak_level.load(std::memory_order_relaxed);
            const int32_t offset = rtl_scope_peak_offset_hz.load(std::memory_order_relaxed);
            const int64_t found = static_cast<int64_t>(preset_scan_frequency_hz) + offset;
            const uint32_t found_hz = rtl_clamp_frequency(
                RtlBand::fm, found > 0 ? static_cast<uint32_t>(found) : 0u);
            const uint32_t snapped_hz = ((found_hz + 50000u) / 100000u) * 100000u;
            if (level >= kFmPresetMinDbfs) {
              fm_preset_offer(snapped_hz, level);
              rtl_fm_preset_scan_found.store(fm_preset_count, std::memory_order_relaxed);
            }
            if (kStreamDiagnosticsEnabled) {
              Serial.printf("RTL_PRESET_SCAN sample center=%u peak=%u level=%.1f\n",
                            preset_scan_frequency_hz, found_hz, static_cast<double>(level));
            }
            ++preset_scan_step_index;
            rtl_fm_preset_scan_step.store(preset_scan_step_index, std::memory_order_relaxed);
            if (preset_scan_frequency_hz + kRtlFmAutoStepHz / 2 >= kRtlFmMaxHz ||
                rtl_fm_preset_scan_cancel.exchange(false, std::memory_order_acq_rel)) {
              preset_scanning = false;
              rtl_fm_preset_scan_active.store(false, std::memory_order_release);
              request_hot_retune(preset_scan_return_hz);
              draw_fm_dashboard(true);
              persist_fm_presets();
              Serial.printf("RTL_PRESET_SCAN done found=%d\n", fm_preset_count);
            } else {
              preset_scan_frequency_hz += kRtlFmAutoStepHz;
              rtl_fm_preset_scan_freq_hz.store(preset_scan_frequency_hz, std::memory_order_relaxed);
              reset_spectrum_renderer();
              request_hot_retune(preset_scan_frequency_hz);
              preset_scan_sample_at_ms = now_retune + kRtlFmAutoSettleMs;
            }
          }
          uint32_t desired_lo = rtl_hot_retune_hz.load(std::memory_order_acquire);
          if (desired_lo != 0 && g_rtl != nullptr &&
              desired_lo != last_lo_applied_hz &&
              (now_retune - last_lo_apply_ms) >= kRtlHotRetuneMinIntervalMs) {
            const uint32_t next = rtl_clamp_frequency(g_stream_band, desired_lo);
            (void)rtl_hot_retune_hz.compare_exchange_strong(
                desired_lo, 0, std::memory_order_acq_rel);
            esp_err_t te = rtl_sdr_v4_esp_retune_hz(g_rtl, next);
            last_lo_apply_ms = now_retune;
            if (te == ESP_OK) {
              last_lo_applied_hz = next;
              /* Light demod re-sync only — do not flush speaker DMA (reverb/gap). */
              rtl_audio_reset_demod_filters();
              Serial.printf("RTL_HOT_TUNE hz=%u ok\n", next);
            } else {
              Serial.printf("RTL_HOT_TUNE hz=%u -> %s\n", next,
                            rtl_sdr_v4_esp_err_to_name(te));
            }
          }

          const uint32_t ui_revision = rtl_ui_revision.load(std::memory_order_acquire);
          if (g_stream_band != RtlBand::adsb && ui_revision != drawn_rtl_ui_revision) {
            drawn_rtl_ui_revision = ui_revision;
            if (orcsdr::home::active())
              orcsdr::home::update(home_dashboard_snapshot());
            else if (g_stream_band == RtlBand::fm)
              orcsdr::fm::update(fm_dashboard_snapshot());
            else if (g_stream_band == RtlBand::p25)
              orcsdr::p25::update(p25_dashboard_snapshot());
            else
              draw_sdr_header(g_stream_band, rtl_ui_frequency_hz,
                              rtl_live_volume.load(std::memory_order_acquire));
          }

          const uint32_t now = millis();
          service_p25_survey(now);
          service_p25_follow(now);
          if (g_stream_band == RtlBand::adsb && now - adsb_metrics_last_ms >= 5000) {
            adsb_metrics_last_ms = now;
            expire_adsb_tracks(now);
            rtl_sdr_v4_esp_metrics_t metrics{};
            if (rtl_sdr_v4_esp_get_metrics(g_rtl, &metrics) == ESP_OK) {
              const auto& decode = adsb_decoder.stats();
              Serial.printf("RTL_ADSB_STATUS uptime_ms=%u effective_sps=%u bytes=%llu "
                            "blocks=%u short=%u overruns=%u drops=%u signal_dbfs=%.1f "
                            "sample_min=%u sample_max=%u sample_mean=%.1f iq_queue_drops=%u "
                            "mag_min=%u mag_max=%u preambles=%u frames=%u df17=%u crc_ok=%u "
                            "aircraft=%u messages=%u\n",
                            metrics.uptime_ms, metrics.effective_sps,
                            static_cast<unsigned long long>(metrics.bytes_total),
                            metrics.blocks_total, metrics.short_transfers, metrics.overruns,
                            metrics.consumer_drops,
                            static_cast<double>(rtl_signal_dbfs.load(std::memory_order_relaxed)),
                            metrics.sample_min, metrics.sample_max,
                            static_cast<double>(metrics.sample_mean),
                            adsb_iq_drops.load(std::memory_order_relaxed),
                            decode.magnitude_min, decode.magnitude_max, decode.preambles,
                            decode.frames, decode.df17, decode.crc_ok,
                            adsb_aircraft_count.load(std::memory_order_relaxed),
                            adsb_total_messages.load(std::memory_order_relaxed));
            }
          }
          /* SIG meter stays on even with GFX off (antenna peaking). */
          if (orcsdr::home::active() &&
              now - rtl_signal_meter_last_ms >= kRtlSignalMeterIntervalMs) {
            rtl_signal_meter_last_ms = now;
            orcsdr::home::update(home_dashboard_snapshot());
          } else if (g_stream_band == RtlBand::fm &&
              now - rtl_signal_meter_last_ms >= kRtlSignalMeterIntervalMs) {
            rtl_signal_meter_last_ms = now;
            draw_fm_dashboard(false);
          } else if (g_stream_band == RtlBand::p25 &&
                     now - rtl_signal_meter_last_ms >= kRtlSignalMeterIntervalMs) {
            rtl_signal_meter_last_ms = now;
            draw_p25_dashboard(false);
          } else if (g_stream_band != RtlBand::adsb &&
                     g_stream_band != RtlBand::fm && g_stream_band != RtlBand::p25 &&
              now - rtl_signal_meter_last_ms >= kRtlSignalMeterIntervalMs) {
            rtl_signal_meter_last_ms = now;
            draw_signal_meter(false);
            /* Capture tool owns the screen below the header while active —
             * dashboard repaints here would fight its debug panel for the
             * same pixels (this was the "leftover screens" glitch). */
            if (orc_tool_current() != OrcTool::Capture) {
              draw_cb_dashboard(false);
              draw_lora_dashboard(false);
              draw_fm_dashboard(false);
            }
          }
          /* Auto-export WAV after buffer fills (never write SD on the IQ callback). */
          if (g_audio_rec_export_pending.exchange(false, std::memory_order_acq_rel)) {
            (void)audio_rec_stop_and_export();
            if (orc_tool_current() == OrcTool::Capture) draw_capture_tool_panel();
            if (g_stream_band == RtlBand::fm)
              orcsdr::fm::update(fm_dashboard_snapshot());
            else
              draw_sdr_controls(g_stream_band, true);
          }
          if (g_iq_rec_export_pending.exchange(false, std::memory_order_acq_rel)) {
            (void)iq_rec_stop_and_export();
            draw_lora_dashboard(false);
          }
          const bool gfx_on = rtl_graphics_enabled.load(std::memory_order_acquire);
          if ((g_stream_band != RtlBand::adsb || orcsdr::home::active()) && gfx_on &&
              orc_tool_current() != OrcTool::Capture) {
            const bool sound_on = rtl_audio_enabled.load(std::memory_order_relaxed);
            const bool audio_stressed =
                sound_on && rtl_audio.dropped_chunks > 0 &&
                rtl_audio.dropped_chunks * 2u > rtl_audio.queued_chunks + 2u;
            const uint32_t visual_interval = audio_stressed
                                                 ? kRtlSpectrumStressedIntervalMs
                                                 : kRtlSpectrumIntervalMs;
            if (now - rtl_session_started_ms >= kRtlAudioPrimeMs &&
                now - spectrum_last_ms >= visual_interval) {
              spectrum_last_ms = now;
              draw_spectrum(nullptr, 0); /* uses frozen IQ snapshot */
            }
          } else if (orc_tool_current() == OrcTool::Capture &&
                     (now - spectrum_last_ms) >= 500) {
            spectrum_last_ms = now;
            draw_capture_tool_panel();
          }

          vTaskDelay(pdMS_TO_TICKS(20));
        }
        Serial.println("RTL_STOP_REQUESTED");
        rtl_auto_fm_active.store(false, std::memory_order_release);
        flush_audio_play_batch(true);
        const esp_err_t stop_err = rtl_sdr_v4_esp_stop(g_rtl, 2000);
        Serial.printf("RTL_STOP_RESULT %s\n", rtl_sdr_v4_esp_err_to_name(stop_err));
        rtl_capture_state.store(stop_err == ESP_OK ? RtlCaptureState::complete
                                                   : RtlCaptureState::failed,
                                std::memory_order_release);
        set_rtl_sdr_status(stop_err == ESP_OK ? "RTL-SDR V4: stopped"
                                              : "RTL-SDR V4: stop failed");
        /* Documentation capture freezes the last live frame while reception stops. */
        if (!ui_documentation_mode && !orcsdr::home::active()) {
          /* Keep rtl_ui_active true so home/power does not paint over SDR controls. */
          if (rtl_ui_band == RtlBand::adsb) orcsdr::adsb::draw();
          else if (rtl_ui_band == RtlBand::fm)
            orcsdr::fm::update(fm_dashboard_snapshot());
          else if (rtl_ui_band == RtlBand::p25)
            orcsdr::p25::update(p25_dashboard_snapshot());
          else
            draw_sdr_controls(g_stream_band, false);
        }
        Serial.printf("RTL_STOP bytes=%llu\n",
                      static_cast<unsigned long long>(rtl_capture_bytes));
      }
      const bool restart = rtl_restart_requested.exchange(false, std::memory_order_acq_rel);
      if (rtl_capture_state.load(std::memory_order_acquire) == RtlCaptureState::complete &&
          restart &&
          g_rtl_device_ready.load(std::memory_order_acquire)) {
        rtl_stop_requested.store(false, std::memory_order_release);
        rtl_capture_requested.store(true, std::memory_order_release);
      }
    } else if (rtl_ui_active.load(std::memory_order_acquire) &&
               rtl_capture_state.load(std::memory_order_acquire) !=
                   RtlCaptureState::running) {
      /* Radio UI idle (stopped): still need touch for START / band / FREQ. */
      poll_sdr_touch_from_stream();
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void initialize_rtl_sdr_host() {
  adsb_iq_free = xQueueCreate(kAdsbIqBlockCount, sizeof(uint8_t));
  adsb_iq_ready = xQueueCreate(kAdsbIqBlockCount, sizeof(uint8_t));
  if (!adsb_iq_free || !adsb_iq_ready) {
    set_rtl_sdr_status("ADS-B: queue allocation failed");
    return;
  }
  for (uint8_t i = 0; i < kAdsbIqBlockCount; ++i) {
    adsb_iq_blocks[i] = static_cast<uint8_t*>(
        heap_caps_malloc(kAdsbIqBlockBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!adsb_iq_blocks[i]) {
      set_rtl_sdr_status("ADS-B: PSRAM allocation failed");
      return;
    }
    (void)xQueueSend(adsb_iq_free, &i, 0);
  }
  if (xTaskCreatePinnedToCore(adsb_decoder_task, "adsb_decode", 6144, nullptr, 4, nullptr, 1) !=
      pdPASS) {
    set_rtl_sdr_status("ADS-B: decoder task failed");
    return;
  }
  if (xTaskCreatePinnedToCore(p25_voice_task, "p25_voice", 8192, nullptr, 6, nullptr, 1) !=
      pdPASS) {
    set_rtl_sdr_status("P25: voice task failed");
    return;
  }

  rtl_sdr_v4_esp_config_t cfg;
  rtl_sdr_v4_esp_config_default(&cfg);
  cfg.event_cb = on_rtl_driver_event;
  /*
   * Fewer, larger URBs → fewer demod wakeups, longer continuous audio runs.
   * 3 × 32 KiB is the measured continuous-listen profile from Tab5.
   */
  cfg.transfer_bytes = 32768;
  cfg.transfer_count = 3;
  // Keep USB ownership deterministic; DSP, UI, and Hosted control stay on core 1.
  cfg.usb_task_core_id = 0;
  esp_err_t err = rtl_sdr_v4_esp_config_validate(&cfg);
  if (err != ESP_OK) {
    set_rtl_sdr_status("RTL-SDR: config invalid");
    return;
  }
  err = rtl_sdr_v4_esp_install(&cfg, &g_rtl);
  if (err != ESP_OK) {
    Serial.printf("RTL_INSTALL %s\n", rtl_sdr_v4_esp_err_to_name(err));
    set_rtl_sdr_status("RTL-SDR: install failed");
    return;
  }
  Serial.printf("RTL_INSTALL ok v%s caps=0x%08x\n", rtl_sdr_v4_esp_get_version_string(),
                static_cast<unsigned>(rtl_sdr_v4_esp_get_capabilities()));
  set_rtl_sdr_status("RTL-SDR: driver host active, waiting");
  // LoRa/dashboard formatting reaches newlib's float formatter on this task.
  // 8 KiB overruns the stack guard during navigation; retain margin for UI draws.
  if (xTaskCreatePinnedToCore(rtl_driver_app_task, "rtl_app", 12288, nullptr, kRtlAppTaskPrio,
                              nullptr, 1) != pdPASS) {
    set_rtl_sdr_status("RTL-SDR: app task failed");
  }
  Serial.println("RTL_CORE_SPLIT usb=core0 dsp_audio_ui_hosted=core1 (inline demod)");
}
#endif /* RTL_USE_LEGACY_USB */

void persist_wifi_profiles() {
  for (uint8_t i = 0; i < std::size(wifi_profiles); ++i) {
    char ssid_key[16], pass_key[16];
    snprintf(ssid_key, sizeof(ssid_key), "wifi%u_ssid", i);
    snprintf(pass_key, sizeof(pass_key), "wifi%u_pass", i);
    if (i < wifi_profile_count) {
      preferences.putString(ssid_key, wifi_profiles[i].ssid);
      preferences.putString(pass_key, wifi_profiles[i].password);
    } else {
      preferences.remove(ssid_key);
      preferences.remove(pass_key);
    }
  }
  wifi_configured = wifi_profile_count > 0;
}

void select_wifi_profile(uint8_t index) {
  if (index >= wifi_profile_count) return;
  strlcpy(wifi_ssid, wifi_profiles[index].ssid, sizeof(wifi_ssid));
  strlcpy(wifi_password, wifi_profiles[index].password, sizeof(wifi_password));
}

void apply_wifi_antenna() {
  M5.getIOExpander(0).digitalWrite(0, settings_wifi_external_antenna);
  Serial.printf("RTL_WIFI_ANTENNA %s\n",
                settings_wifi_external_antenna ? "external_mmcx" : "internal");
}

void initialize_wifi() {
  const uint32_t dma_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  const uint32_t dma_largest =
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  Serial.printf("RTL_WIFI_DMA free=%lu largest=%lu txq=%d rxq=%d\n",
                static_cast<unsigned long>(dma_free),
                static_cast<unsigned long>(dma_largest),
                CONFIG_ESP_HOSTED_SDIO_TX_Q_SIZE, CONFIG_ESP_HOSTED_SDIO_RX_Q_SIZE);
  apply_wifi_antenna();
  WiFi.setPins(kWifiClockPin, kWifiCommandPin, kWifiData0Pin, kWifiData1Pin,
               kWifiData2Pin, kWifiData3Pin, kWifiResetPin);
  wifi_station_ready = WiFi.mode(WIFI_STA);
  uint32_t host_major = 0, host_minor = 0, host_patch = 0;
  uint32_t slave_major = 0, slave_minor = 0, slave_patch = 0;
  if (wifi_station_ready) {
    hostedGetHostVersion(&host_major, &host_minor, &host_patch);
    hostedGetSlaveVersion(&slave_major, &slave_minor, &slave_patch);
    wifi_hosted_versions_match = host_major == slave_major && host_minor == slave_minor &&
                                 host_patch == slave_patch;
    Serial.printf("RTL_WIFI_HOSTED host=%lu.%lu.%lu slave=%lu.%lu.%lu match=%d\n",
                  static_cast<unsigned long>(host_major),
                  static_cast<unsigned long>(host_minor),
                  static_cast<unsigned long>(host_patch),
                  static_cast<unsigned long>(slave_major),
                  static_cast<unsigned long>(slave_minor),
                  static_cast<unsigned long>(slave_patch),
                  wifi_hosted_versions_match ? 1 : 0);
    if (!wifi_hosted_versions_match) {
      WiFi.mode(WIFI_OFF);
      wifi_station_ready = false;
      strlcpy(wifi_status_message, "ESP-Hosted version mismatch",
              sizeof(wifi_status_message));
      Serial.println("RTL_WIFI_BLOCKED hosted_version_mismatch");
    }
  }
  Serial.printf("RTL_WIFI_INIT station=%d core=%d\n", wifi_station_ready ? 1 : 0,
                xPortGetCoreID());
  draw_wifi_state();
}

void log_wifi_coexistence(const char* event, uint32_t elapsed_ms = 0) {
  rtl_sdr_v4_esp_metrics_t metrics{};
  if (g_rtl != nullptr) (void)rtl_sdr_v4_esp_get_metrics(g_rtl, &metrics);
  Serial.printf("RTL_WIFI_COEX event=%s elapsed_ms=%u effective_sps=%u usb_overruns=%u "
                "usb_drops=%u audio_chunks=%u audio_drops=%u dsp_block_us_max=%u "
                "ui_fps=%u\n",
                event, elapsed_ms, metrics.effective_sps, metrics.overruns,
                metrics.consumer_drops, rtl_audio.queued_chunks, rtl_audio.dropped_chunks,
                rtl_dsp_block_us_max.load(std::memory_order_relaxed), rtl_spectrum_fps);
}

void start_wifi_inventory() {
  if (!settings_wifi_power_enabled) return;
  if (!wifi_station_ready) initialize_wifi();
  if (!wifi_station_ready) return;
  if (wifi_scan_running) return;
  WiFi.scanDelete();
  wifi_scan_result_count = 0;
  wifi_scan_started_ms = millis();
  strlcpy(wifi_status_message, "Scanning networks", sizeof(wifi_status_message));
  wifi_network_count = WiFi.scanNetworks(true, true);
  begin_power_monitor("wifi_scan");
  wifi_scan_running = wifi_network_count == WIFI_SCAN_RUNNING;
  log_wifi_coexistence(wifi_scan_running ? "scan_started" : "scan_start_failed");
  draw_wifi_state();
}

void start_wifi_connection() {
  if (!settings_wifi_power_enabled) return;
  if (!wifi_station_ready) initialize_wifi();
  if (!wifi_station_ready || !wifi_ssid[0]) return;
  if (wifi_scan_running) WiFi.scanDelete();
  wifi_scan_running = false;
  wifi_network_count = -1;
  wifi_connected = false;
  wifi_connecting = true;
  wifi_connect_started_ms = millis();
  strlcpy(wifi_status_message, "Testing connection", sizeof(wifi_status_message));
  WiFi.disconnect();
  WiFi.begin(wifi_ssid, wifi_password);
  begin_power_monitor("wifi_connect");
  log_wifi_coexistence("connect_started");
  draw_wifi_state();
}

void stop_wifi() {
  if (wifi_station_ready) {
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);
  }
  wifi_station_ready = false;
  wifi_hosted_versions_match = false;
  wifi_connected = false;
  wifi_connecting = false;
  wifi_scan_running = false;
  strlcpy(wifi_status_message, "Wi-Fi off", sizeof(wifi_status_message));
  Serial.println("RTL_WIFI_OFF");
}

void start_wifi_connection(const char* ssid, const char* password, bool save_on_success) {
  if (!ssid || !ssid[0]) return;
  strlcpy(wifi_ssid, ssid, sizeof(wifi_ssid));
  strlcpy(wifi_password, password ? password : "", sizeof(wifi_password));
  wifi_save_after_connect = save_on_success;
  wifi_connect_requested.store(true, std::memory_order_release);
}

void poll_wifi() {
  if (!settings_wifi_power_enabled) {
    wifi_scan_requested.store(false, std::memory_order_release);
    wifi_connect_requested.store(false, std::memory_order_release);
    return;
  }
  if (wifi_scan_requested.exchange(false, std::memory_order_acq_rel)) start_wifi_inventory();
  if (wifi_connect_requested.exchange(false, std::memory_order_acq_rel)) start_wifi_connection();
  bool state_changed = false;
  if (wifi_scan_running) {
    const int result = WiFi.scanComplete();
    if (result != WIFI_SCAN_RUNNING) {
      wifi_scan_running = false;
      wifi_network_count = result;
      wifi_scan_result_count = result > 0
                                   ? static_cast<uint8_t>(std::min<int>(
                                         result, std::size(wifi_scan_results)))
                                   : 0;
      for (uint8_t i = 0; i < wifi_scan_result_count; ++i) {
        WiFi.SSID(i).toCharArray(wifi_scan_results[i].ssid,
                                sizeof(wifi_scan_results[i].ssid));
        wifi_scan_results[i].rssi = WiFi.RSSI(i);
        wifi_scan_results[i].secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
      }
      WiFi.scanDelete();
      log_wifi_coexistence("scan_complete", millis() - wifi_scan_started_ms);
      allow_boot_speaker();
      state_changed = true;
    }
  }
  const wl_status_t wifi_status = WiFi.status();
  const bool connected = wifi_status == WL_CONNECTED;
  if (wifi_connecting && connected) {
    wifi_connecting = false;
    if (wifi_save_after_connect) {
      int existing = -1;
      for (uint8_t i = 0; i < wifi_profile_count; ++i)
        if (strcmp(wifi_profiles[i].ssid, wifi_ssid) == 0) existing = i;
      if (existing >= 0) {
        strlcpy(wifi_profiles[existing].password, wifi_password,
                sizeof(wifi_profiles[existing].password));
      } else if (wifi_profile_count < std::size(wifi_profiles)) {
        strlcpy(wifi_profiles[wifi_profile_count].ssid, wifi_ssid,
                sizeof(wifi_profiles[wifi_profile_count].ssid));
        strlcpy(wifi_profiles[wifi_profile_count].password, wifi_password,
                sizeof(wifi_profiles[wifi_profile_count].password));
        ++wifi_profile_count;
      }
      persist_wifi_profiles();
    }
    wifi_save_after_connect = false;
    strlcpy(wifi_status_message, "Connection successful", sizeof(wifi_status_message));
    log_wifi_coexistence("connect_complete", millis() - wifi_connect_started_ms);
    allow_boot_speaker();
    state_changed = true;
  } else if (wifi_connecting &&
             (millis() - wifi_connect_started_ms >= 15000u ||
              wifi_status == WL_CONNECT_FAILED || wifi_status == WL_NO_SSID_AVAIL)) {
    const bool discard_candidate = wifi_save_after_connect;
    wifi_connecting = false;
    wifi_save_after_connect = false;
    WiFi.disconnect();
    if (discard_candidate) {
      int saved = -1;
      for (uint8_t i = 0; i < wifi_profile_count; ++i)
        if (strcmp(wifi_profiles[i].ssid, wifi_ssid) == 0) saved = i;
      if (saved >= 0) select_wifi_profile(static_cast<uint8_t>(saved));
      else if (wifi_profile_count) select_wifi_profile(0);
      else {
        wifi_ssid[0] = '\0';
        memset(wifi_password, 0, sizeof(wifi_password));
      }
    }
    strlcpy(wifi_status_message, "Connection failed", sizeof(wifi_status_message));
    log_wifi_coexistence("connect_failed", millis() - wifi_connect_started_ms);
    allow_boot_speaker();
    state_changed = true;
  }
  if (connected != wifi_connected) {
    wifi_connected = connected;
    state_changed = true;
  }
  if (state_changed) {
    draw_wifi_state();
    if (authenticated) emit_identity();
  }
}

orcsdr::fm::Snapshot fm_dashboard_snapshot() {
  orcsdr::fm::Snapshot snapshot;
  snapshot.frequency_hz = rtl_ui_frequency_hz;
  snapshot.step_hz = rtl_fm_step_hz;
  snapshot.filter_bandwidth_hz =
      rtl_filter_bandwidth_hz.load(std::memory_order_relaxed);
  snapshot.span_hz = rtl_scope_span_hz.load(std::memory_order_relaxed);
  snapshot.relative_dbfs = rtl_signal_dbfs_smooth;
  snapshot.left_dbfs = rtl_audio_left_dbfs.load(std::memory_order_relaxed);
  snapshot.right_dbfs = rtl_audio_right_dbfs.load(std::memory_order_relaxed);
  snapshot.running =
      rtl_capture_state.load(std::memory_order_acquire) == RtlCaptureState::running;
  snapshot.driver_ready = rtl_device_ready();
  snapshot.stereo = rtl_stereo_locked.load(std::memory_order_relaxed);
  snapshot.rds_carrier = rtl_rds_carrier_present.load(std::memory_order_relaxed);
  snapshot.rds_locked = rtl_rds_block_locked.load(std::memory_order_relaxed);
  snapshot.wifi_connected = wifi_connected;
  snapshot.sound_enabled = rtl_audio_user_enabled.load(std::memory_order_relaxed);
  snapshot.graphics_enabled = rtl_graphics_enabled.load(std::memory_order_relaxed);
  snapshot.recording = g_audio_rec_active.load(std::memory_order_relaxed);
  snapshot.preset_scanning =
      rtl_fm_preset_scan_active.load(std::memory_order_relaxed);
  snapshot.volume = rtl_ui_volume;
  snapshot.preset_count = static_cast<uint8_t>(fm_preset_count);
  snapshot.battery_percent = M5.Power.getBatteryLevel();
  for (int i = 0; i < fm_preset_count; ++i) {
    const uint32_t delta = rtl_ui_frequency_hz > fm_presets[i].freq_hz
                               ? rtl_ui_frequency_hz - fm_presets[i].freq_hz
                               : fm_presets[i].freq_hz - rtl_ui_frequency_hz;
    if (delta <= 100000u) {
      snapshot.preset_index = static_cast<uint8_t>(i + 1);
      break;
    }
  }
#if !RTL_USE_LEGACY_USB
  rtl_sdr_v4_esp_metrics_t metrics{};
  if (g_rtl != nullptr) {
    (void)rtl_sdr_v4_esp_get_metrics(g_rtl, &metrics);
    snapshot.effective_sps = metrics.effective_sps;
    snapshot.target_sps = metrics.sample_rate_sps ? metrics.sample_rate_sps : kRtlSampleRateSps;
    snapshot.usb_overruns = metrics.overruns;
    snapshot.consumer_drops = metrics.consumer_drops;
    const esp_err_t last_error = rtl_sdr_v4_esp_get_last_error(g_rtl);
    if (last_error != ESP_OK)
      strlcpy(snapshot.last_error, rtl_sdr_v4_esp_err_to_name(last_error),
              sizeof(snapshot.last_error));
  }
#else
  snapshot.target_sps = kRtlSampleRateSps;
#endif
  snapshot.audio_underruns = rtl_audio.dropped_chunks +
      rtl_audio_ring_overruns.load(std::memory_order_relaxed) +
      rtl_audio_submit_failures.load(std::memory_order_relaxed);
  constexpr uint32_t kIqBlockPeriodUs =
      static_cast<uint32_t>((32768ull / 2ull) * 1000000ull / kRtlSampleRateSps);
  snapshot.dsp_percent = std::min<uint32_t>(999,
      rtl_dsp_block_us_max.load(std::memory_order_relaxed) * 100u /
          kIqBlockPeriodUs);
  return snapshot;
}

void handle_fm_dashboard_action(const orcsdr::fm::Action& action) {
  using orcsdr::fm::ActionKind;
  auto tune = [](uint32_t hz) {
    const uint32_t frequency = rtl_clamp_frequency(RtlBand::fm, hz);
    if (rtl_capture_state.load(std::memory_order_acquire) == RtlCaptureState::running)
      request_hot_retune(frequency);
    else
      queue_local_rtl_listen(RtlBand::fm, frequency);
  };
  switch (action.kind) {
    case ActionKind::tune_hz: tune(action.value); break;
    case ActionKind::step_down:
      tune(rtl_step_frequency(RtlBand::fm, rtl_ui_frequency_hz, -1));
      break;
    case ActionKind::step_up:
      tune(rtl_step_frequency(RtlBand::fm, rtl_ui_frequency_hz, 1));
      break;
    case ActionKind::seek_down:
    case ActionKind::seek_up: {
      int selected = -1;
      if (action.kind == ActionKind::seek_down) {
        for (int i = fm_preset_count - 1; i >= 0; --i)
          if (fm_presets[i].freq_hz + 50000u < rtl_ui_frequency_hz) { selected = i; break; }
        if (selected < 0 && fm_preset_count) selected = fm_preset_count - 1;
      } else {
        for (int i = 0; i < fm_preset_count; ++i)
          if (fm_presets[i].freq_hz > rtl_ui_frequency_hz + 50000u) { selected = i; break; }
        if (selected < 0 && fm_preset_count) selected = 0;
      }
      if (selected >= 0) tune(fm_presets[selected].freq_hz);
      else tune(rtl_step_frequency(RtlBand::fm, rtl_ui_frequency_hz,
                                   action.kind == ActionKind::seek_down ? -1 : 1));
      break;
    }
    case ActionKind::save_preset:
      fm_preset_offer(rtl_ui_frequency_hz, rtl_signal_dbfs_smooth);
      persist_fm_presets();
      break;
    case ActionKind::step_cycle: {
      static constexpr uint32_t steps[] = {50000, 100000, 200000, 500000, 1000000};
      size_t i = 0;
      while (i < std::size(steps) && steps[i] != rtl_fm_step_hz) ++i;
      rtl_fm_step_hz = steps[(i + 1) % std::size(steps)];
      break;
    }
    case ActionKind::filter_down:
    case ActionKind::filter_up: {
      const uint32_t current = rtl_filter_bandwidth_hz.load(std::memory_order_relaxed);
      const int64_t wanted = static_cast<int64_t>(current) +
          (action.kind == ActionKind::filter_down ? -20000 : 20000);
      rtl_filter_bandwidth_hz.store(
          rtl_clamp_filter_hz(RtlBand::fm,
                              static_cast<uint32_t>(std::max<int64_t>(wanted, 0))),
          std::memory_order_relaxed);
      rtl_audio_reset_demod_filters();
      reset_spectrum_renderer();
      break;
    }
    case ActionKind::span_down:
    case ActionKind::span_up: {
      const uint32_t current = rtl_scope_span_hz.load(std::memory_order_relaxed);
      const uint32_t next = action.kind == ActionKind::span_down
                                ? std::max(kRtlScopeSpanMinHz, current / 2)
                                : std::min(kRtlScopeSpanMaxHz, current * 2);
      rtl_scope_span_hz.store(next, std::memory_order_relaxed);
      reset_spectrum_renderer();
      break;
    }
    case ActionKind::sound_toggle:
      set_rtl_audio_user_enabled(
          !rtl_audio_user_enabled.load(std::memory_order_acquire));
      break;
    case ActionKind::volume_down: adjust_rtl_volume(-static_cast<int>(kRtlVolumeStep)); break;
    case ActionKind::volume_up: adjust_rtl_volume(static_cast<int>(kRtlVolumeStep)); break;
    case ActionKind::graphics_toggle:
      rtl_graphics_enabled.store(
          !rtl_graphics_enabled.load(std::memory_order_acquire), std::memory_order_release);
      reset_spectrum_renderer();
      break;
    case ActionKind::recording_toggle:
      if (g_audio_rec_active.load(std::memory_order_acquire))
        (void)audio_rec_stop_and_export();
      else
        (void)audio_rec_start();
      break;
    case ActionKind::scan_presets:
      if (rtl_fm_preset_scan_active.load(std::memory_order_relaxed))
        rtl_fm_preset_scan_cancel.store(true, std::memory_order_relaxed);
      else
        rtl_fm_preset_scan_requested.store(true, std::memory_order_relaxed);
      break;
    case ActionKind::open_device_settings:
      open_global_settings(orcsdr::settings::Section::radio_defaults);
      break;
    case ActionKind::exit_to_browse:
      orcsdr::fm::leave();
      show_home();
      break;
    case ActionKind::none: break;
  }
  if (orcsdr::fm::active()) orcsdr::fm::update(fm_dashboard_snapshot());
}

orcsdr::p25::Snapshot p25_dashboard_snapshot() {
  orcsdr::p25::Snapshot snapshot;
  snapshot.frequency_hz = rtl_ui_frequency_hz;
  snapshot.span_hz = rtl_scope_span_hz.load(std::memory_order_relaxed);
  snapshot.relative_dbfs = rtl_signal_dbfs.load(std::memory_order_relaxed);
  snapshot.running =
      rtl_capture_state.load(std::memory_order_acquire) == RtlCaptureState::running;
  snapshot.driver_ready = rtl_device_ready();
  snapshot.wifi_connected = wifi_connected;
  snapshot.sound_enabled = rtl_audio_user_enabled.load(std::memory_order_relaxed);
  snapshot.volume = rtl_ui_volume;
  snapshot.survey_active = p25_survey_active.load(std::memory_order_relaxed);
  snapshot.hold = p25_hold.load(std::memory_order_relaxed);
  snapshot.hold_talkgroup = p25_hold_talkgroup;
  snapshot.auto_follow = p25_auto_follow.load(std::memory_order_relaxed);
  snapshot.encryption_skip = p25_encryption_skip.load(std::memory_order_relaxed);
  snapshot.following_voice =
      p25_follow_state.load(std::memory_order_acquire) == P25FollowState::voice;
  snapshot.imbe_frames = p25_imbe_frames.load(std::memory_order_relaxed);
  snapshot.imbe_errors = p25_imbe_errors.load(std::memory_order_relaxed);
  snapshot.candidate_index = p25_candidate_index;
  snapshot.candidate_count = p25_config.control_channel_count;
  std::copy_n(std::begin(p25_candidate_levels), p25_config.control_channel_count,
              std::begin(snapshot.candidate_levels));
  snapshot.config = p25_config;
  snapshot.config_revision = p25_config_revision;
  strlcpy(snapshot.config_status, p25_config_status, sizeof(snapshot.config_status));
  snapshot.decoded = orcsdr::p25decoder::snapshot();
  if (p25_follow_state.load(std::memory_order_acquire) == P25FollowState::voice) {
    snapshot.decoded.current_grant = p25_follow_grant;
    snapshot.decoded.recent_grants[0] = p25_follow_grant;
  }
  snapshot.battery_percent = M5.Power.getBatteryLevel();
#if !RTL_USE_LEGACY_USB
  rtl_sdr_v4_esp_metrics_t metrics{};
  if (g_rtl != nullptr) {
    (void)rtl_sdr_v4_esp_get_metrics(g_rtl, &metrics);
    snapshot.effective_sps = metrics.effective_sps;
    snapshot.target_sps = metrics.sample_rate_sps ? metrics.sample_rate_sps : kRtlSampleRateSps;
    snapshot.usb_overruns = metrics.overruns;
    snapshot.consumer_drops = metrics.consumer_drops;
    const esp_err_t last_error = rtl_sdr_v4_esp_get_last_error(g_rtl);
    if (last_error != ESP_OK)
      strlcpy(snapshot.last_error, rtl_sdr_v4_esp_err_to_name(last_error),
              sizeof(snapshot.last_error));
  }
#else
  snapshot.target_sps = kRtlSampleRateSps;
#endif
  snapshot.audio_underruns = rtl_audio.dropped_chunks +
      rtl_audio_ring_overruns.load(std::memory_order_relaxed) +
      rtl_audio_submit_failures.load(std::memory_order_relaxed);
  constexpr uint32_t kIqBlockPeriodUs =
      static_cast<uint32_t>((32768ull / 2ull) * 1000000ull / kRtlSampleRateSps);
  snapshot.dsp_percent = std::min<uint32_t>(999,
      rtl_dsp_block_us_max.load(std::memory_order_relaxed) * 100u / kIqBlockPeriodUs);
  return snapshot;
}

void tune_p25_control(uint32_t frequency_hz) {
  frequency_hz = rtl_clamp_frequency(RtlBand::p25, frequency_hz);
  p25_follow_state.store(P25FollowState::control, std::memory_order_release);
  p25_voice_session.fetch_add(1, std::memory_order_acq_rel);
  p25_control_frequency_hz = frequency_hz;
  p25_voice_frequency_hz = 0;
  if (rtl_capture_state.load(std::memory_order_acquire) == RtlCaptureState::running)
    request_hot_retune(frequency_hz);
  else
    queue_local_rtl_listen(RtlBand::p25, frequency_hz);
}

void start_p25_survey() {
  std::fill(std::begin(p25_candidate_levels), std::end(p25_candidate_levels), -120.0f);
  std::fill(std::begin(p25_candidate_tsbk_good), std::end(p25_candidate_tsbk_good), 0);
  p25_candidate_index = 0;
  p25_entry_probe_at_ms = 0;
  p25_survey_sample_at_ms = millis() + 1500;
  p25_survey_active.store(true, std::memory_order_relaxed);
  tune_p25_control(p25_config.control_channels_hz[0]);
  Serial.printf("RTL_P25_SURVEY start candidates=%u dwell_ms=1500\n",
                static_cast<unsigned>(p25_config.control_channel_count));
}

void handle_p25_dashboard_action(const orcsdr::p25::Action& action) {
  using orcsdr::p25::ActionKind;
  switch (action.kind) {
    case ActionKind::tune_hz:
      p25_survey_active.store(false, std::memory_order_relaxed);
      tune_p25_control(action.value);
      break;
    case ActionKind::previous_candidate:
      p25_survey_active.store(false, std::memory_order_relaxed);
      p25_candidate_index = static_cast<uint8_t>(
          (p25_candidate_index + p25_config.control_channel_count - 1) %
          p25_config.control_channel_count);
      tune_p25_control(p25_config.control_channels_hz[p25_candidate_index]);
      request_p25_config_save();
      break;
    case ActionKind::next_candidate:
      p25_survey_active.store(false, std::memory_order_relaxed);
      p25_candidate_index = static_cast<uint8_t>(
          (p25_candidate_index + 1) % p25_config.control_channel_count);
      tune_p25_control(p25_config.control_channels_hz[p25_candidate_index]);
      request_p25_config_save();
      break;
    case ActionKind::survey_toggle:
      if (p25_survey_active.load(std::memory_order_relaxed)) {
        p25_survey_active.store(false, std::memory_order_relaxed);
        Serial.println("RTL_P25_SURVEY stop");
      } else {
        start_p25_survey();
      }
      break;
    case ActionKind::hold_toggle:
      p25_hold.store(!p25_hold.load(std::memory_order_relaxed), std::memory_order_relaxed);
      if (!p25_hold.load(std::memory_order_relaxed)) p25_hold_talkgroup = 0;
      request_p25_config_save();
      break;
    case ActionKind::hold_talkgroup:
      if (p25_hold.load(std::memory_order_relaxed) && p25_hold_talkgroup == action.value) {
        p25_hold.store(false, std::memory_order_relaxed);
        p25_hold_talkgroup = 0;
      } else {
        p25_hold_talkgroup = static_cast<uint16_t>(action.value);
        p25_hold.store(true, std::memory_order_relaxed);
      }
      Serial.printf("RTL_P25_HOLD enabled=%d tg=%u\n",
                    p25_hold.load(std::memory_order_relaxed) ? 1 : 0,
                    p25_hold_talkgroup);
      request_p25_config_save();
      break;
    case ActionKind::skip_talkgroup: {
      const auto decoded = orcsdr::p25decoder::snapshot();
      const uint16_t talkgroup =
          p25_follow_state.load(std::memory_order_acquire) == P25FollowState::voice
              ? p25_follow_grant.talkgroup : decoded.current_grant.talkgroup;
      if (talkgroup != 0) {
        p25_skipped_talkgroup = talkgroup;
        p25_skip_until_ms = millis() + 30000;
        Serial.printf("RTL_P25_SKIP tg=%u duration_ms=30000\n", talkgroup);
      }
      if (p25_follow_state.load(std::memory_order_acquire) == P25FollowState::voice) {
        p25_follow_state.store(P25FollowState::control, std::memory_order_release);
        p25_voice_session.fetch_add(1, std::memory_order_acq_rel);
        p25_voice_frequency_hz = 0;
        request_hot_retune(p25_control_frequency_hz);
      }
      break;
    }
    case ActionKind::auto_follow_toggle:
      p25_auto_follow.store(!p25_auto_follow.load(std::memory_order_relaxed),
                            std::memory_order_relaxed);
      if (!p25_auto_follow.load(std::memory_order_relaxed) &&
          p25_follow_state.load(std::memory_order_acquire) == P25FollowState::voice) {
        p25_follow_state.store(P25FollowState::control, std::memory_order_release);
        p25_voice_session.fetch_add(1, std::memory_order_acq_rel);
        p25_voice_frequency_hz = 0;
        request_hot_retune(p25_control_frequency_hz);
      }
      request_p25_config_save();
      break;
    case ActionKind::encryption_skip_toggle:
      p25_encryption_skip.store(!p25_encryption_skip.load(std::memory_order_relaxed),
                                std::memory_order_relaxed);
      request_p25_config_save();
      break;
    case ActionKind::reload_config:
      p25_survey_active.store(false, std::memory_order_release);
      load_p25_config();
      if (rtl_ui_band == RtlBand::p25) tune_p25_control(p25_control_frequency_hz);
      break;
    case ActionKind::span_down:
    case ActionKind::span_up: {
      const uint32_t current = rtl_scope_span_hz.load(std::memory_order_relaxed);
      rtl_scope_span_hz.store(
          action.kind == ActionKind::span_down
              ? std::max(kRtlScopeSpanMinHz, current / 2)
              : std::min(kRtlScopeSpanMaxHz, current * 2),
          std::memory_order_relaxed);
      reset_spectrum_renderer();
      break;
    }
    case ActionKind::sound_toggle:
      set_rtl_audio_user_enabled(
          !rtl_audio_user_enabled.load(std::memory_order_acquire));
      break;
    case ActionKind::volume_down:
      adjust_rtl_volume(-static_cast<int>(kRtlVolumeStep));
      break;
    case ActionKind::volume_up:
      adjust_rtl_volume(static_cast<int>(kRtlVolumeStep));
      break;
    case ActionKind::open_device_settings:
      open_global_settings(orcsdr::settings::Section::radio_defaults);
      break;
    case ActionKind::exit_to_home:
      orcsdr::p25::leave();
      show_home();
      break;
    case ActionKind::none: break;
  }
  if (orcsdr::p25::active()) orcsdr::p25::update(p25_dashboard_snapshot());
}

void service_p25_survey(uint32_t now) {
  if (g_stream_band != RtlBand::p25) return;
  if (p25_entry_probe_at_ms != 0 && now >= p25_entry_probe_at_ms) {
    p25_entry_probe_at_ms = 0;
    const auto decoded = orcsdr::p25decoder::snapshot();
    if (decoded.tsbk_good <= p25_entry_probe_tsbk_good) {
      Serial.println("RTL_P25_PROBE no_control fallback=survey");
      start_p25_survey();
    } else {
      request_p25_config_save();
      Serial.printf("RTL_P25_PROBE control_hz=%lu tsbk_good=%lu\n",
                    static_cast<unsigned long>(p25_control_frequency_hz),
                    static_cast<unsigned long>(decoded.tsbk_good));
    }
    return;
  }
  if (!p25_survey_active.load(std::memory_order_relaxed) ||
      now < p25_survey_sample_at_ms) return;
  const float level = rtl_signal_dbfs.load(std::memory_order_relaxed);
  const auto decoded = orcsdr::p25decoder::snapshot();
  p25_candidate_levels[p25_candidate_index] = level;
  p25_candidate_tsbk_good[p25_candidate_index] = decoded.tsbk_good;
  Serial.printf("RTL_P25_SURVEY_SAMPLE index=%u frequency_hz=%lu relative_dbfs=%.1f "
                "frame_sync=%d tsbk_good=%lu\n",
                static_cast<unsigned>(p25_candidate_index),
                static_cast<unsigned long>(p25_config.control_channels_hz[p25_candidate_index]),
                static_cast<double>(level), decoded.frame_sync ? 1 : 0,
                static_cast<unsigned long>(decoded.tsbk_good));
  if (++p25_candidate_index < p25_config.control_channel_count) {
    request_hot_retune(p25_config.control_channels_hz[p25_candidate_index]);
    p25_survey_sample_at_ms = now + 1500;
    return;
  }
  const auto best_decoded = std::max_element(std::begin(p25_candidate_tsbk_good),
                                             std::end(p25_candidate_tsbk_good));
  p25_candidate_index = *best_decoded > 0
      ? static_cast<uint8_t>(best_decoded - std::begin(p25_candidate_tsbk_good))
      : static_cast<uint8_t>(std::max_element(
            std::begin(p25_candidate_levels), std::end(p25_candidate_levels)) -
            std::begin(p25_candidate_levels));
  p25_survey_active.store(false, std::memory_order_relaxed);
  p25_control_frequency_hz = p25_config.control_channels_hz[p25_candidate_index];
  request_hot_retune(p25_control_frequency_hz);
  if (p25_candidate_tsbk_good[p25_candidate_index] > 0)
    request_p25_config_save();
  Serial.printf("RTL_P25_SURVEY_DONE best_index=%u frequency_hz=%lu relative_dbfs=%.1f "
                "decoded=%d tsbk_good=%lu\n",
                static_cast<unsigned>(p25_candidate_index),
                static_cast<unsigned long>(p25_config.control_channels_hz[p25_candidate_index]),
                static_cast<double>(p25_candidate_levels[p25_candidate_index]),
                p25_candidate_tsbk_good[p25_candidate_index] > 0 ? 1 : 0,
                static_cast<unsigned long>(p25_candidate_tsbk_good[p25_candidate_index]));
  if (orcsdr::p25::active()) orcsdr::p25::update(p25_dashboard_snapshot());
}

void service_p25_follow(uint32_t now) {
  if (g_stream_band != RtlBand::p25 ||
      p25_survey_active.load(std::memory_order_relaxed)) return;
  const auto decoded = orcsdr::p25decoder::snapshot();
  if (p25_follow_state.load(std::memory_order_acquire) == P25FollowState::voice) {
    const bool never_acquired = decoded.last_voice_ms == 0 &&
                                now - p25_follow_started_ms >= kP25VoiceAcquireMs;
    const bool ended = decoded.last_voice_ms != 0 &&
                       now - decoded.last_voice_ms >= kP25VoiceHangMs;
    if (!never_acquired && !ended) return;
    p25_follow_state.store(P25FollowState::control, std::memory_order_release);
    p25_voice_session.fetch_add(1, std::memory_order_acq_rel);
    p25_voice_frequency_hz = 0;
    request_hot_retune(p25_control_frequency_hz);
    Serial.printf("RTL_P25_FOLLOW_RETURN control_hz=%lu reason=%s tg=%u\n",
                  static_cast<unsigned long>(p25_control_frequency_hz),
                  never_acquired ? "no_voice" : "hang", p25_follow_grant.talkgroup);
    return;
  }

  if (!p25_auto_follow.load(std::memory_order_relaxed)) return;
  const auto& grant = decoded.current_grant;
  if (!grant.valid || grant.frequency_hz == 0 || now - grant.seen_ms > 1500) return;
  if (p25_skipped_talkgroup != 0 &&
      static_cast<int32_t>(p25_skip_until_ms - now) <= 0) p25_skipped_talkgroup = 0;
  if (grant.talkgroup == p25_skipped_talkgroup) return;
  if (grant.tdma || (grant.encrypted && p25_encryption_skip.load(std::memory_order_relaxed))) return;
  if (p25_hold.load(std::memory_order_relaxed)) {
    if (p25_hold_talkgroup == 0) p25_hold_talkgroup = grant.talkgroup;
    if (grant.talkgroup != p25_hold_talkgroup) return;
  }
  if (grant.frequency_hz == rtl_ui_frequency_hz) return;

  p25_control_frequency_hz = rtl_ui_frequency_hz;
  p25_voice_frequency_hz = grant.frequency_hz;
  p25_follow_grant = grant;
  p25_follow_started_ms = now;
  p25_follow_state.store(P25FollowState::voice, std::memory_order_release);
  p25_voice_session.fetch_add(1, std::memory_order_acq_rel);
  (void)ensure_speaker_running(rtl_live_volume.load(std::memory_order_acquire));
  request_hot_retune(grant.frequency_hz);
  Serial.printf("RTL_P25_FOLLOW_VOICE control_hz=%lu voice_hz=%lu tg=%u src=%lu emergency=%d\n",
                static_cast<unsigned long>(p25_control_frequency_hz),
                static_cast<unsigned long>(grant.frequency_hz), grant.talkgroup,
                static_cast<unsigned long>(grant.source_id), grant.emergency ? 1 : 0);
}

orcsdr::settings::State global_settings_state() {
  orcsdr::settings::State state;
  state.wifi_power_enabled = settings_wifi_power_enabled;
  state.wifi_external_antenna = settings_wifi_external_antenna;
  state.wifi_ready = wifi_station_ready;
  state.wifi_scanning = wifi_scan_running;
  state.wifi_connected = wifi_connected;
  state.wifi_connecting = wifi_connecting;
  strlcpy(state.wifi_ssid, wifi_connected ? WiFi.SSID().c_str() : wifi_ssid,
          sizeof(state.wifi_ssid));
  const String ip = wifi_connected ? WiFi.localIP().toString() : String();
  strlcpy(state.wifi_ip, ip.c_str(), sizeof(state.wifi_ip));
  strlcpy(state.wifi_message, wifi_status_message, sizeof(state.wifi_message));
  state.wifi_rssi = wifi_connected ? WiFi.RSSI() : 0;
  state.saved_network_count = wifi_profile_count;
  for (uint8_t i = 0; i < wifi_profile_count; ++i) {
    strlcpy(state.profiles[i].ssid, wifi_profiles[i].ssid,
            sizeof(state.profiles[i].ssid));
    state.profiles[i].connected =
        wifi_connected && strcmp(WiFi.SSID().c_str(), wifi_profiles[i].ssid) == 0;
  }
  if (!wifi_scan_running && wifi_scan_result_count > 0) {
    state.network_count = wifi_scan_result_count;
    for (uint8_t i = 0; i < state.network_count; ++i) {
      strlcpy(state.networks[i].ssid, wifi_scan_results[i].ssid,
              sizeof(state.networks[i].ssid));
      state.networks[i].rssi = wifi_scan_results[i].rssi;
      state.networks[i].secure = wifi_scan_results[i].secure;
      for (uint8_t saved = 0; saved < wifi_profile_count; ++saved) {
        if (strcmp(state.networks[i].ssid, wifi_profiles[saved].ssid) == 0) {
          state.networks[i].saved = true;
          break;
        }
      }
    }
  }
  state.location_configured = adsb_settings.location_configured;
  state.latitude_e7 = adsb_settings.latitude_e7;
  state.longitude_e7 = adsb_settings.longitude_e7;
  state.radar_range_nm = adsb_settings.radar_range_nm;
  strlcpy(state.location_label, settings_location_label, sizeof(state.location_label));
  strlcpy(state.map_pack, settings_map_pack, sizeof(state.map_pack));
  state.brightness = settings_brightness;
  state.rotation = settings_rotation;
  state.screen_timeout_sec = settings_screen_timeout_sec;
  state.volume = rtl_live_volume.load(std::memory_order_acquire);
  state.sound_default = rtl_audio_user_enabled.load(std::memory_order_acquire);
  state.auto_start_reception = settings_auto_start_reception;
  state.graphics_default = settings_graphics_default;
  strlcpy(state.default_band, rtl_band_name(rtl_ui_band), sizeof(state.default_band));
  state.fm_frequency_hz = rtl_saved_fm_hz;
  state.sd_ready = g_sd_ready;
  state.sd_total_bytes = sd_total_bytes();
  state.sd_free_bytes = g_sd_ready && g_sd_fs ? state.sd_total_bytes -
      (g_sd_fs == &SD_MMC ? SD_MMC.usedBytes() : SD.usedBytes()) : 0;
  const auto catalog_state = orcsdr::catalog::state();
  state.catalog_ready = catalog_state.ready;
  state.catalog_busy = catalog_state.busy;
  state.catalog_progress_percent = catalog_state.progress_percent;
  strlcpy(state.catalog_message, catalog_state.message, sizeof(state.catalog_message));
  strlcpy(state.catalog_date, catalog_state.catalog_date, sizeof(state.catalog_date));
  for (uint8_t i = 0; i < std::size(state.catalog_packs); ++i) {
    const auto& source = catalog_state.packs[i];
    auto& target = state.catalog_packs[i];
    strlcpy(target.id, source.id, sizeof(target.id));
    strlcpy(target.title, source.title, sizeof(target.title));
    strlcpy(target.version, source.version, sizeof(target.version));
    strlcpy(target.source_date, source.source_date, sizeof(target.source_date));
    strlcpy(target.status, source.status, sizeof(target.status));
    target.runtime_bytes = source.runtime_bytes;
    target.archive_bytes = source.archive_bytes;
    target.installed = source.installed;
    target.update_available = source.update_available;
  }
  state.companion_supported = false;
  state.battery_level = M5.Power.getBatteryLevel();
  state.battery_mv = M5.Power.getBatteryVoltage();
  state.battery_current_ma = M5.Power.getBatteryCurrent();
  state.vbus_mv = M5.Power.getVBUSVoltage();
  strlcpy(state.charging_state, charging_state(), sizeof(state.charging_state));
  snprintf(state.build_identity, sizeof(state.build_identity), "%s %s", __DATE__, __TIME__);
  state.uptime_seconds = millis() / 1000;
  return state;
}

orcsdr::home::Snapshot home_dashboard_snapshot(bool demo) {
  static orcsdr::home::Snapshot previous{};
  orcsdr::home::Snapshot snapshot{};
  snapshot.frequency_hz = demo ? 145700000 : rtl_ui_frequency_hz;
  snapshot.requested_frequency_hz = demo
                                        ? snapshot.frequency_hz
                                        : rtl_requested_frequency_hz.load(
                                              std::memory_order_acquire);
  snapshot.span_hz = demo ? 500000 : rtl_scope_span_hz.load(std::memory_order_relaxed);
  snapshot.filter_bandwidth_hz = demo
                                     ? 200000
                                     : rtl_filter_bandwidth_hz.load(
                                           std::memory_order_relaxed);
  snapshot.step_hz = rtl_ui_band == RtlBand::fm       ? rtl_fm_step_hz
                     : rtl_ui_band == RtlBand::p25    ? kP25StepHz
                     : rtl_ui_band == RtlBand::cb     ? 10000
                     : rtl_ui_band == RtlBand::lora   ? 125000
                     : rtl_ui_band == RtlBand::am     ? kRtlAmStepHz
                                                       : 12500;
  strlcpy(snapshot.mode, demo ? "FM" : rtl_band_name(rtl_ui_band),
          sizeof(snapshot.mode));
  snapshot.battery_percent = demo ? 76 : M5.Power.getBatteryLevel();
  snapshot.vbus_mv = demo ? 5000 : M5.Power.getVBUSVoltage();
  snapshot.volume = demo ? 128 : rtl_live_volume.load(std::memory_order_acquire);
  snapshot.usb_connected = snapshot.vbus_mv >= 4000;
  if (!demo) {
    const float raw = rtl_signal_dbfs.load(std::memory_order_relaxed);
    rtl_signal_dbfs_smooth = 0.88f * rtl_signal_dbfs_smooth + 0.12f * raw;
  }
  snapshot.relative_dbfs = demo ? -32.0f : rtl_signal_dbfs_smooth;
  snapshot.wifi_connected = demo || wifi_connected;
  if (demo) {
    strlcpy(snapshot.wifi_ip, "192.0.2.42", sizeof(snapshot.wifi_ip));
    strlcpy(snapshot.clock, "12:45", sizeof(snapshot.clock));
    strlcpy(snapshot.date, "DEMO", sizeof(snapshot.date));
  } else {
    const String ip = wifi_connected ? WiFi.localIP().toString() : String();
    ip.toCharArray(snapshot.wifi_ip, sizeof(snapshot.wifi_ip));
    const uint32_t seconds = millis() / 1000u;
    snprintf(snapshot.clock, sizeof(snapshot.clock), "UP %02lu:%02lu",
             static_cast<unsigned long>((seconds / 3600u) % 100u),
             static_cast<unsigned long>((seconds / 60u) % 60u));
    strlcpy(snapshot.date, "DEVICE UPTIME", sizeof(snapshot.date));
  }
  snapshot.driver_ready = demo || rtl_device_ready();
  snapshot.receiving = demo ||
      rtl_capture_state.load(std::memory_order_acquire) == RtlCaptureState::running;
  snapshot.sound_enabled = rtl_audio_user_enabled.load(std::memory_order_relaxed);
#if !RTL_USE_LEGACY_USB
  rtl_sdr_v4_esp_metrics_t metrics{};
  if (!demo && g_rtl != nullptr && rtl_sdr_v4_esp_get_metrics(g_rtl, &metrics) == ESP_OK)
    snapshot.effective_sps = metrics.effective_sps;
#endif
  if (demo) snapshot.effective_sps = 959800;

  const bool tuner_changed = previous.revision == 0 ||
      snapshot.frequency_hz != previous.frequency_hz ||
      snapshot.requested_frequency_hz != previous.requested_frequency_hz ||
      snapshot.span_hz != previous.span_hz || snapshot.step_hz != previous.step_hz ||
      snapshot.filter_bandwidth_hz != previous.filter_bandwidth_hz ||
      strcmp(snapshot.mode, previous.mode) != 0;
  const bool audio_changed = previous.revision == 0 ||
                             snapshot.sound_enabled != previous.sound_enabled ||
                             snapshot.volume != previous.volume;
  const bool status_changed = previous.revision == 0 ||
      snapshot.battery_percent != previous.battery_percent ||
      snapshot.vbus_mv != previous.vbus_mv ||
      snapshot.wifi_connected != previous.wifi_connected ||
      strcmp(snapshot.wifi_ip, previous.wifi_ip) != 0 ||
      snapshot.driver_ready != previous.driver_ready ||
      snapshot.receiving != previous.receiving ||
      snapshot.effective_sps != previous.effective_sps ||
      static_cast<int>(snapshot.relative_dbfs) != static_cast<int>(previous.relative_dbfs) ||
      strcmp(snapshot.clock, previous.clock) != 0;
  snapshot.tuner_revision = previous.tuner_revision + (tuner_changed ? 1u : 0u);
  snapshot.audio_revision = previous.audio_revision + (audio_changed ? 1u : 0u);
  snapshot.status_revision = previous.status_revision + (status_changed ? 1u : 0u);
  snapshot.revision = previous.revision +
      ((tuner_changed || audio_changed || status_changed) ? 1u : 0u);
  previous = snapshot;
  return snapshot;
}

void show_home(bool demo) {
  orcsdr::fm::leave();
  orcsdr::p25::leave();
  orcsdr::adsb::leave();
  orcsdr::settings::leave();
  rtl_nav_open = false;
  rtl_frequency_keypad_open = false;
  orcsdr::home::enter(home_dashboard_snapshot(demo));
  if (demo) {
    float levels[256];
    for (size_t i = 0; i < std::size(levels); ++i) {
      const float center = static_cast<float>(static_cast<int>(i) - 128);
      const float carrier = 72.0f * expf(-(center * center) / 70.0f);
      const float side = 20.0f * expf(-((center - 52.0f) * (center - 52.0f)) / 18.0f);
      levels[i] = -105.0f + carrier + side + 4.0f * sinf(i * 0.39f);
    }
    orcsdr::home::draw_spectrum(levels, 0, std::size(levels), -105.0f);
  }
}

orcsdr::dashboards::Id dashboard_for_band(RtlBand band, uint32_t frequency_hz) {
  using Id = orcsdr::dashboards::Id;
  switch (band) {
    case RtlBand::fm: return Id::fm;
    case RtlBand::p25: return Id::p25;
    case RtlBand::adsb: return Id::adsb;
    case RtlBand::wx: return Id::weather;
    case RtlBand::cb: return Id::cb;
    case RtlBand::lora: return Id::lora;
    case RtlBand::am: return Id::shortwave;
    case RtlBand::browse:
      if (frequency_hz >= 118000000 && frequency_hz <= 137000000) return Id::airband;
      if (frequency_hz >= 156000000 && frequency_hz <= 162025000) return Id::marine;
      if (frequency_hz >= 1000000 && frequency_hz <= 30000000) return Id::shortwave;
      return Id::utilities;
  }
  return Id::utilities;
}

void persist_dashboard_open(orcsdr::dashboards::Id id) {
  if (!orcsdr::dashboards::record_open(id)) return;
  uint8_t recent[orcsdr::dashboards::kRecentCapacity]{};
  const size_t count = orcsdr::dashboards::copy_recent(recent, sizeof(recent));
  preferences.putBytes("dash_recent", recent, count);
}

void open_dashboard(orcsdr::dashboards::Id id) {
  using Id = orcsdr::dashboards::Id;
  RtlBand band = RtlBand::browse;
  uint32_t frequency = rtl_ui_frequency_hz;
  switch (id) {
    case Id::fm: band = RtlBand::fm; frequency = rtl_saved_fm_hz; break;
    case Id::p25: band = RtlBand::p25; frequency = p25_control_frequency_hz; break;
    case Id::adsb: band = RtlBand::adsb; frequency = kAdsbDefaultHz; break;
    case Id::shortwave: band = RtlBand::browse; frequency = 7100000; break;
    case Id::weather: band = RtlBand::wx; frequency = kRtlWxHz; break;
    case Id::cb: band = RtlBand::cb; frequency = kCbDefaultHz; break;
    case Id::lora: band = RtlBand::lora; frequency = kLoraDefaultHz; break;
    case Id::airband: band = RtlBand::browse; frequency = 121500000; break;
    case Id::marine: band = RtlBand::browse; frequency = 156800000; break;
    case Id::satellite: band = RtlBand::browse; frequency = 137500000; break;
    case Id::utilities: band = RtlBand::browse; break;
    case Id::settings:
      persist_dashboard_open(id);
      open_global_settings(orcsdr::settings::Section::connectivity);
      return;
    default: return;
  }
  persist_dashboard_open(id);
  orcsdr::home::leave();
  if (rtl_capture_state.load(std::memory_order_acquire) == RtlCaptureState::running &&
      band == rtl_ui_band) {
    request_hot_retune(frequency);
    preferences.putUInt("last_band", static_cast<uint32_t>(band));
  } else {
    queue_local_rtl_listen(band, frequency);
  }
  draw_sdr_screen(band, frequency, rtl_live_volume.load(std::memory_order_acquire));
}

void handle_home_action(const orcsdr::home::Action& action) {
  using orcsdr::home::ActionKind;
  switch (action.kind) {
    case ActionKind::open_dashboard: open_dashboard(action.dashboard); return;
    case ActionKind::tune_frequency:
      if (rtl_capture_state.load(std::memory_order_acquire) == RtlCaptureState::running)
        request_hot_retune(action.value);
      else
        queue_local_rtl_listen(rtl_ui_band, action.value, false);
      break;
    case ActionKind::span_down:
    case ActionKind::span_up: {
      const uint32_t current_span = rtl_scope_span_hz.load(std::memory_order_relaxed);
      rtl_scope_span_hz.store(action.kind == ActionKind::span_down
                                  ? std::max(kRtlScopeSpanMinHz, current_span / 2)
                                  : std::min(kRtlScopeSpanMaxHz, current_span * 2),
                              std::memory_order_relaxed);
      reset_spectrum_renderer();
      break;
    }
    case ActionKind::step_down:
    case ActionKind::step_up: {
      const uint32_t next = rtl_step_frequency(
          rtl_ui_band, rtl_ui_frequency_hz,
          action.kind == ActionKind::step_down ? -1 : 1);
      if (rtl_capture_state.load(std::memory_order_acquire) == RtlCaptureState::running)
        request_hot_retune(next);
      else
        queue_local_rtl_listen(rtl_ui_band, next, false);
      break;
    }
    case ActionKind::sound_toggle:
      set_rtl_audio_user_enabled(
          !rtl_audio_user_enabled.load(std::memory_order_acquire));
      break;
    case ActionKind::volume_down:
      adjust_rtl_volume(-static_cast<int>(kRtlVolumeStep));
      break;
    case ActionKind::volume_up:
      adjust_rtl_volume(static_cast<int>(kRtlVolumeStep));
      break;
    default: return;
  }
  orcsdr::home::update(home_dashboard_snapshot());
}

void open_global_settings(orcsdr::settings::Section section) {
  if (!ui_documentation_mode)
    persist_dashboard_open(orcsdr::dashboards::Id::settings);
  settings_restore_home = orcsdr::home::active();
  if (settings_restore_home) orcsdr::home::leave();
  settings_restore_graphics = rtl_graphics_enabled.exchange(false, std::memory_order_acq_rel);
  rtl_nav_open = false;
  orcsdr::settings::enter(global_settings_state(), section);
}

void close_global_settings() {
  rtl_graphics_enabled.store(settings_restore_graphics, std::memory_order_release);
  if (settings_restore_home) {
    settings_restore_home = false;
    show_home();
  } else if (rtl_ui_band == RtlBand::adsb) {
    orcsdr::adsb::draw();
    draw_global_settings_gear();
  } else if (rtl_ui_band == RtlBand::fm) {
    orcsdr::fm::draw();
    bump_rtl_ui();
  } else if (rtl_ui_band == RtlBand::p25) {
    orcsdr::p25::draw();
    bump_rtl_ui();
  } else {
    draw_sdr_screen(rtl_ui_band, rtl_ui_frequency_hz,
                    rtl_live_volume.load(std::memory_order_acquire));
  }
}

void handle_global_settings_touch(int32_t x, int32_t y) {
  const auto action = orcsdr::settings::handle_touch(x, y);
  switch (action.kind) {
    case orcsdr::settings::ActionKind::close:
      close_global_settings();
      break;
    case orcsdr::settings::ActionKind::wifi_power_changed:
      settings_wifi_power_enabled = action.value != 0;
      preferences.putBool("set_wifi_power", settings_wifi_power_enabled);
      if (!settings_wifi_power_enabled) {
        wifi_scan_requested.store(false, std::memory_order_release);
        wifi_connect_requested.store(false, std::memory_order_release);
        stop_wifi();
      } else {
        strlcpy(wifi_status_message, "Wi-Fi ready; choose Scan or Use", sizeof(wifi_status_message));
      }
      orcsdr::settings::update(global_settings_state());
      break;
    case orcsdr::settings::ActionKind::wifi_antenna_changed:
      settings_wifi_external_antenna = action.value != 0;
      preferences.putBool("set_wifi_ext_ant", settings_wifi_external_antenna);
      apply_wifi_antenna();
      strlcpy(wifi_status_message,
              settings_wifi_external_antenna ? "External MMCX antenna selected"
                                             : "Internal antenna selected",
              sizeof(wifi_status_message));
      orcsdr::settings::update(global_settings_state());
      break;
    case orcsdr::settings::ActionKind::scan_wifi:
      wifi_scan_requested.store(true, std::memory_order_release);
      strlcpy(wifi_status_message, "Scan queued", sizeof(wifi_status_message));
      orcsdr::settings::update(global_settings_state());
      break;
    case orcsdr::settings::ActionKind::connect_wifi: {
      char ssid[33]{}, password[64]{};
      if (!orcsdr::settings::take_wifi_credentials(
              ssid, sizeof(ssid), password, sizeof(password))) break;
      bool already_saved = false;
      for (uint8_t i = 0; i < wifi_profile_count; ++i)
        already_saved |= strcmp(wifi_profiles[i].ssid, ssid) == 0;
      if (!already_saved && wifi_profile_count >= std::size(wifi_profiles)) {
        strlcpy(wifi_status_message, "Four-profile limit reached",
                sizeof(wifi_status_message));
      } else {
        start_wifi_connection(ssid, password, true);
      }
      memset(password, 0, sizeof(password));
      orcsdr::settings::update(global_settings_state());
      break;
    }
    case orcsdr::settings::ActionKind::connect_saved_wifi:
      if (action.value >= 0 && action.value < wifi_profile_count) {
        select_wifi_profile(static_cast<uint8_t>(action.value));
        wifi_save_after_connect = false;
        wifi_connect_requested.store(true, std::memory_order_release);
        orcsdr::settings::update(global_settings_state());
      }
      break;
    case orcsdr::settings::ActionKind::forget_wifi:
      if (action.value >= 0 && action.value < wifi_profile_count) {
        const uint8_t index = static_cast<uint8_t>(action.value);
        if (wifi_connected && strcmp(WiFi.SSID().c_str(), wifi_profiles[index].ssid) == 0) {
          WiFi.disconnect();
          wifi_connected = false;
        }
        for (uint8_t i = index; i + 1 < wifi_profile_count; ++i)
          wifi_profiles[i] = wifi_profiles[i + 1];
        wifi_profiles[--wifi_profile_count] = {};
        persist_wifi_profiles();
        if (wifi_profile_count) select_wifi_profile(0);
        else {
          wifi_ssid[0] = wifi_password[0] = '\0';
          stop_wifi();
        }
        strlcpy(wifi_status_message, "Profile forgotten", sizeof(wifi_status_message));
        orcsdr::settings::update(global_settings_state());
      }
      break;
    case orcsdr::settings::ActionKind::move_wifi_up:
    case orcsdr::settings::ActionKind::move_wifi_down: {
      const int from = action.value;
      const int to = action.kind == orcsdr::settings::ActionKind::move_wifi_up
                         ? from - 1 : from + 1;
      if (from >= 0 && from < wifi_profile_count && to >= 0 && to < wifi_profile_count) {
        std::swap(wifi_profiles[from], wifi_profiles[to]);
        persist_wifi_profiles();
        orcsdr::settings::update(global_settings_state());
      }
      break;
    }
    case orcsdr::settings::ActionKind::location_changed: {
      const auto& state = orcsdr::settings::state();
      adsb_settings.location_configured = state.location_configured;
      adsb_settings.latitude_e7 = state.latitude_e7;
      adsb_settings.longitude_e7 = state.longitude_e7;
      adsb_settings_persist_pending.store(true, std::memory_order_release);
      break;
    }
    case orcsdr::settings::ActionKind::range_changed:
      adsb_settings.radar_range_nm = static_cast<uint16_t>(action.value);
      adsb_settings_persist_pending.store(true, std::memory_order_release);
      break;
    case orcsdr::settings::ActionKind::brightness_changed:
      settings_brightness = static_cast<uint8_t>(action.value);
      M5.Display.setBrightness(settings_brightness);
      preferences.putUChar("set_bright", settings_brightness);
      break;
    case orcsdr::settings::ActionKind::rotation_changed:
      settings_rotation = action.value == 3 ? 3 : 1;
      M5.Display.setRotation(settings_rotation);
      preferences.putUChar("set_rotation", settings_rotation);
      orcsdr::settings::draw();
      break;
    case orcsdr::settings::ActionKind::timeout_changed:
      settings_screen_timeout_sec = static_cast<uint16_t>(action.value);
      preferences.putUShort("set_timeout", settings_screen_timeout_sec);
      break;
    case orcsdr::settings::ActionKind::volume_changed:
      rtl_ui_volume = static_cast<uint8_t>(action.value);
      rtl_live_volume.store(rtl_ui_volume, std::memory_order_release);
      rtl_requested_volume.store(rtl_ui_volume, std::memory_order_release);
      rtl_volume_changed.store(true, std::memory_order_release);
      apply_speaker_volume(rtl_ui_volume);
      schedule_rtl_audio_settings_persist();
      break;
    case orcsdr::settings::ActionKind::sound_changed:
      settings_sound_default = action.value != 0;
      set_rtl_audio_user_enabled(settings_sound_default);
      break;
    case orcsdr::settings::ActionKind::auto_start_changed:
      settings_auto_start_reception = action.value != 0;
      preferences.putBool("set_auto_rx", settings_auto_start_reception);
      break;
    case orcsdr::settings::ActionKind::graphics_changed:
      settings_graphics_default = action.value != 0;
      preferences.putBool("set_gfx", settings_graphics_default);
      break;
    case orcsdr::settings::ActionKind::catalog_check:
      if (ensure_tab5_sd()) {
        orcsdr::catalog::begin(g_sd_fs, sd_total_bytes() -
            (g_sd_fs == &SD_MMC ? SD_MMC.usedBytes() : SD.usedBytes()));
        if (!orcsdr::catalog::request_check(wifi_connected))
          Serial.println("ORC_CATALOG_CHECK_REJECTED");
      }
      orcsdr::settings::update(global_settings_state());
      break;
    case orcsdr::settings::ActionKind::catalog_install:
      if (!orcsdr::catalog::request_install(static_cast<uint8_t>(action.value), wifi_connected))
        Serial.println("ORC_CATALOG_INSTALL_REJECTED");
      orcsdr::settings::update(global_settings_state());
      break;
    case orcsdr::settings::ActionKind::catalog_remove:
      if (!orcsdr::catalog::request_remove(static_cast<uint8_t>(action.value)))
        Serial.println("ORC_CATALOG_REMOVE_REJECTED");
      orcsdr::settings::update(global_settings_state());
      break;
    default: break;
  }
}

void draw_ui() {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
  M5.Display.setTextSize(5);
  M5.Display.drawString("OrcLink", 640, 90);

  M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
  M5.Display.setTextSize(3);
  M5.Display.drawString("M5Tab5 agent online", 640, 175);

  M5.Display.fillRoundRect(kButtonX, kButtonY, kButtonWidth, kButtonHeight, 18, TFT_DARKCYAN);
  M5.Display.drawRoundRect(kButtonX, kButtonY, kButtonWidth, kButtonHeight, 18, TFT_CYAN);
  M5.Display.setTextColor(TFT_WHITE, TFT_DARKCYAN);
  M5.Display.setTextSize(3);
  M5.Display.drawString("Tap to verify touch", 640, 360);

  M5.Display.setTextSize(3);
  draw_touch_state("Waiting for touch", TFT_LIGHTGREY);
}

bool decode_hex(const char* value, uint8_t* output, size_t output_size) {
  auto digit = [](char value) -> int {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
  };
  if (strlen(value) != output_size * 2) return false;
  for (size_t index = 0; index < output_size; ++index) {
    const int high = digit(value[index * 2]);
    const int low = digit(value[index * 2 + 1]);
    if (high < 0 || low < 0) return false;
    output[index] = static_cast<uint8_t>((high << 4) | low);
  }
  return true;
}

bool decode_hex_text(const char* value, char* output, size_t output_size) {
  const size_t length = strlen(value);
  if (length % 2 != 0 || length / 2 >= output_size) return false;
  auto digit = [](char value) -> int {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
  };
  for (size_t index = 0; index < length / 2; ++index) {
    const int high = digit(value[index * 2]);
    const int low = digit(value[index * 2 + 1]);
    if (high < 0 || low < 0) return false;
    output[index] = static_cast<char>((high << 4) | low);
  }
  output[length / 2] = '\0';
  return true;
}

bool parse_hex_u32_exact(const char* value, uint32_t* output) {
  if (value == nullptr || output == nullptr || strlen(value) != 8) return false;
  char* end = nullptr;
  const unsigned long parsed = strtoul(value, &end, 16);
  if (end == nullptr || *end != '\0') return false;
  *output = static_cast<uint32_t>(parsed);
  return true;
}

bool lora_present_host_message(char* fields) {
  if (fields == nullptr) return false;
  char* sender_text = fields;
  char* packet_text = strchr(sender_text, ' ');
  if (packet_text == nullptr) return false;
  *packet_text++ = '\0';
  char* message_hex = strchr(packet_text, ' ');
  if (message_hex == nullptr) return false;
  *message_hex++ = '\0';
  uint32_t sender = 0;
  uint32_t packet_id = 0;
  char message[sizeof(lora_display_packets[0].text)];
  if (!parse_hex_u32_exact(sender_text, &sender) ||
      !parse_hex_u32_exact(packet_text, &packet_id) ||
      !decode_hex_text(message_hex, message, sizeof(message)) || message[0] == '\0') {
    return false;
  }
  for (char* p = message; *p; ++p) {
    const unsigned char value = static_cast<unsigned char>(*p);
    if (value < 0x20 || value == 0x7f) *p = ' ';
  }
  LoraDisplayPacket logged_packet;
  portENTER_CRITICAL(&lora_message_mux);
  memmove(&lora_display_packets[1], &lora_display_packets[0],
          sizeof(lora_display_packets[0]) * (kLoraDisplayPacketCount - 1));
  lora_display_packets[0] = {};
  strlcpy(lora_display_packets[0].text, message, sizeof(lora_display_packets[0].text));
  lora_display_packets[0].sender = sender;
  lora_display_packets[0].packet_id = packet_id;
  lora_display_packets[0].received_ms = millis();
  lora_display_packets[0].port = 1;
  logged_packet = lora_display_packets[0];
  portEXIT_CRITICAL(&lora_message_mux);
  enqueue_lora_sd_log(logged_packet);
  lora_messages.fetch_add(1, std::memory_order_relaxed);
  bump_rtl_ui();
  return true;
}

bool lora_present_host_packet(char* fields) {
  if (fields == nullptr) return false;
  char* values[9]{};
  char* context = nullptr;
  for (size_t index = 0; index < std::size(values); ++index) {
    values[index] = strtok_r(index == 0 ? fields : nullptr, " ", &context);
    if (values[index] == nullptr) return false;
  }
  if (strtok_r(nullptr, " ", &context) != nullptr) return false;
  uint32_t sender = 0;
  uint32_t destination = 0;
  uint32_t packet_id = 0;
  char* end = nullptr;
  const unsigned long port = strtoul(values[3], &end, 10);
  if (!parse_hex_u32_exact(values[0], &sender) ||
      !parse_hex_u32_exact(values[1], &destination) ||
      !parse_hex_u32_exact(values[2], &packet_id) || end == nullptr || *end != '\0' || port > UINT16_MAX) {
    return false;
  }
  const long snr = strtol(values[4], &end, 10);
  if (end == nullptr || *end != '\0' || snr < INT16_MIN || snr > INT16_MAX) return false;
  const long signal = strtol(values[5], &end, 10);
  if (end == nullptr || *end != '\0' || signal < INT16_MIN || signal > INT16_MAX) return false;
  const long latitude = strtol(values[6], &end, 10);
  if (end == nullptr || *end != '\0') return false;
  const long longitude = strtol(values[7], &end, 10);
  if (end == nullptr || *end != '\0') return false;
  char text[sizeof(lora_display_packets[0].text)]{};
  if (strcmp(values[8], "-") != 0 && !decode_hex_text(values[8], text, sizeof(text))) return false;
  for (char* p = text; *p; ++p) {
    const unsigned char value = static_cast<unsigned char>(*p);
    if (value < 0x20 || value == 0x7f) *p = ' ';
  }
  LoraDisplayPacket logged_packet;
  portENTER_CRITICAL(&lora_message_mux);
  memmove(&lora_display_packets[1], &lora_display_packets[0],
          sizeof(lora_display_packets[0]) * (kLoraDisplayPacketCount - 1));
  lora_display_packets[0] = {};
  strlcpy(lora_display_packets[0].text, text, sizeof(lora_display_packets[0].text));
  lora_display_packets[0].sender = sender;
  lora_display_packets[0].destination = destination;
  lora_display_packets[0].packet_id = packet_id;
  lora_display_packets[0].received_ms = millis();
  lora_display_packets[0].latitude_e7 = static_cast<int32_t>(latitude);
  lora_display_packets[0].longitude_e7 = static_cast<int32_t>(longitude);
  lora_display_packets[0].snr_tenths = static_cast<int16_t>(snr);
  lora_display_packets[0].signal_tenths = static_cast<int16_t>(signal);
  lora_display_packets[0].port = static_cast<uint16_t>(port);
  if (latitude != INT32_MAX && longitude != INT32_MAX) {
    size_t position_index = 0;
    while (position_index < kLoraNodePositionCount &&
           lora_node_positions[position_index].node != sender) {
      ++position_index;
    }
    if (position_index >= kLoraNodePositionCount) {
      position_index = kLoraNodePositionCount - 1;
    }
    if (position_index > 0) {
      memmove(&lora_node_positions[1], &lora_node_positions[0],
              sizeof(lora_node_positions[0]) * position_index);
    }
    lora_node_positions[0] = {
        sender, millis(), static_cast<int32_t>(latitude), static_cast<int32_t>(longitude)};
  }
  logged_packet = lora_display_packets[0];
  portEXIT_CRITICAL(&lora_message_mux);
  enqueue_lora_sd_log(logged_packet);
  lora_messages.fetch_add(1, std::memory_order_relaxed);
  bump_rtl_ui();
  return true;
}

void print_hex(const uint8_t* value, size_t size) {
  static constexpr char kHex[] = "0123456789abcdef";
  for (size_t index = 0; index < size; ++index) {
    Serial.print(kHex[value[index] >> 4]);
    Serial.print(kHex[value[index] & 0x0f]);
  }
}

void persist_journal() {
  preferences.putBytes("journal", &journal, sizeof(journal));
}

void persist_workflow() {
  preferences.putBytes("workflow", &workflow, sizeof(workflow));
}

void load_state() {
  preferences.begin("orclink", false);
  paired = preferences.getBytesLength("pair_key") == sizeof(pairing_key);
  if (paired) preferences.getBytes("pair_key", pairing_key, sizeof(pairing_key));
  if (preferences.getBytesLength("journal") == sizeof(journal)) {
    preferences.getBytes("journal", &journal, sizeof(journal));
  }
  if (journal.magic != kJournalMagic || journal.head >= kJournalCapacity ||
      journal.count > kJournalCapacity) {
    memset(&journal, 0, sizeof(journal));
    journal.magic = kJournalMagic;
    persist_journal();
  }
  if (preferences.getBytesLength("workflow") == sizeof(workflow)) {
    preferences.getBytes("workflow", &workflow, sizeof(workflow));
  }
  if (workflow.magic != kWorkflowMagic) {
    memset(&workflow, 0, sizeof(workflow));
    workflow.magic = kWorkflowMagic;
    persist_workflow();
  }
  const String stored_ssid = preferences.isKey("wifi_ssid")
                                 ? preferences.getString("wifi_ssid", "")
                                 : String();
  const String stored_password = preferences.isKey("wifi_pass")
                                     ? preferences.getString("wifi_pass", "")
                                     : String();
  for (uint8_t i = 0; i < std::size(wifi_profiles); ++i) {
    char ssid_key[16], pass_key[16];
    snprintf(ssid_key, sizeof(ssid_key), "wifi%u_ssid", i);
    snprintf(pass_key, sizeof(pass_key), "wifi%u_pass", i);
    const String ssid = preferences.isKey(ssid_key)
                            ? preferences.getString(ssid_key, "") : String();
    const String password = preferences.isKey(pass_key)
                                ? preferences.getString(pass_key, "") : String();
    if (ssid.isEmpty() || ssid.length() > 32 || password.length() > 63) continue;
    ssid.toCharArray(wifi_profiles[wifi_profile_count].ssid,
                     sizeof(wifi_profiles[wifi_profile_count].ssid));
    password.toCharArray(wifi_profiles[wifi_profile_count].password,
                         sizeof(wifi_profiles[wifi_profile_count].password));
    ++wifi_profile_count;
  }
  const bool legacy_valid = !stored_ssid.isEmpty() && stored_ssid.length() <= 32 &&
                            stored_password.length() <= 63;
  if (wifi_profile_count == 0 && legacy_valid) {
    stored_ssid.toCharArray(wifi_profiles[0].ssid, sizeof(wifi_profiles[0].ssid));
    stored_password.toCharArray(wifi_profiles[0].password,
                                sizeof(wifi_profiles[0].password));
    preferences.putString("wifi0_ssid", wifi_profiles[0].ssid);
    preferences.putString("wifi0_pass", wifi_profiles[0].password);
    wifi_profile_count = 1;
  }
  wifi_configured = wifi_profile_count > 0;
  if (wifi_configured) {
    strlcpy(wifi_ssid, wifi_profiles[0].ssid, sizeof(wifi_ssid));
    strlcpy(wifi_password, wifi_profiles[0].password, sizeof(wifi_password));
  }
  settings_brightness = preferences.getUChar("set_bright", 180);
  if (settings_brightness < 16) settings_brightness = 16;
  settings_rotation = preferences.getUChar("set_rotation", 1);
  if (settings_rotation != 1 && settings_rotation != 3) settings_rotation = 1;
  settings_screen_timeout_sec = preferences.getUShort("set_timeout", 0);
  settings_wifi_power_enabled = preferences.getBool("set_wifi_power", true);
  settings_wifi_external_antenna = preferences.getBool("set_wifi_ext_ant", false);
  settings_sound_default = preferences.getBool("set_sound", true);
  rtl_audio_user_enabled.store(settings_sound_default, std::memory_order_release);
  rtl_audio_enabled.store(settings_sound_default && rtl_band_has_audio(rtl_ui_band),
                          std::memory_order_release);
  settings_auto_start_reception = preferences.getBool("set_auto_rx", true);
  settings_graphics_default = preferences.getBool("set_gfx", true);
  const String location_label = preferences.isKey("loc_label")
                                    ? preferences.getString("loc_label", "") : String();
  const String map_pack = preferences.isKey("map_pack")
                              ? preferences.getString("map_pack", "") : String();
  location_label.toCharArray(settings_location_label, sizeof(settings_location_label));
  map_pack.toCharArray(settings_map_pack, sizeof(settings_map_pack));
  rtl_ui_volume = preferences.getUChar("set_volume", kRtlVolumeDefault);
  rtl_live_volume.store(rtl_ui_volume, std::memory_order_release);
  rtl_graphics_enabled.store(settings_graphics_default, std::memory_order_release);
  M5.Display.setBrightness(settings_brightness);
  M5.Display.setRotation(settings_rotation);
  // Reuse last good FM LO (KZEL 96.113 default when none stored yet).
  if (preferences.isKey("sdr_fm_hz")) {
    const uint32_t stored_fm = preferences.getUInt("sdr_fm_hz", kRtlFmDefaultHz);
    if (stored_fm >= kRtlFmMinHz && stored_fm <= kRtlFmMaxHz) {
      rtl_saved_fm_hz = stored_fm;
    }
  } else {
    rtl_saved_fm_hz = kRtlFmDefaultHz;
    preferences.putUInt("sdr_fm_hz", rtl_saved_fm_hz);
  }
  load_p25_config();
  if (preferences.isKey("fm_presets") &&
      preferences.getBytesLength("fm_presets") == sizeof(fm_presets)) {
    preferences.getBytes("fm_presets", fm_presets, sizeof(fm_presets));
    const uint8_t stored_count = preferences.getUChar("fm_preset_count", 0);
    fm_preset_count = (stored_count <= kFmPresetCapacity) ? stored_count : 0;
    Serial.printf("RTL_PRESETS_LOAD count=%d\n", fm_preset_count);
  }
  load_fm_config();
  rtl_ui_frequency_hz = rtl_saved_fm_hz;
  rtl_requested_frequency_hz.store(rtl_saved_fm_hz, std::memory_order_release);
  Serial.printf("RTL_FM_LOAD frequency_hz=%u\n", rtl_saved_fm_hz);
  adsb_settings.location_configured = preferences.getBool("adsb_loc_set", false);
  adsb_settings.latitude_e7 = preferences.getInt("adsb_lat_e7", 0);
  adsb_settings.longitude_e7 = preferences.getInt("adsb_lon_e7", 0);
  adsb_settings.radar_range_nm = preferences.getUShort("adsb_range", 25);
  if (adsb_settings.latitude_e7 < -900000000 || adsb_settings.latitude_e7 > 900000000 ||
      adsb_settings.longitude_e7 < -1800000000 ||
      adsb_settings.longitude_e7 > 1800000000 ||
      (adsb_settings.radar_range_nm != 10 && adsb_settings.radar_range_nm != 25 &&
       adsb_settings.radar_range_nm != 50 && adsb_settings.radar_range_nm != 100)) {
    adsb_settings = {};
    adsb_settings.radar_range_nm = 25;
  }
  if (preferences.isKey("last_band")) {
    const auto stored_band = static_cast<RtlBand>(
        preferences.getUInt("last_band", static_cast<uint32_t>(RtlBand::fm)));
    if (stored_band == RtlBand::fm || stored_band == RtlBand::am ||
        stored_band == RtlBand::wx || stored_band == RtlBand::cb ||
        stored_band == RtlBand::lora || stored_band == RtlBand::browse ||
        stored_band == RtlBand::adsb || stored_band == RtlBand::p25) {
      rtl_ui_band = stored_band;
      rtl_requested_band.store(stored_band, std::memory_order_release);
      if (stored_band == RtlBand::p25) {
        rtl_ui_frequency_hz = p25_control_frequency_hz;
        rtl_requested_frequency_hz.store(p25_control_frequency_hz,
                                         std::memory_order_release);
      }
      Serial.printf("RTL_BAND_RESTORE band=%s\n", rtl_band_name(stored_band));
    }
  }
  uint8_t recent[orcsdr::dashboards::kRecentCapacity]{};
  const size_t recent_bytes = preferences.getBytesLength("dash_recent");
  if (recent_bytes > 0 && recent_bytes <= sizeof(recent))
    preferences.getBytes("dash_recent", recent, recent_bytes);
  orcsdr::dashboards::load_recent(recent, recent_bytes);
}

uint32_t append_journal(const char* kind, int16_t x = -1, int16_t y = -1) {
  size_t index;
  if (journal.count < kJournalCapacity) {
    index = (journal.head + journal.count) % kJournalCapacity;
    ++journal.count;
  } else {
    index = journal.head;
    journal.head = (journal.head + 1) % kJournalCapacity;
    ++journal.dropped_events;
  }
  JournalEntry& entry = journal.entries[index];
  entry.sequence = ++journal.next_sequence;
  strncpy(entry.kind, kind, sizeof(entry.kind) - 1);
  entry.kind[sizeof(entry.kind) - 1] = '\0';
  entry.x = x;
  entry.y = y;
  persist_journal();
  return entry.sequence;
}

bool hmac_matches(const char* value, const uint8_t* candidate) {
  uint8_t expected[32];
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (info == nullptr ||
      mbedtls_md_hmac(info, pairing_key, sizeof(pairing_key),
                      reinterpret_cast<const uint8_t*>(value), strlen(value), expected) != 0) {
    return false;
  }
  uint8_t difference = 0;
  for (size_t index = 0; index < sizeof(expected); ++index) {
    difference |= expected[index] ^ candidate[index];
  }
  return difference == 0;
}

void run_offline_workflow() {
  if (workflow.config_revision == 0 || workflow.runs >= workflow.max_runs) return;
  ++workflow.runs;
  persist_workflow();
  draw_session_state("Host offline - local status workflow active", TFT_ORANGE);
  append_journal("workflow_run");
}

void acknowledge_journal(uint32_t sequence) {
  while (journal.count > 0 && journal.entries[journal.head].sequence <= sequence) {
    journal.head = (journal.head + 1) % kJournalCapacity;
    --journal.count;
  }
  if (sequence > journal.acknowledged_sequence) {
    journal.acknowledged_sequence = sequence;
    persist_journal();
  }
}

void emit_pending_journal() {
  if (!authenticated) return;
  for (size_t offset = 0; offset < journal.count; ++offset) {
    const JournalEntry& entry = journal.entries[(journal.head + offset) % kJournalCapacity];
    Serial.printf(
        "{\"type\":\"event\",\"message_id\":\"m5tab5_journal_%lu\","
        "\"protocol_version\":{\"major\":1,\"minor\":0},"
        "\"payload\":{\"node_id\":\"%s\",\"kind\":\"local_journal\","
        "\"journal_sequence\":%lu,\"event_kind\":\"%s\",\"x\":%d,\"y\":%d}}\n",
        static_cast<unsigned long>(entry.sequence), node_id,
        static_cast<unsigned long>(entry.sequence), entry.kind, entry.x, entry.y);
  }
}

bool point_in_button(int32_t x, int32_t y) {
  return x >= kButtonX && x < kButtonX + kButtonWidth &&
         y >= kButtonY && y < kButtonY + kButtonHeight;
}

void queue_local_rtl_listen(RtlBand band, uint32_t frequency_hz,
                            bool persist_navigation) {
  if (!rtl_band_has_audio(band)) sync_rtl_audio_for_band(band);
  if (band == RtlBand::adsb) {
    if (!orcsdr::home::active()) orcsdr::adsb::enter(adsb_settings);
    frequency_hz = kAdsbDefaultHz;
    Serial.println("RTL_ADSB_CAPTURE live_rf=true ui_data=demo");
  }
#if RTL_USE_LEGACY_USB
  if (rtl_sdr_device == nullptr) return;
#else
  if (!g_rtl_device_ready.load(std::memory_order_acquire) || g_rtl == nullptr) return;
#endif
  frequency_hz = rtl_clamp_frequency(band, frequency_hz);
  if (band == RtlBand::p25) {
    p25_control_frequency_hz = frequency_hz;
    p25_follow_state.store(P25FollowState::control, std::memory_order_release);
    p25_voice_frequency_hz = 0;
  }
  if (rtl_ui_band == RtlBand::lora && band != RtlBand::lora &&
      g_iq_rec_active.exchange(false, std::memory_order_acq_rel)) {
    g_iq_rec_export_pending.store(true, std::memory_order_release);
  }
  if (band != rtl_ui_band) {
    rtl_filter_bandwidth_hz.store(rtl_filter_default_hz(band), std::memory_order_relaxed);
  }
  if (band == RtlBand::cb) {
    rtl_scope_span_hz.store(480000, std::memory_order_relaxed);
  }
  if (band == RtlBand::lora) {
    rtl_scope_span_hz.store(kRtlScopeSpanMaxHz, std::memory_order_relaxed);
  }
  if (band == RtlBand::p25) {
    rtl_scope_span_hz.store(kRtlScopeSpanMaxHz, std::memory_order_relaxed);
  }
  if (band == RtlBand::fm && persist_navigation) {
    persist_fm_frequency(frequency_hz);
  }
  if (persist_navigation) {
    preferences.putUInt("last_band", static_cast<uint32_t>(band));
    persist_dashboard_open(dashboard_for_band(band, frequency_hz));
  }
  rtl_requested_band.store(band, std::memory_order_release);
  rtl_requested_frequency_hz.store(frequency_hz, std::memory_order_release);
  rtl_hot_retune_hz.store(0, std::memory_order_release);
  rtl_ui_band = band;
  rtl_ui_frequency_hz = frequency_hz;
  rtl_continuous_requested.store(true, std::memory_order_release);
  const RtlCaptureState state = rtl_capture_state.load(std::memory_order_acquire);
  if (state == RtlCaptureState::running) {
    rtl_restart_requested.store(true, std::memory_order_release);
    rtl_stop_requested.store(true, std::memory_order_release);
  } else {
    rtl_restart_requested.store(false, std::memory_order_release);
    rtl_stop_requested.store(false, std::memory_order_release);
    rtl_capture_requested.store(true, std::memory_order_release);
  }
  bump_rtl_ui();
  if (persist_navigation)
    append_journal(band == RtlBand::am       ? "sdr_am"
                   : band == RtlBand::wx     ? "sdr_wx"
                   : band == RtlBand::cb     ? "sdr_cb"
                   : band == RtlBand::lora   ? "sdr_lora"
                   : band == RtlBand::browse ? "sdr_browse"
                   : band == RtlBand::adsb   ? "sdr_adsb"
                   : band == RtlBand::p25    ? "sdr_p25"
                                             : "sdr_fm");
}

void adjust_rtl_volume(int delta) {
  int volume = rtl_live_volume.load(std::memory_order_acquire);
  volume += delta;
  if (volume < kRtlVolumeMin) volume = kRtlVolumeMin;
  if (volume > kRtlVolumeMax) volume = kRtlVolumeMax;
  const uint8_t next = static_cast<uint8_t>(volume);
  rtl_live_volume.store(next, std::memory_order_release);
  rtl_requested_volume.store(next, std::memory_order_release);
  rtl_ui_volume = next;
  rtl_volume_changed.store(true, std::memory_order_release);
  apply_speaker_volume(next);
  bump_rtl_ui();
  schedule_rtl_audio_settings_persist();
}

bool point_in_scope(int32_t x, int32_t y) {
  if (rtl_ui_band == RtlBand::adsb) return false;
  // The FM module owns its complete touch surface.
  if (rtl_ui_band == RtlBand::fm) return false;
  // Spectrum + waterfall hit target for pan/flick (not the control rows).
  return x >= kSpectrumX && x < kSpectrumX + spectrum_draw_width() && y >= kSpectrumY &&
         y < kWaterfallY + kWaterfallHeight;
}

void tune_cb_channel(size_t channel) {
  channel %= std::size(kCbChannelsHz);
  const uint32_t frequency = kCbChannelsHz[channel];
  if (rtl_capture_state.load(std::memory_order_acquire) == RtlCaptureState::running) {
    request_hot_retune(frequency);
  } else {
    queue_local_rtl_listen(RtlBand::cb, frequency);
  }
  draw_cb_dashboard(false);
  draw_spectrum_axis();
}

bool handle_cb_touch(int32_t x, int32_t y) {
  if (rtl_ui_band != RtlBand::cb || x < kCbPanelX || x >= kCbPanelX + kCbPanelWidth ||
      y < kCbPanelY || y >= kCbPanelY + kCbPanelHeight) return false;
  const int knob_x = kCbPanelX + 192;
  const int knob_y = kCbPanelY + 311;
  const int dx = x - knob_x;
  const int dy = y - knob_y;
  if (dx * dx + dy * dy <= 78 * 78) {
    constexpr float kPi = 3.14159265358979323846f;
    const float turn = (atan2f(static_cast<float>(dy), static_cast<float>(dx)) + kPi) /
                       (2.0f * kPi);
    tune_cb_channel(min(static_cast<size_t>(turn * 40.0f), size_t{39}));
    return true;
  }
  if (y >= kCbPanelY + 380 && y <= kCbPanelY + 442) {
    const int control = constrain((x - (kCbPanelX + 23)) / 56, 0, 5);
    if (control == 0 || control == 1) {
      const size_t current = cb_channel_index(rtl_ui_frequency_hz);
      tune_cb_channel((current + (control == 0 ? 39 : 1)) % 40);
    } else if (control == 2) {
      CbMode mode = cb_mode.load(std::memory_order_relaxed);
      mode = mode == CbMode::am ? CbMode::usb : mode == CbMode::usb ? CbMode::lsb
                                                                        : CbMode::am;
      cb_mode.store(mode, std::memory_order_relaxed);
      cb_clarifier_hz.store(0, std::memory_order_relaxed);
      rtl_filter_bandwidth_hz.store(mode == CbMode::am ? 10000 : 3000,
                                    std::memory_order_relaxed);
      rtl_audio_reset_demod_filters();
      Serial.printf("RTL_CB_MODE mode=%s\n", mode == CbMode::usb ? "USB"
                                              : mode == CbMode::lsb ? "LSB" : "AM");
    } else if (control == 3) {
      int clarifier = cb_clarifier_hz.load(std::memory_order_relaxed) + 500;
      if (clarifier > 1500) clarifier = -1500;
      cb_clarifier_hz.store(clarifier, std::memory_order_relaxed);
      rtl_audio.ssb_cos = 1.0f;
      rtl_audio.ssb_sin = 0.0f;
    } else if (control == 4 || control == 5) {
      int threshold = cb_squelch_dbfs.load(std::memory_order_relaxed);
      threshold = constrain(threshold + (control == 4 ? -5 : 5), -90, -35);
      cb_squelch_dbfs.store(threshold, std::memory_order_relaxed);
      Serial.printf("RTL_CB_SQUELCH dbfs=%d\n", threshold);
    } else {
      return true;
    }
    draw_cb_dashboard(false);
    return true;
  }
  return true;
}

bool handle_lora_touch(int32_t x, int32_t y) {
  if (rtl_ui_band != RtlBand::lora) return false;
  if (x >= kSpectrumX + 925 && x < kSpectrumX + kSpectrumWidth && y >= 578 &&
      y < 636) {
    set_lora_sd_logging(
        !lora_log_requested.load(std::memory_order_relaxed));
    draw_lora_dashboard(false);
    return true;
  }
  return false;
}

void request_hot_retune(uint32_t frequency_hz) {
  if (rtl_ui_band == RtlBand::wx || rtl_ui_band == RtlBand::adsb) return;
  frequency_hz = rtl_clamp_frequency(rtl_ui_band, frequency_hz);
  if (frequency_hz == 0) return;
  /* UI: 1 kHz display quantize. */
  const uint32_t ui_hz = rtl_ui_band == RtlBand::p25
                             ? frequency_hz
                             : (frequency_hz / 1000u) * 1000u;
  /* LO: 5 kHz — avoids thrashing bulk/EP0. */
  const uint32_t lo_hz = rtl_ui_band == RtlBand::p25
                             ? frequency_hz
                             : (frequency_hz / kRtlHotRetuneQuantHz) * kRtlHotRetuneQuantHz;
  const bool ui_changed = (ui_hz != rtl_ui_frequency_hz);
  rtl_ui_frequency_hz = ui_hz;
  rtl_requested_frequency_hz.store(ui_hz, std::memory_order_release);
  if (lo_hz != 0) {
    rtl_hot_retune_hz.store(lo_hz, std::memory_order_release);
  }
  /*
   * Do not full-repaint header on every drag sample — that was starving audio
   * on Core 1. Light frequency-only redraw instead.
   */
  if (ui_changed) {
    if (orcsdr::home::active()) {
      orcsdr::home::update(home_dashboard_snapshot());
      return;
    }
    if (rtl_ui_band == RtlBand::fm && orcsdr::fm::active()) {
      orcsdr::fm::update(fm_dashboard_snapshot());
      return;
    }
    if (rtl_ui_band == RtlBand::p25 && orcsdr::p25::active()) {
      orcsdr::p25::update(p25_dashboard_snapshot());
      return;
    }
    char frequency_text[24];
    format_frequency(frequency_text, sizeof(frequency_text), ui_hz);
    char label[64];
    snprintf(label, sizeof(label), "%s  %s", rtl_band_name(rtl_ui_band), frequency_text);
    /* Frequency row only (below SIG strip) — do not touch the meter. */
    M5.Display.fillRect(180, 34, 760, 36, TFT_BLACK);
    M5.Display.setTextDatum(middle_center);
    M5.Display.setTextSize(4);
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5.Display.drawString(label, 560, 48);
    draw_rf_band_guide(ui_hz);
  }
}

// Capture runs on the high-priority USB task and previously starved Arduino
// loop() touch polling. Service edges here so buttons work during waterfall.
// Only this path may call M5.update() while rtl_ui_active — concurrent update
// from loop() was silencing the ES8388 speaker path after 0.8.26.
void poll_sdr_touch_from_stream() {
  if (ui_documentation_mode) return;
  if (orcsdr::home::active()) {
    static uint32_t home_last_poll_ms = 0;
    const uint32_t now = millis();
    if (now - home_last_poll_ms < 33) return;
    home_last_poll_ms = now;
    M5.update();
    const auto touch = M5.Touch.getDetail(0);
    handle_home_action(orcsdr::home::handle_touch(touch.x, touch.y,
                                                   touch.isPressed()));
    return;
  }
  if (rtl_ui_band == RtlBand::adsb && orcsdr::adsb::active()) return;
  if (orcsdr::settings::active()) {
    static uint32_t settings_last_poll_ms = 0;
    static uint32_t settings_last_update_ms = 0;
    const uint32_t now = millis();
    if (now - settings_last_poll_ms < 33) return;
    settings_last_poll_ms = now;
    M5.update();
    if (now - settings_last_update_ms >= 500) {
      settings_last_update_ms = now;
      orcsdr::settings::update(global_settings_state());
    }
    const auto touch = M5.Touch.getDetail(0);
    const bool pressed = touch.isPressed() || touch.wasPressed();
    if (pressed && !was_pressed) handle_global_settings_touch(touch.x, touch.y);
    was_pressed = pressed;
    return;
  }
  if (rtl_ui_band == RtlBand::p25 && orcsdr::p25::active()) {
    static uint32_t p25_last_poll_ms = 0;
    const uint32_t now = millis();
    if (now - p25_last_poll_ms < 33) return;
    p25_last_poll_ms = now;
    M5.update();
    const auto touch = M5.Touch.getDetail(0);
    const bool pressed = touch.isPressed() || touch.wasPressed();
    if (pressed && !was_pressed) handle_sdr_touch(touch.x, touch.y);
    was_pressed = pressed;
    return;
  }
  static uint32_t last_touch_poll_ms = 0;
  static bool flick_thresh_set = false;
  static bool scope_dragging = false;
  static bool filter_edge_dragging = false;
  static bool pinch_active = false;
  static float pinch_anchor_distance = 0;
  static uint32_t pinch_anchor_value = 0;
  static int drag_anchor_x = 0;
  static uint32_t drag_anchor_hz = 0;
  static uint32_t last_queue_ms = 0;
  static uint32_t last_queued_hz = 0;
  const uint32_t now = millis();
  if (now - last_touch_poll_ms < 33) return;
  last_touch_poll_ms = now;
  if (!flick_thresh_set) {
    M5.Touch.setFlickThresh(24);
    flick_thresh_set = true;
  }
  M5.update();
  const uint8_t touch_count = M5.Touch.getCount();
  const auto touch = M5.Touch.getDetail(0);
  const bool pressed = touch.isPressed() || touch.wasPressed();
  const int scope_width = spectrum_draw_width();

  if (rtl_nav_open) {
    if (pressed && !was_pressed) {
      if (!handle_nav_touch(touch.x, touch.y)) handle_tool_tab_touch(touch.x, touch.y);
    }
    was_pressed = pressed;
    return;
  }

  if (touch_count >= 2) {
    const auto second = M5.Touch.getDetail(1);
    if (point_in_scope(touch.x, touch.y) && point_in_scope(second.x, second.y)) {
      const float dx = static_cast<float>(touch.x - second.x);
      const float dy = static_cast<float>(touch.y - second.y);
      const float distance = sqrtf(dx * dx + dy * dy);
      if (!pinch_active) {
        pinch_active = true;
        pinch_anchor_distance = max(distance, 16.0f);
        pinch_anchor_value = rtl_pinch_mode == SdrPinchMode::Span
                                 ? rtl_scope_span_hz.load(std::memory_order_relaxed)
                                 : rtl_filter_bandwidth_hz.load(std::memory_order_relaxed);
      } else if (distance >= 16.0f) {
        uint32_t next;
        if (rtl_pinch_mode == SdrPinchMode::Span) {
          next = static_cast<uint32_t>(pinch_anchor_value * pinch_anchor_distance / distance);
          next = constrain((next / 5000u) * 5000u, kRtlScopeSpanMinHz,
                           kRtlScopeSpanMaxHz);
          if (next != rtl_scope_span_hz.exchange(next, std::memory_order_relaxed)) {
            redraw_spectrum_panel();
            draw_spectrum_axis();
          }
        } else {
          next = static_cast<uint32_t>(pinch_anchor_value * distance / pinch_anchor_distance);
          next = rtl_clamp_filter_hz(rtl_ui_band, next);
          if (next != rtl_filter_bandwidth_hz.exchange(next, std::memory_order_relaxed)) {
            redraw_spectrum_panel();
          }
        }
      }
      scope_dragging = false;
      was_pressed = true;
      return;
    }
  }
  if (pinch_active) {
    pinch_active = false;
    scope_dragging = false;
    was_pressed = pressed;
    return;
  }

  if (pressed && !was_pressed && rtl_ui_band == RtlBand::cb &&
      point_in_scope(touch.x, touch.y)) {
    const uint32_t span = rtl_scope_span_hz.load(std::memory_order_relaxed);
    const int64_t offset = (static_cast<int64_t>(touch.x - kSpectrumX) * span) /
                               scope_width -
                           static_cast<int64_t>(span / 2);
    const int64_t selected = static_cast<int64_t>(rtl_ui_frequency_hz) + offset;
    tune_cb_channel(cb_channel_index(
        static_cast<uint32_t>(selected < 0 ? 0 : selected)));
    was_pressed = true;
    return;
  }

  if (pressed && !was_pressed && touch.y >= kSpectrumY &&
      touch.y < kSpectrumY + kSpectrumHeight) {
    const uint32_t span = rtl_scope_span_hz.load(std::memory_order_relaxed);
    const uint32_t bandwidth = rtl_filter_bandwidth_hz.load(std::memory_order_relaxed);
    const int half_width = constrain(
        static_cast<int>((static_cast<uint64_t>(bandwidth) * scope_width) /
                         (2u * span)),
        3, scope_width / 2 - 2);
    const int center = kSpectrumX + scope_width / 2;
    filter_edge_dragging = abs(touch.x - (center - half_width)) <= 18 ||
                           abs(touch.x - (center + half_width)) <= 18;
  }
  if (filter_edge_dragging && pressed) {
    const int center = kSpectrumX + scope_width / 2;
    const uint32_t span = rtl_scope_span_hz.load(std::memory_order_relaxed);
    uint32_t bandwidth = static_cast<uint32_t>(
        (2ull * abs(touch.x - center) * span) / scope_width);
    bandwidth = rtl_clamp_filter_hz(rtl_ui_band, bandwidth);
    if (bandwidth != rtl_filter_bandwidth_hz.exchange(bandwidth,
                                                       std::memory_order_relaxed)) {
      redraw_spectrum_panel();
    }
    was_pressed = pressed;
    return;
  }
  if (filter_edge_dragging && !pressed) {
    filter_edge_dragging = false;
    was_pressed = false;
    return;
  }

  // Phone-style infinite scroll: header tracks finger; PLL only every ~120 ms.
  if (pressed && !was_pressed && point_in_scope(touch.x, touch.y) &&
      rtl_ui_band != RtlBand::wx) {
    scope_dragging = true;
    drag_anchor_x = touch.x;
    drag_anchor_hz = rtl_ui_frequency_hz;
    last_queued_hz = rtl_ui_frequency_hz;
  }

  if (scope_dragging && pressed) {
    const int dx = touch.x - drag_anchor_x;
    const double hz_per_px = rtl_scope_span_hz.load(std::memory_order_relaxed) /
                             static_cast<double>(scope_width);
    int64_t next = static_cast<int64_t>(drag_anchor_hz) -
                   static_cast<int64_t>(llround(static_cast<double>(dx) * hz_per_px));
    if (next < 0) next = 0;
    const uint32_t tuned =
        rtl_clamp_frequency(rtl_ui_band, static_cast<uint32_t>(next));
    rtl_ui_frequency_hz = tuned;
    bump_rtl_ui();
    /* UI can update often; LO apply is rate-limited in the stream loop. */
    if (tuned != last_queued_hz && now - last_queue_ms >= 80) {
      request_hot_retune(tuned);
      last_queued_hz = tuned;
      last_queue_ms = now;
    }
    was_pressed = pressed;
    return;
  }

  if (scope_dragging && !pressed) {
    int dx = touch.x - drag_anchor_x;
    if (touch.wasFlicked() || touch.wasDragged()) {
      if (abs(touch.distanceX()) > abs(dx)) dx = touch.distanceX();
    }
    const double hz_per_px = rtl_scope_span_hz.load(std::memory_order_relaxed) /
                             static_cast<double>(scope_width);
    int64_t next;
    if (abs(dx) < 12) {
      next = static_cast<int64_t>(drag_anchor_hz) +
             static_cast<int64_t>(llround(
                 static_cast<double>(drag_anchor_x - (kSpectrumX + scope_width / 2)) *
                 hz_per_px));
    } else {
      next = static_cast<int64_t>(drag_anchor_hz) -
             static_cast<int64_t>(llround(static_cast<double>(dx) * hz_per_px));
    }
    if (next < 0) next = 0;
    request_hot_retune(rtl_clamp_frequency(rtl_ui_band, static_cast<uint32_t>(next)));
    scope_dragging = false;
    was_pressed = false;
    return;
  }

  if (pressed && !was_pressed && !point_in_scope(touch.x, touch.y)) {
    handle_sdr_touch(touch.x, touch.y);
  }
  was_pressed = pressed;
}

void handle_sdr_touch(int32_t x, int32_t y) {
  if (orcsdr::settings::active()) {
    handle_global_settings_touch(x, y);
    return;
  }
  if (orcsdr::audio_header::home_hit(x, y)) {
    show_home();
    return;
  }
  if (x >= 1211 && x < 1269 && y >= 8 && y < 66) {
    open_global_settings(rtl_ui_band == RtlBand::adsb
                             ? orcsdr::settings::Section::location_adsb
                             : rtl_ui_band == RtlBand::p25
                                   ? orcsdr::settings::Section::radio_defaults
                             : orcsdr::settings::Section::connectivity);
    return;
  }
  if (rtl_ui_band == RtlBand::adsb) {
    const orcsdr::adsb::Action action = orcsdr::adsb::handle_touch(x, y);
    if (action == orcsdr::adsb::Action::settings_changed) {
      adsb_settings = orcsdr::adsb::settings();
      adsb_settings_persist_pending.store(true, std::memory_order_release);
    } else if (action == orcsdr::adsb::Action::exit) {
      show_home();
    }
    return;
  }
  if (rtl_ui_band == RtlBand::fm && orcsdr::fm::active()) {
    handle_fm_dashboard_action(orcsdr::fm::handle_touch(x, y));
    return;
  }
  if (rtl_ui_band == RtlBand::p25 && orcsdr::p25::active()) {
    handle_p25_dashboard_action(orcsdr::p25::handle_touch(x, y));
    return;
  }
  if (handle_tool_tab_touch(x, y)) return;
  if (handle_cb_touch(x, y)) return;
  if (handle_lora_touch(x, y)) return;
  static constexpr int kBandWidths[] = {110, 110, 110, 120, 140, 160, 170, 200};
  static constexpr int kTuneWidths[] = {170, 170, 220, 150, 150, 220};
  if (rtl_ui_band == RtlBand::lora) {
    const int index = sdr_button_at(kSdrTuneY, x, y, kTuneWidths,
                                    std::size(kTuneWidths));
    if (index < 0) return;
    if (index == 0 || index == 1) {
      const uint32_t next = rtl_step_frequency(RtlBand::lora, rtl_ui_frequency_hz,
                                               index == 0 ? -1 : 1);
      if (rtl_capture_state.load(std::memory_order_acquire) == RtlCaptureState::running) {
        request_hot_retune(next);
      } else {
        queue_local_rtl_listen(RtlBand::lora, next);
      }
    } else if (index == 2) {
      uint32_t bandwidth = lora_bandwidth_hz.load(std::memory_order_relaxed);
      bandwidth = bandwidth == 125000 ? 250000 : bandwidth == 250000 ? 500000 : 125000;
      lora_bandwidth_hz.store(bandwidth, std::memory_order_relaxed);
      rtl_filter_bandwidth_hz.store(bandwidth, std::memory_order_relaxed);
      reset_spectrum_renderer();
    } else if (index == 3) {
      const uint8_t sf = lora_sf.load(std::memory_order_relaxed);
      lora_sf.store(sf >= 12 ? 7 : sf + 1, std::memory_order_relaxed);
    } else if (index == 4) {
      if (g_iq_rec_active.load(std::memory_order_acquire)) (void)iq_rec_stop_and_export();
      else (void)iq_rec_start();
    } else {
      const RtlCaptureState state = rtl_capture_state.load(std::memory_order_acquire);
      if (state == RtlCaptureState::running) {
        rtl_restart_requested.store(false, std::memory_order_release);
        rtl_stop_requested.store(true, std::memory_order_release);
        Serial.println("RTL_UI_STOP");
      } else {
        queue_local_rtl_listen(RtlBand::lora, rtl_ui_frequency_hz);
      }
    }
    draw_sdr_controls(rtl_ui_band,
                      rtl_capture_state.load(std::memory_order_acquire) ==
                          RtlCaptureState::running);
    return;
  }
  const int band_index = sdr_button_at(kSdrBandY, x, y, kBandWidths, std::size(kBandWidths));
  if (band_index >= 0) {
    if (band_index == 0) {
      queue_local_rtl_listen(RtlBand::fm, rtl_ui_band == RtlBand::fm
                                               ? rtl_ui_frequency_hz
                                               : rtl_saved_fm_hz);
    } else if (band_index == 1) {
      queue_local_rtl_listen(RtlBand::am, rtl_ui_band == RtlBand::am
                                               ? rtl_ui_frequency_hz
                                               : kRtlAmDefaultHz);
    } else if (band_index == 2) {
      queue_local_rtl_listen(RtlBand::wx, kRtlWxHz);
    } else if (band_index == 3) {
      queue_local_rtl_listen(RtlBand::cb, rtl_ui_band == RtlBand::cb
                                              ? rtl_ui_frequency_hz
                                              : kCbDefaultHz);
    } else if (band_index == 4) {
      queue_local_rtl_listen(RtlBand::lora, rtl_ui_band == RtlBand::lora
                                                ? rtl_ui_frequency_hz
                                                : kLoraDefaultHz);
    } else if (band_index == 5) {
      queue_local_rtl_listen(RtlBand::browse,
                             rtl_ui_band == RtlBand::browse
                                 ? rtl_ui_frequency_hz
                                 : constrain(rtl_ui_frequency_hz,
                                             kRtlBrowseMinHz, kRtlBrowseMaxHz));
    } else if (band_index == 6) {
      /* REC toggle — Capture tool records post-demod PCM for offline analysis. */
      if (g_audio_rec_active.load(std::memory_order_acquire)) {
        (void)audio_rec_stop_and_export();
      } else {
        if (orc_tool_current() != OrcTool::Capture) set_orc_tool(OrcTool::Capture);
        (void)audio_rec_start();
      }
      const bool running =
          rtl_capture_state.load(std::memory_order_acquire) == RtlCaptureState::running;
      draw_sdr_controls(rtl_ui_band, running);
      if (orc_tool_current() == OrcTool::Capture) draw_capture_tool_panel();
    } else if (band_index == 7) {
      const RtlCaptureState state = rtl_capture_state.load(std::memory_order_acquire);
      if (state == RtlCaptureState::running) {
        rtl_restart_requested.store(false, std::memory_order_release);
        rtl_stop_requested.store(true, std::memory_order_release);
        Serial.println("RTL_UI_STOP");
        /* Do not append_journal here — NVS can stall the touch/USB path. */
      } else {
        queue_local_rtl_listen(rtl_ui_band, rtl_ui_frequency_hz);
      }
    }
    return;
  }

  const int tune_index = sdr_button_at(kSdrTuneY, x, y, kTuneWidths, std::size(kTuneWidths));
  if (tune_index < 0) return;
  if (tune_index == 0 || tune_index == 1) {
    if (rtl_ui_band == RtlBand::wx) return;
    const uint32_t next = rtl_step_frequency(rtl_ui_band, rtl_ui_frequency_hz,
                                             tune_index == 0 ? -1 : 1);
    const RtlCaptureState st = rtl_capture_state.load(std::memory_order_acquire);
    if (st == RtlCaptureState::running) {
      /* Hot retune in-stream — do not tear down USB for FREQ +/- */
      request_hot_retune(next);
    } else {
      queue_local_rtl_listen(rtl_ui_band, next);
    }
  } else if (tune_index == 2) {
    const bool next = !rtl_audio_user_enabled.load(std::memory_order_acquire);
    set_rtl_audio_user_enabled(next);
    Serial.printf("RTL_SOUND %s\n", next ? "on" : "off");
    bump_rtl_ui();
    const bool running =
        rtl_capture_state.load(std::memory_order_acquire) == RtlCaptureState::running;
    draw_sdr_controls(rtl_ui_band, running);
  } else if (tune_index == 3) {
    adjust_rtl_volume(-static_cast<int>(kRtlVolumeStep));
  } else if (tune_index == 4) {
    adjust_rtl_volume(static_cast<int>(kRtlVolumeStep));
  } else if (tune_index == 5) {
    const bool next =
        !rtl_graphics_enabled.load(std::memory_order_acquire);
    rtl_graphics_enabled.store(next, std::memory_order_release);
    Serial.printf("RTL_GRAPHICS %s\n", next ? "on" : "off");
    if (!next) {
      paint_graphics_paused_banner();
    } else {
      reset_spectrum_renderer();
      /* Next stream loop tick will resume waterfall/scope. */
    }
    const bool running =
        rtl_capture_state.load(std::memory_order_acquire) == RtlCaptureState::running;
    draw_sdr_controls(rtl_ui_band, running);
  }
}

void emit_touch(int32_t x, int32_t y) {
  char status[64];
  const uint32_t sequence = append_journal("operator_touch", x, y);
  snprintf(status, sizeof(status), "Touch verified (%lu) at %ld,%ld",
           static_cast<unsigned long>(sequence), static_cast<long>(x),
           static_cast<long>(y));
  draw_touch_state(status, TFT_GREEN);
  emit_pending_journal();
}

void emit_identity() {
  const String wifi_ip = wifi_connected ? WiFi.localIP().toString() : "0.0.0.0";
  const int32_t battery_level = M5.Power.getBatteryLevel();
  const int16_t battery_mv = M5.Power.getBatteryVoltage();
  const int32_t battery_ma = M5.Power.getBatteryCurrent();
  const int16_t vbus_mv = M5.Power.getVBUSVoltage();
  Serial.printf(
      "{\"type\":\"hello\",\"message_id\":\"m5tab5_hello_01\","
      "\"protocol_version\":{\"major\":1,\"minor\":0},"
      "\"payload\":{\"adapter\":{\"id\":\"m5tab5.serial\",\"version\":\"0.8.31\"},"
      "\"features\":[\"telemetry\",\"display\",\"touch\",\"authenticated_session\",\"journal\",\"network_inventory\",\"wifi_provisioning\",\"power_telemetry\",\"usb_host\",\"rtl_sdr_probe\",\"rtl_sdr_radio_ui\",\"rtl_sdr_scope_scroll\"],"
      "\"capability_namespaces\":[\"m5tab5\"],\"config_schema_version\":1}}\n");
  Serial.printf(
      "{\"type\":\"node_snapshot\",\"message_id\":\"m5tab5_snapshot_%lu\","
      "\"protocol_version\":{\"major\":1,\"minor\":0},"
      "\"payload\":{\"node_id\":\"%s\",\"connection_state\":\"online\","
      "\"hardware\":{\"chip\":\"%s\",\"revision\":%u,\"cores\":%u,"
      "\"flash_bytes\":%u,\"psram_bytes\":%u,\"free_heap_bytes\":%u,"
      "\"display\":{\"width\":%d,\"height\":%d,\"touch_ready\":%s}},"
      "\"network\":{\"transport\":\"esp_hosted_sdio\",\"station_ready\":%s,"
      "\"configured\":%s,\"scan_result\":%d,\"connected\":%s,\"ip\":\"%s\"},"
      "\"power\":{\"source\":\"m5unified\",\"pmic_detected\":%s,\"pmic_type\":%d,"
      "\"battery_level\":%ld,\"battery_mv\":%d,"
      "\"battery_ma\":%ld,\"vbus_mv\":%d,\"charging\":\"%s\"},"
      "\"journal\":{\"latest_sequence\":%lu,\"acknowledged_sequence\":%lu,\"pending\":%u,\"dropped\":%lu},"
      "\"offline_workflow\":{\"id\":\"status_on_disconnect\",\"config_revision\":%lu,"
      "\"runs\":%u,\"max_runs\":%u},"
      "\"capabilities\":[\"m5tab5.device.info\",\"m5tab5.health.read\","
      "\"m5tab5.display.status\",\"m5tab5.input.touch\",\"m5tab5.event.journal.sync\","
      "\"m5tab5.network.inventory\",\"m5tab5.network.configure\","
      "\"m5tab5.power.read\"]}}\n",
      static_cast<unsigned long>(millis()), node_id, ESP.getChipModel(),
      ESP.getChipRevision(), ESP.getChipCores(), ESP.getFlashChipSize(),
      ESP.getPsramSize(), ESP.getFreeHeap(), M5.Display.width(), M5.Display.height(),
      M5.Touch.isEnabled() ? "true" : "false",
      wifi_station_ready ? "true" : "false", wifi_configured ? "true" : "false",
      wifi_network_count, wifi_connected ? "true" : "false", wifi_ip.c_str(),
      M5.Power.getType() == m5::Power_Class::pmic_unknown ? "false" : "true",
      static_cast<int>(M5.Power.getType()), static_cast<long>(battery_level), battery_mv,
      static_cast<long>(battery_ma), vbus_mv, charging_state(),
      static_cast<unsigned long>(journal.next_sequence),
      static_cast<unsigned long>(journal.acknowledged_sequence), journal.count,
      static_cast<unsigned long>(journal.dropped_events),
      static_cast<unsigned long>(workflow.config_revision), workflow.runs, workflow.max_runs);
}

void set_online() {
  if (!authenticated) return;
  draw_session_state("Authenticated host online", TFT_GREEN);
  append_journal("session_online");
  emit_identity();
  emit_pending_journal();
}

struct UiDocScreen {
  const char* id;
  const char* modes;
};

constexpr UiDocScreen kUiDocScreens[] = {
    {"home", "demo"},
    {"nav", "demo"},
    {"settings.connectivity", "demo"},
    {"settings.location-adsb", "demo"},
    {"settings.data-maps", "demo"},
    {"settings.display-audio", "demo"},
    {"settings.radio-defaults", "demo"},
    {"settings.storage", "demo"},
    {"settings.companion", "demo"},
    {"settings.system", "demo"},
    {"fm.listen", "live,demo"},
    {"fm.spectrum", "live,demo"},
    {"fm.station-rds", "live,demo"},
    {"fm.rf-health", "live,demo"},
    {"fm.settings", "demo"},
    {"p25.monitor", "live,demo"},
    {"p25.spectrum", "live,demo"},
    {"p25.talkgroups", "live,demo"},
    {"p25.program", "live,demo"},
    {"p25.rf-health", "live,demo"},
    {"adsb.radar", "live,demo"},
    {"adsb.list", "live,demo"},
    {"adsb.target", "live,demo"},
    {"adsb.stats", "live,demo"},
    {"adsb.settings", "demo"},
    {"lora.live", "live,demo"},
    {"lora.packets", "live,demo"},
    {"lora.map", "live,demo"},
    {"am.radio", "live,demo"},
    {"am.scope", "live,demo"},
    {"am.capture", "live,demo"},
    {"wx.radio", "live,demo"},
    {"wx.scope", "live,demo"},
    {"wx.capture", "live,demo"},
    {"cb.radio", "live,demo"},
    {"cb.scope", "live,demo"},
    {"cb.capture", "live,demo"},
    {"browse.radio", "live,demo"},
    {"browse.scope", "live,demo"},
    {"browse.capture", "live,demo"},
    {"overlay.volume", "demo"},
    {"overlay.frequency-keypad", "demo"},
    {"overlay.wifi-scanning", "demo"},
    {"overlay.masked-keyboard", "demo"},
};

struct UiDocState {
  bool active = false;
  bool was_receiving = false;
  bool was_ui_active = false;
  bool was_settings_active = false;
  bool was_home_active = false;
  bool graphics_enabled = true;
  bool nav_open = false;
  RtlBand band = RtlBand::fm;
  uint32_t frequency_hz = kRtlFmDefaultHz;
  OrcTool tool = OrcTool::Radio;
  orcsdr::fm::View fm_view = orcsdr::fm::View::listen;
  orcsdr::p25::View p25_view = orcsdr::p25::View::monitor;
  uint8_t adsb_view = 0;
  orcsdr::settings::Section settings_section = orcsdr::settings::Section::connectivity;
  LoraView lora = LoraView::Live;
  LoraDisplayPacket lora_packets[kLoraDisplayPacketCount]{};
  LoraNodePosition lora_positions[kLoraNodePositionCount]{};
  uint32_t lora_message_count = 0;
  float lora_noise = -80.0f;
  float lora_trigger = -71.0f;
  float signal_dbfs = -80.0f;
  CbMode cb_mode_value = CbMode::am;
  int32_t cb_clarifier = 0;
  int32_t cb_squelch = -55;
  bool cb_squelch_was_open = false;
  char current_screen[40]{};
  bool current_demo = true;
};

UiDocState ui_doc;

bool ui_doc_screen_exists(const char* id, const char* mode = nullptr) {
  for (const auto& screen : kUiDocScreens) {
    if (strcmp(screen.id, id) != 0) continue;
    return mode == nullptr || strstr(screen.modes, mode) != nullptr;
  }
  return false;
}

size_t ui_doc_prefix_count(const char* prefix) {
  size_t count = 0;
  const size_t length = strlen(prefix);
  for (const auto& screen : kUiDocScreens)
    if (strncmp(screen.id, prefix, length) == 0) ++count;
  return count;
}

bool ui_doc_self_check() {
  if (static_cast<uint8_t>(orcsdr::fm::View::count) != 5 ||
      static_cast<uint8_t>(orcsdr::p25::View::count) != 5 ||
      static_cast<uint8_t>(orcsdr::settings::Section::count) != 8 ||
      ui_doc_prefix_count("fm.") != static_cast<uint8_t>(orcsdr::fm::View::count) ||
      ui_doc_prefix_count("p25.") != static_cast<uint8_t>(orcsdr::p25::View::count) ||
      ui_doc_prefix_count("settings.") !=
          static_cast<uint8_t>(orcsdr::settings::Section::count) ||
      ui_doc_prefix_count("adsb.") != orcsdr::adsb::kDocumentationViewCount ||
      ui_doc_prefix_count("lora.") != static_cast<uint8_t>(LoraView::Count) ||
      ui_doc_prefix_count("am.") != static_cast<uint8_t>(OrcTool::Count) ||
      ui_doc_prefix_count("wx.") != static_cast<uint8_t>(OrcTool::Count) ||
      ui_doc_prefix_count("cb.") != static_cast<uint8_t>(OrcTool::Count) ||
      ui_doc_prefix_count("browse.") != static_cast<uint8_t>(OrcTool::Count) ||
      !orcsdr::ui_capture::valid_slug("fm-listen") ||
      orcsdr::ui_capture::valid_slug("../private")) return false;
  for (size_t i = 0; i < std::size(kUiDocScreens); ++i) {
    if (!kUiDocScreens[i].id[0] || !kUiDocScreens[i].modes[0]) return false;
    for (size_t j = i + 1; j < std::size(kUiDocScreens); ++j)
      if (strcmp(kUiDocScreens[i].id, kUiDocScreens[j].id) == 0) return false;
  }
  return ui_doc_screen_exists("settings.system") &&
         ui_doc_screen_exists("fm.settings") &&
         ui_doc_screen_exists("p25.rf-health") &&
         ui_doc_screen_exists("adsb.settings") &&
         ui_doc_screen_exists("lora.map") &&
         ui_doc_screen_exists("browse.capture");
}

void ui_doc_leave_surfaces() {
  orcsdr::home::leave();
  orcsdr::settings::leave();
  orcsdr::fm::leave();
  orcsdr::p25::leave();
  orcsdr::adsb::leave();
  M5.Display.clearScrollRect();
}

void ui_doc_badge() {
  M5.Display.fillRoundRect(572, 8, 136, 42, 10, TFT_MAROON);
  M5.Display.drawRoundRect(572, 8, 136, 42, 10, TFT_YELLOW);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_WHITE, TFT_MAROON);
  M5.Display.drawString("DEMO", 640, 29);
}

orcsdr::fm::Snapshot ui_doc_fm_snapshot(bool demo) {
  if (!demo) return fm_dashboard_snapshot();
  orcsdr::fm::Snapshot snapshot{};
  snapshot.frequency_hz = 101700000;
  snapshot.step_hz = 100000;
  snapshot.filter_bandwidth_hz = 180000;
  snapshot.span_hz = 2000000;
  snapshot.effective_sps = 959800;
  snapshot.target_sps = 960000;
  snapshot.dsp_percent = 38;
  snapshot.audio_ring_pressure_percent = 42;
  snapshot.battery_percent = 76;
  snapshot.relative_dbfs = -31.5f;
  snapshot.left_dbfs = -8.0f;
  snapshot.right_dbfs = -8.7f;
  snapshot.running = snapshot.driver_ready = snapshot.stereo = true;
  snapshot.rds_carrier = snapshot.rds_locked = snapshot.wifi_connected = true;
  snapshot.sound_enabled = snapshot.graphics_enabled = true;
  snapshot.volume = 128;
  snapshot.preset_index = 2;
  snapshot.preset_count = 10;
  strlcpy(snapshot.program_service, "KEXP-FM", sizeof(snapshot.program_service));
  strlcpy(snapshot.radio_text, "Where the Music Matters", sizeof(snapshot.radio_text));
  strlcpy(snapshot.pi_code, "259F", sizeof(snapshot.pi_code));
  strlcpy(snapshot.program_type, "Music", sizeof(snapshot.program_type));
  return snapshot;
}

orcsdr::p25::Snapshot ui_doc_p25_snapshot(bool demo) {
  if (!demo) return p25_dashboard_snapshot();
  orcsdr::p25::Snapshot snapshot{};
  snapshot.frequency_hz = 453487500;
  snapshot.span_hz = snapshot.target_sps = 960000;
  snapshot.effective_sps = 959700;
  snapshot.dsp_percent = 44;
  snapshot.audio_ring_pressure_percent = 35;
  snapshot.battery_percent = 76;
  snapshot.relative_dbfs = -37.0f;
  snapshot.running = snapshot.driver_ready = snapshot.wifi_connected = true;
  snapshot.sound_enabled = snapshot.survey_active = snapshot.auto_follow = true;
  snapshot.encryption_skip = snapshot.following_voice = true;
  snapshot.candidate_count = 4;
  snapshot.candidate_index = 1;
  snapshot.volume = 128;
  snapshot.candidate_levels[0] = -53.0f;
  snapshot.candidate_levels[1] = -37.0f;
  snapshot.candidate_levels[2] = -61.0f;
  snapshot.candidate_levels[3] = -58.0f;
  snapshot.decoded.frame_sync = snapshot.decoded.identity_valid = true;
  snapshot.decoded.nac = 0x3A5;
  snapshot.decoded.wacn = 0xBEE00;
  snapshot.decoded.system_id = 0x2A7;
  snapshot.decoded.rfss = 1;
  snapshot.decoded.site = 1;
  snapshot.decoded.sync_words = 1842;
  snapshot.decoded.nid_good = 1801;
  snapshot.decoded.tsbk_good = 932;
  snapshot.decoded.estimated_ber_percent = 1.2f;
  snapshot.decoded.current_grant = {true, false, false, false, 38130, 1204521,
                                    453487500, millis()};
  snapshot.decoded.recent_grants[0] = snapshot.decoded.current_grant;
  snapshot.imbe_frames = 428;
  snapshot.imbe_errors = 2;
  return snapshot;
}

orcsdr::settings::State ui_doc_settings_state() {
  auto state = global_settings_state();
  state.wifi_power_enabled = state.wifi_ready = state.wifi_connected = true;
  state.wifi_scanning = state.wifi_connecting = false;
  state.wifi_rssi = -48;
  strlcpy(state.wifi_ssid, "OrcSDR Demo", sizeof(state.wifi_ssid));
  strlcpy(state.wifi_ip, "192.0.2.42", sizeof(state.wifi_ip));
  strlcpy(state.wifi_message, "Connected using external antenna",
          sizeof(state.wifi_message));
  state.network_count = 3;
  strlcpy(state.networks[0].ssid, "OrcSDR Demo", sizeof(state.networks[0].ssid));
  state.networks[0].rssi = -48;
  state.networks[0].secure = state.networks[0].saved = true;
  strlcpy(state.networks[1].ssid, "Workshop Guest", sizeof(state.networks[1].ssid));
  state.networks[1].rssi = -62;
  state.networks[1].secure = true;
  strlcpy(state.networks[2].ssid, "Open Lab", sizeof(state.networks[2].ssid));
  state.networks[2].rssi = -71;
  state.saved_network_count = 1;
  strlcpy(state.profiles[0].ssid, "OrcSDR Demo", sizeof(state.profiles[0].ssid));
  state.profiles[0].connected = true;
  state.location_configured = true;
  state.latitude_e7 = 455230640;
  state.longitude_e7 = -1226764830;
  strlcpy(state.location_label, "Portland, Oregon", sizeof(state.location_label));
  strlcpy(state.map_pack, "Portland 10/25/50/100 NM", sizeof(state.map_pack));
  state.wifi_external_antenna = true;
  state.sd_ready = true;
  state.sd_total_bytes = 32ull * 1024 * 1024 * 1024;
  state.sd_free_bytes = 21ull * 1024 * 1024 * 1024;
  state.battery_level = 76;
  state.battery_mv = 7900;
  state.battery_current_ma = -320;
  state.vbus_mv = 5000;
  strlcpy(state.charging_state, "CHARGING", sizeof(state.charging_state));
  strlcpy(state.build_identity, "OrcSDR documentation build",
          sizeof(state.build_identity));
  state.uptime_seconds = 7342;
  strlcpy(state.default_band, "FM", sizeof(state.default_band));
  state.fm_frequency_hz = 101700000;
  return state;
}

bool ui_doc_view_for_suffix(const char* suffix, orcsdr::fm::View* view) {
  static constexpr const char* names[] = {"listen", "spectrum", "station-rds",
                                           "rf-health", "settings"};
  for (uint8_t i = 0; i < std::size(names); ++i)
    if (strcmp(suffix, names[i]) == 0) {
      *view = static_cast<orcsdr::fm::View>(i);
      return true;
    }
  return false;
}

bool ui_doc_view_for_suffix(const char* suffix, orcsdr::p25::View* view) {
  static constexpr const char* names[] = {"monitor", "spectrum", "talkgroups",
                                           "program", "rf-health"};
  for (uint8_t i = 0; i < std::size(names); ++i)
    if (strcmp(suffix, names[i]) == 0) {
      *view = static_cast<orcsdr::p25::View>(i);
      return true;
    }
  return false;
}

bool ui_doc_render(const char* screen_id, bool demo) {
  if (!ui_doc_screen_exists(screen_id, demo ? "demo" : "live")) return false;
  ui_doc_leave_surfaces();
  rtl_nav_open = false;
  rtl_frequency_keypad_open = false;

  if (strcmp(screen_id, "home") == 0) {
    rtl_ui_active.store(false, std::memory_order_release);
    show_home(demo);
  } else if (strcmp(screen_id, "nav") == 0 ||
             strcmp(screen_id, "overlay.frequency-keypad") == 0) {
    rtl_ui_active.store(true, std::memory_order_release);
    rtl_ui_band = RtlBand::browse;
    rtl_ui_frequency_hz = kRtlBrowseDefaultHz;
    g_orc_tool.store(static_cast<uint8_t>(OrcTool::Radio), std::memory_order_release);
    draw_sdr_screen(rtl_ui_band, rtl_ui_frequency_hz, rtl_ui_volume);
    rtl_nav_open = true;
    rtl_frequency_keypad_open = strcmp(screen_id, "overlay.frequency-keypad") == 0;
    if (rtl_frequency_keypad_open)
      strlcpy(rtl_frequency_entry, "146.520", sizeof(rtl_frequency_entry));
    draw_tool_tabs();
  } else if (strncmp(screen_id, "settings.", 9) == 0 ||
             strncmp(screen_id, "overlay.wifi-", 13) == 0 ||
             strcmp(screen_id, "overlay.masked-keyboard") == 0) {
    static constexpr const char* names[] = {"connectivity", "location-adsb", "data-maps",
        "display-audio", "radio-defaults", "storage", "companion", "system"};
    orcsdr::settings::Section section = orcsdr::settings::Section::connectivity;
    const char* suffix = screen_id + 9;
    for (uint8_t i = 0; i < std::size(names); ++i)
      if (strcmp(suffix, names[i]) == 0) section = static_cast<orcsdr::settings::Section>(i);
    const bool keyboard = strcmp(screen_id, "overlay.masked-keyboard") == 0;
    auto state = ui_doc_settings_state();
    if (strcmp(screen_id, "overlay.wifi-scanning") == 0) {
      state.wifi_connected = false;
      state.wifi_scanning = true;
      state.network_count = 0;
      state.wifi_ip[0] = '\0';
      strlcpy(state.wifi_message, "Scanning for nearby networks",
              sizeof(state.wifi_message));
    }
    orcsdr::settings::show_documentation_section(
        orcsdr::settings::Section::connectivity, state, keyboard);
    if (strncmp(screen_id, "settings.", 9) == 0)
      orcsdr::settings::show_documentation_section(section, state);
  } else if (strncmp(screen_id, "fm.", 3) == 0 ||
             strcmp(screen_id, "overlay.volume") == 0) {
    orcsdr::fm::View view = orcsdr::fm::View::listen;
    if (strncmp(screen_id, "fm.", 3) == 0 &&
        !ui_doc_view_for_suffix(screen_id + 3, &view)) return false;
    orcsdr::fm::show_documentation_view(view, ui_doc_fm_snapshot(demo),
                                        strcmp(screen_id, "overlay.volume") == 0);
  } else if (strncmp(screen_id, "p25.", 4) == 0) {
    orcsdr::p25::View view;
    if (!ui_doc_view_for_suffix(screen_id + 4, &view)) return false;
    orcsdr::p25::show_documentation_view(view, ui_doc_p25_snapshot(demo));
  } else if (strncmp(screen_id, "adsb.", 5) == 0) {
    static constexpr const char* names[] = {"radar", "list", "target", "stats", "settings"};
    uint8_t view = 0;
    bool found = false;
    for (uint8_t i = 0; i < std::size(names); ++i)
      if (strcmp(screen_id + 5, names[i]) == 0) { view = i; found = true; }
    if (!found) return false;
    auto settings = adsb_settings;
    if (demo) {
      settings.location_configured = true;
      settings.latitude_e7 = 455230640;
      settings.longitude_e7 = -1226764830;
      settings.radar_range_nm = 25;
    }
    orcsdr::adsb::show_documentation_view(view, settings, demo);
  } else if (strncmp(screen_id, "lora.", 5) == 0) {
    const char* suffix = screen_id + 5;
    lora_view = strcmp(suffix, "packets") == 0 ? LoraView::Packets
                : strcmp(suffix, "map") == 0 ? LoraView::Map : LoraView::Live;
    rtl_ui_active.store(true, std::memory_order_release);
    rtl_ui_band = RtlBand::lora;
    rtl_ui_frequency_hz = kLoraDefaultHz;
    if (demo) {
      lora_display_packets[0] = {};
      strlcpy(lora_display_packets[0].text, "Position and telemetry received",
              sizeof(lora_display_packets[0].text));
      lora_display_packets[0].sender = 0xA1B2C3D4;
      lora_display_packets[0].destination = 0xFFFFFFFF;
      lora_display_packets[0].packet_id = 7;
      lora_display_packets[0].received_ms = millis();
      lora_display_packets[0].latitude_e7 = 455230640;
      lora_display_packets[0].longitude_e7 = -1226764830;
      lora_display_packets[0].snr_tenths = 94;
      lora_display_packets[0].signal_tenths = -720;
      lora_display_packets[0].port = 1;
      lora_node_positions[0] = {0xA1B2C3D4, millis(), 455230640, -1226764830};
      lora_messages.store(1, std::memory_order_relaxed);
      lora_noise_dbfs.store(-80.0f, std::memory_order_relaxed);
      lora_trigger_dbfs.store(-71.0f, std::memory_order_relaxed);
      rtl_signal_dbfs_smooth = -54.0f;
    }
    draw_sdr_screen(rtl_ui_band, rtl_ui_frequency_hz, rtl_ui_volume);
  } else {
    struct GenericBand { const char* name; RtlBand band; uint32_t frequency; };
    static constexpr GenericBand bands[] = {{"am.", RtlBand::am, kRtlAmDefaultHz},
        {"wx.", RtlBand::wx, kRtlWxHz}, {"cb.", RtlBand::cb, kCbDefaultHz},
        {"browse.", RtlBand::browse, kRtlBrowseDefaultHz}};
    bool found = false;
    for (const auto& entry : bands) {
      const size_t prefix = strlen(entry.name);
      if (strncmp(screen_id, entry.name, prefix) != 0) continue;
      const char* suffix = screen_id + prefix;
      const OrcTool tool = strcmp(suffix, "scope") == 0 ? OrcTool::Scope
                           : strcmp(suffix, "capture") == 0 ? OrcTool::Capture
                                                            : OrcTool::Radio;
      rtl_ui_active.store(true, std::memory_order_release);
      rtl_ui_band = entry.band;
      rtl_ui_frequency_hz = entry.frequency;
      g_orc_tool.store(static_cast<uint8_t>(tool), std::memory_order_release);
      if (demo) {
        rtl_signal_dbfs_smooth = -38.0f;
        if (entry.band == RtlBand::cb) {
          cb_mode.store(CbMode::am, std::memory_order_relaxed);
          cb_clarifier_hz.store(0, std::memory_order_relaxed);
          cb_squelch_dbfs.store(-55, std::memory_order_relaxed);
          cb_squelch_open.store(true, std::memory_order_relaxed);
        }
      }
      draw_sdr_screen(entry.band, entry.frequency, rtl_ui_volume);
      if (demo) {
        if (tool == OrcTool::Capture) draw_capture_tool_panel();
        else draw_documentation_spectrum();
      }
      found = true;
      break;
    }
    if (!found) return false;
  }
  if (demo) ui_doc_badge();
  strlcpy(ui_doc.current_screen, screen_id, sizeof(ui_doc.current_screen));
  ui_doc.current_demo = demo;
  return true;
}

bool ui_doc_live_band(const char* screen_id, RtlBand* band, uint32_t* frequency_hz) {
  if (strncmp(screen_id, "fm.", 3) == 0) {
    *band = RtlBand::fm; *frequency_hz = rtl_saved_fm_hz; return true;
  }
  if (strncmp(screen_id, "p25.", 4) == 0) {
    *band = RtlBand::p25; *frequency_hz = p25_control_frequency_hz; return true;
  }
  if (strncmp(screen_id, "adsb.", 5) == 0) {
    *band = RtlBand::adsb; *frequency_hz = kAdsbDefaultHz; return true;
  }
  if (strncmp(screen_id, "lora.", 5) == 0) {
    *band = RtlBand::lora; *frequency_hz = kLoraDefaultHz; return true;
  }
  struct Entry { const char* prefix; RtlBand band; uint32_t frequency; };
  static constexpr Entry entries[] = {{"am.", RtlBand::am, kRtlAmDefaultHz},
      {"wx.", RtlBand::wx, kRtlWxHz}, {"cb.", RtlBand::cb, kCbDefaultHz},
      {"browse.", RtlBand::browse, kRtlBrowseDefaultHz}};
  for (const auto& entry : entries)
    if (strncmp(screen_id, entry.prefix, strlen(entry.prefix)) == 0) {
      *band = entry.band; *frequency_hz = entry.frequency; return true;
    }
  return false;
}

void ui_doc_enter() {
  if (ui_doc.active) return;
  ui_doc = {};
  ui_doc.active = true;
  ui_doc.was_receiving =
      rtl_capture_state.load(std::memory_order_acquire) == RtlCaptureState::running;
  ui_doc.was_ui_active = rtl_ui_active.load(std::memory_order_acquire);
  ui_doc.was_settings_active = orcsdr::settings::active();
  ui_doc.was_home_active = orcsdr::home::active();
  ui_doc.graphics_enabled = rtl_graphics_enabled.exchange(false, std::memory_order_acq_rel);
  ui_doc.nav_open = rtl_nav_open;
  ui_doc.band = rtl_ui_band;
  ui_doc.frequency_hz = rtl_ui_frequency_hz;
  ui_doc.tool = orc_tool_current();
  if (orcsdr::fm::active()) ui_doc.fm_view = orcsdr::fm::view();
  if (orcsdr::p25::active()) ui_doc.p25_view = orcsdr::p25::view();
  if (orcsdr::adsb::active()) ui_doc.adsb_view = orcsdr::adsb::view();
  if (orcsdr::settings::active()) ui_doc.settings_section = orcsdr::settings::section();
  ui_doc.lora = lora_view;
  memcpy(ui_doc.lora_packets, lora_display_packets, sizeof(lora_display_packets));
  memcpy(ui_doc.lora_positions, lora_node_positions, sizeof(lora_node_positions));
  ui_doc.lora_message_count = lora_messages.load(std::memory_order_relaxed);
  ui_doc.lora_noise = lora_noise_dbfs.load(std::memory_order_relaxed);
  ui_doc.lora_trigger = lora_trigger_dbfs.load(std::memory_order_relaxed);
  ui_doc.signal_dbfs = rtl_signal_dbfs_smooth;
  ui_doc.cb_mode_value = cb_mode.load(std::memory_order_relaxed);
  ui_doc.cb_clarifier = cb_clarifier_hz.load(std::memory_order_relaxed);
  ui_doc.cb_squelch = cb_squelch_dbfs.load(std::memory_order_relaxed);
  ui_doc.cb_squelch_was_open = cb_squelch_open.load(std::memory_order_relaxed);
  ui_documentation_mode = true;
}

bool ui_doc_pause_reception() {
  rtl_capture_requested.store(false, std::memory_order_release);
  rtl_restart_requested.store(false, std::memory_order_release);
  if (rtl_capture_state.load(std::memory_order_acquire) == RtlCaptureState::running)
    rtl_stop_requested.store(true, std::memory_order_release);
  const uint32_t deadline = millis() + 5000;
  while (rtl_capture_state.load(std::memory_order_acquire) == RtlCaptureState::running &&
         static_cast<int32_t>(deadline - millis()) > 0) delay(10);
  return rtl_capture_state.load(std::memory_order_acquire) != RtlCaptureState::running;
}

[[gnu::noinline]] void ui_doc_exit() {
  if (!ui_doc.active) return;
  ui_documentation_mode = false;
  memcpy(lora_display_packets, ui_doc.lora_packets, sizeof(lora_display_packets));
  memcpy(lora_node_positions, ui_doc.lora_positions, sizeof(lora_node_positions));
  lora_messages.store(ui_doc.lora_message_count, std::memory_order_relaxed);
  lora_noise_dbfs.store(ui_doc.lora_noise, std::memory_order_relaxed);
  lora_trigger_dbfs.store(ui_doc.lora_trigger, std::memory_order_relaxed);
  rtl_signal_dbfs_smooth = ui_doc.signal_dbfs;
  cb_mode.store(ui_doc.cb_mode_value, std::memory_order_relaxed);
  cb_clarifier_hz.store(ui_doc.cb_clarifier, std::memory_order_relaxed);
  cb_squelch_dbfs.store(ui_doc.cb_squelch, std::memory_order_relaxed);
  cb_squelch_open.store(ui_doc.cb_squelch_was_open, std::memory_order_relaxed);
  lora_view = ui_doc.lora;
  rtl_graphics_enabled.store(ui_doc.graphics_enabled, std::memory_order_release);
  rtl_ui_band = ui_doc.band;
  rtl_ui_frequency_hz = ui_doc.frequency_hz;
  g_orc_tool.store(static_cast<uint8_t>(ui_doc.tool), std::memory_order_release);
  rtl_ui_active.store(ui_doc.was_ui_active, std::memory_order_release);
  rtl_nav_open = ui_doc.nav_open;
  ui_doc_leave_surfaces();
  if (ui_doc.was_home_active) {
    show_home();
  } else if (ui_doc.was_settings_active) {
    orcsdr::settings::enter(global_settings_state(), ui_doc.settings_section);
  } else if (ui_doc.was_ui_active) {
    if (ui_doc.band == RtlBand::fm)
      orcsdr::fm::show_documentation_view(ui_doc.fm_view, fm_dashboard_snapshot());
    else if (ui_doc.band == RtlBand::p25)
      orcsdr::p25::show_documentation_view(ui_doc.p25_view, p25_dashboard_snapshot());
    else if (ui_doc.band == RtlBand::adsb)
      orcsdr::adsb::show_documentation_view(ui_doc.adsb_view, adsb_settings, false);
    else
      draw_sdr_screen(ui_doc.band, ui_doc.frequency_hz, rtl_ui_volume);
  } else {
    show_home();
  }
  if (ui_doc.was_receiving)
    queue_local_rtl_listen(ui_doc.band, ui_doc.frequency_hz, false);
  ui_doc = {};
}

void process_command(char* command) {
  if (strncmp(command, "UI_DOC_", 7) == 0 || strncmp(command, "UI_CAPTURE ", 11) == 0) {
    if (!authenticated) {
      Serial.println("UI_DOC_ERROR auth_required");
      return;
    }
    last_ping_ms = millis();
    if (strcmp(command, "UI_DOC_LIST") == 0) {
      const esp_app_desc_t* app = esp_app_get_description();
      Serial.printf("UI_DOC_LIST_BEGIN count=%u firmware=\"%s\"\n",
                    static_cast<unsigned>(std::size(kUiDocScreens)), app->version);
      for (const auto& screen : kUiDocScreens)
        Serial.printf("UI_DOC_SCREEN id=%s modes=%s\n", screen.id, screen.modes);
      Serial.println("UI_DOC_LIST_DONE");
      return;
    }
    if (strncmp(command, "UI_DOC_SHOW ", 12) == 0) {
      char screen[40]{};
      char mode[8]{};
      char trailing = 0;
      if (sscanf(command + 12, "%39s %7s %c", screen, mode, &trailing) != 2 ||
          (strcmp(mode, "live") != 0 && strcmp(mode, "demo") != 0)) {
        Serial.println("UI_DOC_ERROR usage: UI_DOC_SHOW <screen-id> <live|demo>");
        return;
      }
      if (!ui_doc_screen_exists(screen, mode)) {
        Serial.println("UI_DOC_ERROR unknown_screen_or_mode");
        return;
      }
      ui_doc_enter();
      if (strcmp(mode, "demo") == 0) {
        if (!ui_doc_pause_reception()) {
          Serial.println("UI_DOC_ERROR reception_stop_timeout");
          return;
        }
      } else {
        RtlBand band;
        uint32_t frequency_hz = 0;
        if (ui_doc_live_band(screen, &band, &frequency_hz) &&
            (rtl_capture_state.load(std::memory_order_acquire) != RtlCaptureState::running ||
             rtl_ui_band != band || rtl_ui_frequency_hz != frequency_hz))
          queue_local_rtl_listen(band, frequency_hz, false);
      }
      if (!ui_doc_render(screen, strcmp(mode, "demo") == 0)) {
        Serial.println("UI_DOC_ERROR unknown_screen_or_mode");
        return;
      }
      Serial.printf("UI_DOC_SHOW_DONE id=%s mode=%s\n", screen, mode);
      return;
    }
    if (strncmp(command, "UI_CAPTURE ", 11) == 0) {
      const char* slug = command + 11;
      if (!ui_doc.active || !ui_doc.current_screen[0]) {
        Serial.println("UI_CAPTURE_ERROR documentation_mode_inactive");
        return;
      }
      if (!orcsdr::ui_capture::valid_slug(slug)) {
        Serial.println("UI_CAPTURE_ERROR invalid_slug");
        return;
      }
      if (!ui_doc_pause_reception()) {
        Serial.println("UI_CAPTURE_ERROR reception_stop_timeout");
        return;
      }
      if (!ensure_tab5_sd() || g_sd_fs == nullptr) {
        Serial.println("UI_CAPTURE_ERROR sd_unavailable");
        return;
      }
      const auto result = orcsdr::ui_capture::save_bmp(M5.Display, *g_sd_fs, slug);
      if (!result.ok) {
        Serial.printf("UI_CAPTURE_ERROR %s\n", result.error ? result.error : "capture_failed");
        return;
      }
      const esp_app_desc_t* app = esp_app_get_description();
      Serial.printf("UI_CAPTURE_DONE slug=%s path=\"/orcsdr/screenshots/%s.bmp\" "
                    "bytes=%u width=%u height=%u firmware=\"%s\" sha256=",
                    slug, slug, static_cast<unsigned>(result.bytes), result.width,
                    result.height, app->version);
      print_hex(result.sha256, sizeof(result.sha256));
      Serial.println();
      return;
    }
    if (strcmp(command, "UI_DOC_EXIT") == 0) {
      ui_doc_exit();
      Serial.println("UI_DOC_EXIT_DONE restored=true");
      return;
    }
  }
  if (strcmp(command, "RTL_WIFI_SCAN") == 0) {
    wifi_scan_requested.store(true, std::memory_order_release);
    Serial.println("RTL_WIFI_SCAN_QUEUED");
    return;
  }
  if (strcmp(command, "RTL_WIFI_CONNECT_SAVED") == 0) {
    if (wifi_profile_count == 0) {
      Serial.println("RTL_WIFI_CONNECT_ERROR no_saved_profile");
      return;
    }
    select_wifi_profile(0);
    wifi_connect_requested.store(true, std::memory_order_release);
    Serial.println("RTL_WIFI_CONNECT_QUEUED saved_profile=0");
    return;
  }
  if (strcmp(command, "RTL_WIFI_STATUS") == 0) {
    Serial.printf("RTL_WIFI_STATUS station=%d hosted_match=%d scanning=%d connecting=%d "
                  "connected=%d saved_profiles=%u\n",
                  wifi_station_ready ? 1 : 0, wifi_hosted_versions_match ? 1 : 0,
                  wifi_scan_running ? 1 : 0, wifi_connecting ? 1 : 0,
                  wifi_connected ? 1 : 0, wifi_profile_count);
    return;
  }
  if (strncmp(command, "RTL_ADSB_LOCATION ", 18) == 0) {
    double latitude = 0, longitude = 0;
    char trailing = 0;
    if (sscanf(command + 18, "%lf %lf %c", &latitude, &longitude, &trailing) != 2 ||
        latitude < -90.0 || latitude > 90.0 || longitude < -180.0 || longitude > 180.0) {
      Serial.println("RTL_ADSB_LOCATION_ERROR invalid_coordinate");
      return;
    }
    adsb_settings.location_configured = true;
    adsb_settings.latitude_e7 = static_cast<int32_t>(llround(latitude * 10000000.0));
    adsb_settings.longitude_e7 = static_cast<int32_t>(llround(longitude * 10000000.0));
    preferences.putBool("adsb_loc_set", true);
    preferences.putInt("adsb_lat_e7", adsb_settings.latitude_e7);
    preferences.putInt("adsb_lon_e7", adsb_settings.longitude_e7);
    if (orcsdr::adsb::active()) orcsdr::adsb::enter(adsb_settings);
    Serial.println("RTL_ADSB_LOCATION_OK");
    return;
  }
  if (strcmp(command, "RTL_ADSB_STOP") == 0 && rtl_ui_band == RtlBand::adsb) {
    rtl_stop_requested.store(true, std::memory_order_release);
    Serial.println("RTL_ADSB_STOPPING");
    return;
  }
  if (strcmp(command, "RTL_ADSB_START") == 0) {
    queue_local_rtl_listen(RtlBand::adsb, kAdsbDefaultHz);
    Serial.println("RTL_ADSB_STARTING");
    return;
  }
  if (strcmp(command, "SD_LIST") == 0) {
    sd_list();
    return;
  }
  if (strncmp(command, "SD_GET_BEGIN ", 13) == 0) {
    sd_get_begin(command + 13);
    return;
  }
  if (strcmp(command, "SD_GET_CHUNK") == 0) {
    sd_get_chunk();
    return;
  }
  if (strcmp(command, "SD_GET_ABORT") == 0) {
    if (g_sd_get.active) sd_get_abort("host_abort");
    else Serial.println("SD_GET_ABORTED");
    return;
  }
  if (strncmp(command, "SD_REMOVE ", 10) == 0) {
    sd_remove(command + 10);
    return;
  }
  if (strncmp(command, "SD_PUT_BEGIN ", 13) == 0) {
    sd_put_begin(command + 13);
    return;
  }
  if (strncmp(command, "SD_PUT_CHUNK ", 13) == 0) {
    sd_put_chunk(command + 13);
    return;
  }
  if (strcmp(command, "SD_PUT_ABORT") == 0) {
    if (g_sd_put.active) {
      sd_put_abort("host_abort");
    } else {
      Serial.println("SD_PUT_ABORTED");
    }
    return;
  }
  if (strcmp(command, "RTL_CAPTURE_STATUS") == 0) {
    const RtlCaptureState state = rtl_capture_state.load(std::memory_order_acquire);
    if (state != RtlCaptureState::complete && state != RtlCaptureState::failed) {
      Serial.printf("RTL_CAPTURE_STATUS state=%s\n", rtl_capture_state_name(state));
      return;
    }
    Serial.printf(
        "RTL_CAPTURE_STATUS state=%s bytes=%llu min=%u max=%u mean=%.3f sha256=%s error=\"%s\"\n",
        rtl_capture_state_name(state),
        static_cast<unsigned long long>(rtl_capture_bytes), rtl_capture_min,
        rtl_capture_max, rtl_capture_mean,
        rtl_capture_sha256[0] == '\0' ? "none" : rtl_capture_sha256,
        rtl_capture_error);
    return;
  }
  if (strcmp(command, "RTL_IQ_START") == 0) {
    (void)iq_rec_start();
    return;
  }
  if (strcmp(command, "RTL_IQ_STOP") == 0 || strcmp(command, "RTL_IQ_SAVE") == 0) {
    g_sd_tried = false;
    g_sd_ready = false;
    (void)iq_rec_stop_and_export();
    return;
  }
  if (strcmp(command, "RTL_IQ_STATUS") == 0) {
    Serial.printf("RTL_IQ_STATUS active=%s ready=%s storage=%s mode=%s bytes=%u max_bytes=%u frequency_hz=%u sf=%u bw=%u auto=%s events=%u messages=%u noise_dbfs=%.1f trigger_dbfs=%.1f last_path=\"%s\"\n",
                  g_iq_rec_active.load(std::memory_order_acquire) ? "true" : "false",
                  g_iq_rec_ready.load(std::memory_order_acquire) ? "true" : "false",
                  "psram",
                  g_iq_rec_auto_triggered.load(std::memory_order_acquire) ? "energy" : "manual",
                  static_cast<unsigned>(g_iq_rec_write.load(std::memory_order_acquire)),
                  static_cast<unsigned>(kIqRecMaxBytes), g_iq_rec_frequency_hz,
                  static_cast<unsigned>(g_iq_rec_sf),
                  static_cast<unsigned>(g_iq_rec_bandwidth_hz),
                  lora_detector_enabled.load(std::memory_order_relaxed) ? "on" : "off",
                  static_cast<unsigned>(lora_rf_events.load(std::memory_order_relaxed)),
                  static_cast<unsigned>(lora_messages.load(std::memory_order_relaxed)),
                  static_cast<double>(lora_noise_dbfs.load(std::memory_order_relaxed)),
                  static_cast<double>(lora_trigger_dbfs.load(std::memory_order_relaxed)),
                  g_iq_rec_last_path[0] ? g_iq_rec_last_path : "none");
    return;
  }
  if (strcmp(command, "RTL_LORA_AUTO ON") == 0 ||
      strcmp(command, "RTL_LORA_AUTO OFF") == 0) {
    const bool enabled = command[14] == 'O' && command[15] == 'N';
    lora_detector_enabled.store(enabled, std::memory_order_release);
    Serial.printf("RTL_LORA_AUTO %s\n", enabled ? "ON" : "OFF");
    return;
  }
  if (strncmp(command, "RTL_LORA_TUNE ", 14) == 0) {
    char* end = nullptr;
    const unsigned long requested = strtoul(command + 14, &end, 10);
    if (end == command + 14 || *end != '\0' || requested < kLoraMinHz ||
        requested > kLoraMaxHz) {
      Serial.printf("RTL_LORA_TUNE_ERROR range=%u-%u\n", kLoraMinHz, kLoraMaxHz);
      return;
    }
    const uint32_t frequency_hz = static_cast<uint32_t>(requested);
    if (rtl_capture_state.load(std::memory_order_acquire) == RtlCaptureState::running &&
        rtl_ui_band == RtlBand::lora) {
      request_hot_retune(frequency_hz);
    } else {
      queue_local_rtl_listen(RtlBand::lora, frequency_hz);
    }
    Serial.printf("RTL_LORA_TUNE_OK frequency_hz=%u\n", frequency_hz);
    return;
  }
  if (strcmp(command, "LORA_SD_LOG ON") == 0) {
    set_lora_sd_logging(true);
    return;
  }
  if (strcmp(command, "LORA_SD_LOG OFF") == 0) {
    set_lora_sd_logging(false);
    return;
  }
  if (strcmp(command, "LORA_SD_LOG STATUS") == 0) {
    Serial.printf(
        "LORA_SD_LOG_STATUS requested=%s ready=%s error=%s queued=%u "
        "dropped=%lu path=\"%s\"\n",
        lora_log_requested.load(std::memory_order_relaxed) ? "true" : "false",
        lora_log_ready.load(std::memory_order_relaxed) ? "true" : "false",
        lora_log_error.load(std::memory_order_relaxed) ? "true" : "false",
        lora_log_queue == nullptr
            ? 0u
            : static_cast<unsigned>(uxQueueMessagesWaiting(lora_log_queue)),
        static_cast<unsigned long>(
            lora_log_dropped.load(std::memory_order_relaxed)),
        kLoraLogPath);
    return;
  }
  if (strncmp(command, "LORA_MESSAGE ", 13) == 0) {
    if (!lora_present_host_message(command + 13)) {
      Serial.println("LORA_MESSAGE_ERROR invalid_fields");
      return;
    }
    Serial.println("LORA_MESSAGE_OK");
    return;
  }
  if (strncmp(command, "LORA_PACKET ", 12) == 0) {
    if (!lora_present_host_packet(command + 12)) {
      Serial.println("LORA_PACKET_ERROR invalid_fields");
      return;
    }
    Serial.println("LORA_PACKET_OK");
    return;
  }
  if (strcmp(command, "LORA_MESSAGE_CLEAR") == 0) {
    portENTER_CRITICAL(&lora_message_mux);
    for (auto& packet : lora_display_packets) packet = {};
    for (auto& position : lora_node_positions) position = {};
    portEXIT_CRITICAL(&lora_message_mux);
    Serial.println("LORA_MESSAGE_CLEARED");
    return;
  }
  if (strcmp(command, "RTL_IQ_RETRIEVE_BEGIN") == 0) {
    if (!g_iq_rec_ready.load(std::memory_order_acquire) ||
        g_iq_rec_active.load(std::memory_order_acquire)) {
      Serial.println("RTL_IQ_RETRIEVE_ERROR capture_not_ready");
      return;
    }
    // The completed PSRAM buffer is immutable while g_iq_rec_ready is true,
    // so retrieval does not need to interrupt RTL acquisition or rendering.
    g_iq_retrieve_resume.store(false, std::memory_order_release);
    Serial.print("RTL_IQ_RETRIEVE_READY storage=psram");
    Serial.printf(" bytes=%u rate=%u frequency_hz=%u sf=%u bw=%u\n",
                  static_cast<unsigned>(g_iq_rec_write.load(std::memory_order_acquire)),
                  kRtlSampleRateSps, g_iq_rec_frequency_hz,
                  static_cast<unsigned>(g_iq_rec_sf),
                  static_cast<unsigned>(g_iq_rec_bandwidth_hz));
    return;
  }
  if (strcmp(command, "RTL_IQ_GET_BEGIN") == 0) {
    iq_get_begin();
    return;
  }
  if (strcmp(command, "RTL_IQ_GET_CHUNK") == 0) {
    iq_get_chunk();
    return;
  }
  if (strcmp(command, "RTL_IQ_GET_ABORT") == 0) {
    iq_get_reset();
    Serial.printf("RTL_IQ_GET_ABORTED ready=%s bytes=%u\n",
                  g_iq_rec_ready.load(std::memory_order_acquire) ? "true" : "false",
                  static_cast<unsigned>(g_iq_rec_write.load(std::memory_order_acquire)));
    return;
  }
  if (strcmp(command, "RTL_IQ_RETRIEVE_END") == 0) {
    if (g_iq_get.active) iq_get_abort("host_end");
    g_iq_rec_ready.store(false, std::memory_order_release);
    g_iq_rec_write.store(0, std::memory_order_release);
    g_iq_rec_auto_triggered.store(false, std::memory_order_release);
    if (g_iq_retrieve_resume.exchange(false, std::memory_order_acq_rel) &&
        rtl_device_ready()) {
      queue_local_rtl_listen(RtlBand::lora, rtl_ui_frequency_hz);
      Serial.println("RTL_IQ_RETRIEVE_RESUMING");
    } else {
      Serial.println("RTL_IQ_RETRIEVE_DONE");
    }
    return;
  }
  if (strcmp(command, "RTL_REC_START") == 0) {
    if (orc_tool_current() != OrcTool::Capture) set_orc_tool(OrcTool::Capture);
    (void)audio_rec_start();
    return;
  }
  if (strcmp(command, "RTL_REC_STOP") == 0) {
    (void)audio_rec_stop_and_export();
    return;
  }
  if (strcmp(command, "RTL_REC_STATUS") == 0) {
    audio_rec_status_print();
    return;
  }
  if (strcmp(command, "RTL_REC_SAVE") == 0) {
    /* Re-export held PSRAM PCM after inserting an SD card. */
    g_sd_tried = false;
    g_sd_ready = false;
    (void)audio_rec_stop_and_export();
    return;
  }
  if (strncmp(command, "RTL_TOOL ", 9) == 0) {
    const char* name = command + 9;
    if (strcmp(name, "RADIO") == 0 || strcmp(name, "radio") == 0) {
      set_orc_tool(OrcTool::Radio);
    } else if (strcmp(name, "SCOPE") == 0 || strcmp(name, "scope") == 0) {
      set_orc_tool(OrcTool::Scope);
    } else if (strcmp(name, "CAPTURE") == 0 || strcmp(name, "capture") == 0) {
      set_orc_tool(OrcTool::Capture);
    } else {
      Serial.println("RTL_TOOL_INVALID use RADIO|SCOPE|CAPTURE");
    }
    return;
  }
  if (strcmp(command, "RTL_TOOL") == 0) {
    Serial.printf("RTL_TOOL_STATUS tool=%s\n", orc_tool_name(orc_tool_current()));
    return;
  }
  if (strcmp(command, "RTL_STOP") == 0 && (authenticated || ORC_LORA_TEST_BUILD)) {
    rtl_restart_requested.store(false, std::memory_order_release);
    rtl_stop_requested.store(true, std::memory_order_release);
    Serial.println("RTL_STOPPING");
    return;
  }
  const bool continuous_command = strncmp(command, "RTL_LISTEN", 10) == 0;
  const bool kzel_capture = strcmp(command, "RTL_CAPTURE") == 0 ||
                            strcmp(command, "RTL_CAPTURE KZEL") == 0 ||
                            strcmp(command, "RTL_LISTEN KZEL") == 0 ||
                            strcmp(command, "RTL_LISTEN FM") == 0;
  const bool noaa_capture = strcmp(command, "RTL_CAPTURE NOAA") == 0 ||
                            strcmp(command, "RTL_LISTEN NOAA") == 0 ||
                            strcmp(command, "RTL_LISTEN WX") == 0;
  const bool am_capture = strcmp(command, "RTL_CAPTURE AM") == 0 ||
                          strcmp(command, "RTL_LISTEN AM") == 0;
  const bool lora_capture = strcmp(command, "RTL_CAPTURE LORA") == 0 ||
                            strcmp(command, "RTL_LISTEN LORA") == 0;
  if ((kzel_capture || noaa_capture || am_capture || lora_capture) && authenticated) {
    const RtlBand band =
        noaa_capture ? RtlBand::wx
        : am_capture ? RtlBand::am
        : lora_capture ? RtlBand::lora
                       : RtlBand::fm;
    const uint32_t frequency_hz = rtl_band_default_frequency(band);
    RtlCaptureState expected = RtlCaptureState::ready;
    const RtlCaptureState current = rtl_capture_state.load(std::memory_order_acquire);
    if (current == RtlCaptureState::complete || current == RtlCaptureState::failed) {
      expected = current;
    }
    if (!rtl_device_ready() ||
        !rtl_capture_state.compare_exchange_strong(expected, RtlCaptureState::queued,
                                                   std::memory_order_acq_rel)) {
      Serial.println("RTL_CAPTURE_BUSY_OR_UNAVAILABLE");
      return;
    }
    rtl_requested_band.store(band, std::memory_order_release);
    rtl_requested_frequency_hz.store(frequency_hz, std::memory_order_release);
    rtl_continuous_requested.store(continuous_command, std::memory_order_release);
    rtl_stop_requested.store(false, std::memory_order_release);
    rtl_restart_requested.store(false, std::memory_order_release);
    rtl_capture_requested.store(true, std::memory_order_release);
    Serial.printf("RTL_CAPTURE_QUEUED band=%s frequency_hz=%u sample_rate_sps=%u "
                  "bytes=%u continuous=%s volume=%u\n",
                  rtl_band_name(band), frequency_hz, kRtlSampleRateSps, kRtlCaptureBytes,
                  continuous_command ? "true" : "false",
                  rtl_requested_volume.load(std::memory_order_acquire));
    return;
  }
  if (strncmp(command, "RTL_VOLUME ", 11) == 0 && authenticated) {
    const int value = atoi(command + 11);
    if (value < kRtlVolumeMin || value > kRtlVolumeMax) {
      Serial.println("RTL_VOLUME_INVALID");
      return;
    }
    rtl_requested_volume.store(static_cast<uint8_t>(value), std::memory_order_release);
    rtl_live_volume.store(static_cast<uint8_t>(value), std::memory_order_release);
    rtl_ui_volume = static_cast<uint8_t>(value);
    rtl_volume_changed.store(true, std::memory_order_release);
    apply_speaker_volume(static_cast<uint8_t>(value));
    bump_rtl_ui();
    Serial.printf("RTL_VOLUME_OK volume=%u\n", value);
    return;
  }
  if (strcmp(command, "RTL_STATUS") == 0) {
    Serial.printf("RTL_SDR_STATUS connected=%s vid=%04x pid=%04x speed=%s serial=\"%s\"\n",
                  rtl_device_ready() ? "true" : "false", rtl_sdr_vid, rtl_sdr_pid,
                  rtl_sdr_speed, rtl_sdr_serial);
    return;
  }

  // ---------------------------------------------------------------------
  // CLI/serial control surface for scripted/AI-driven tuning and decoding
  // work — everything below is read-only or directly mirrors an existing
  // touch action, so it follows the same auth convention as its touch-UI
  // equivalent (state changes require `authenticated`, status queries do
  // not, matching RTL_STATUS/RTL_REC_STATUS/RTL_TOOL above).
  // ---------------------------------------------------------------------
  if (strcmp(command, "RTL_HELP") == 0) {
    Serial.println("RTL_HELP_BEGIN");
    Serial.println("RTL_STATUS                    - device connection info");
    Serial.println("RTL_TUNE <BAND> <HZ>           - tune band+freq (auth) BAND=FM|AM|WX|CB|LORA|BROWSE|ADSB|P25");
    Serial.println("RTL_FREQ                       - query current band/frequency/mode");
    Serial.println("RTL_FREQ <HZ>                  - hot-retune within current band (auth)");
    Serial.println("RTL_VOLUME                     - query current volume");
    Serial.println("RTL_VOLUME <0-32>              - set volume (auth)");
    Serial.println("RTL_SOUND                      - query audio-enabled state");
    Serial.println("RTL_SOUND ON|OFF                - enable/disable audio+demod pipeline (no auth)");
    Serial.println("RTL_AUDIO_TEST STATUS|TONE|FM|STOP - audio output validation (no auth)");
    Serial.println("RTL_SIGNAL                     - signal dBFS, stereo lock, L/R levels");
    Serial.println("RTL_PRESET_SCAN                - start FM band scan for presets (auth, FM only)");
    Serial.println("RTL_PRESET_LIST                - list current FM presets");
    Serial.println("RTL_PRESET_TUNE <n>            - tune to preset n, 1-based (auth, FM only)");
    Serial.println("RTL_RDS_STATUS                 - on-demand RDS Stage1/2 diagnostic dump");
    Serial.println("RTL_RDS_CAPTURE_START/STOP/STATUS - capture FM MPX to SD for replay");
    Serial.println("RTL_RDS_REPLAY <path.s16>       - replay captured MPX while radio is stopped");
    Serial.println("RTL_P25_STATUS                 - profile, survey, RF levels; decoded fields are explicit");
    Serial.println("RTL_P25_SCAN                   - survey known Lane County control candidates (auth)");
    Serial.println("RTL_REC_START/STOP/STATUS/SAVE - audio capture-to-WAV control");
    Serial.println("RTL_TOOL [RADIO|SCOPE|CAPTURE] - query/switch active tool tab");
    Serial.println("RTL_CAPTURE|RTL_LISTEN [FM|AM|WX|LORA] - one-shot/continuous band capture (auth)");
    Serial.println("RTL_STOP                       - stop active capture (auth)");
    Serial.println("SD_LIST/SD_GET_*/SD_PUT_*      - SD card file transfer (see copy_to_tab5_sd.ps1)");
    Serial.println("RTL_HELP_END");
    return;
  }
  if (strcmp(command, "RTL_P25_STATUS") == 0) {
    const auto decoded = orcsdr::p25decoder::snapshot();
    Serial.printf(
        "RTL_P25_STATUS profile=SW7_LRIG site=Lane_County_Simulcast "
        "frequency_hz=%lu survey=%d candidate=%u relative_dbfs=%.1f "
        "frame_sync=%d identity=%d nac=%03X wacn=%05lX sysid=%03X rfss=%u site=%u "
        "sync_words=%lu nid_good=%lu nid_failed=%lu tsbk_good=%lu tsbk_failed=%lu "
        "ber_percent=%.2f grants=%d follow=%s control_hz=%lu voice_hz=%lu "
        "voice_ldus=%lu voice_frames=%lu voice_queue_drops=%lu imbe_frames=%lu "
        "imbe_errors=%lu pcm_frames=%lu voice_stack_hwm=%lu imbe_max_us=%lu "
        "imbe_synth_max_us=%lu audio_queue_max_us=%lu\n",
        static_cast<unsigned long>(rtl_ui_frequency_hz),
        p25_survey_active.load(std::memory_order_relaxed) ? 1 : 0,
        static_cast<unsigned>(p25_candidate_index),
        static_cast<double>(rtl_signal_dbfs.load(std::memory_order_relaxed)),
        decoded.frame_sync ? 1 : 0, decoded.identity_valid ? 1 : 0,
        decoded.nac, static_cast<unsigned long>(decoded.wacn), decoded.system_id,
        decoded.rfss, decoded.site, static_cast<unsigned long>(decoded.sync_words),
        static_cast<unsigned long>(decoded.nid_good),
        static_cast<unsigned long>(decoded.nid_failed),
        static_cast<unsigned long>(decoded.tsbk_good),
        static_cast<unsigned long>(decoded.tsbk_failed),
        static_cast<double>(decoded.estimated_ber_percent),
        decoded.current_grant.valid ? 1 : 0,
        p25_follow_state.load(std::memory_order_acquire) == P25FollowState::voice
            ? "voice" : "control",
        static_cast<unsigned long>(p25_control_frequency_hz),
        static_cast<unsigned long>(p25_voice_frequency_hz),
        static_cast<unsigned long>(decoded.voice_ldus),
        static_cast<unsigned long>(decoded.voice_frames),
        static_cast<unsigned long>(decoded.voice_queue_drops),
        static_cast<unsigned long>(p25_imbe_frames.load(std::memory_order_relaxed)),
        static_cast<unsigned long>(p25_imbe_errors.load(std::memory_order_relaxed)),
        static_cast<unsigned long>(p25_pcm_frames.load(std::memory_order_relaxed)),
        static_cast<unsigned long>(p25_voice_stack_hwm.load(std::memory_order_relaxed)),
        static_cast<unsigned long>(p25_imbe_max_us.load(std::memory_order_relaxed)),
        static_cast<unsigned long>(p25_imbe_synth_max_us.load(std::memory_order_relaxed)),
        static_cast<unsigned long>(p25_audio_queue_max_us.load(std::memory_order_relaxed)));
    for (size_t i = 0; i < p25_config.control_channel_count; ++i)
      Serial.printf("RTL_P25_CANDIDATE index=%u frequency_hz=%lu relative_dbfs=%.1f\n",
                    static_cast<unsigned>(i),
                    static_cast<unsigned long>(p25_config.control_channels_hz[i]),
                    static_cast<double>(p25_candidate_levels[i]));
    return;
  }
  if (strcmp(command, "RTL_P25_CONFIG_RELOAD") == 0 && authenticated) {
    p25_survey_active.store(false, std::memory_order_release);
    load_p25_config();
    if (rtl_ui_band == RtlBand::p25) tune_p25_control(p25_control_frequency_hz);
    if (orcsdr::p25::active()) orcsdr::p25::draw();
    Serial.printf("RTL_P25_CONFIG_RELOAD status=\"%s\"\n", p25_config_status);
    return;
  }
  if (strcmp(command, "RTL_P25_SCAN") == 0 && authenticated) {
    if (rtl_ui_band != RtlBand::p25)
      queue_local_rtl_listen(RtlBand::p25, p25_control_frequency_hz);
    if (!p25_survey_active.load(std::memory_order_relaxed))
      handle_p25_dashboard_action({orcsdr::p25::ActionKind::survey_toggle});
    return;
  }
  if (strncmp(command, "RTL_TUNE ", 9) == 0 && authenticated) {
    char band_name[16] = {0};
    unsigned long freq_hz = 0;
    if (sscanf(command + 9, "%15s %lu", band_name, &freq_hz) != 2) {
      Serial.println("RTL_TUNE_INVALID usage: RTL_TUNE <BAND> <HZ>");
      return;
    }
    RtlBand band;
    if (!rtl_band_from_name(band_name, &band)) {
      Serial.println("RTL_TUNE_INVALID unknown band (FM|AM|WX|CB|LORA|BROWSE|ADSB|P25)");
      return;
    }
    if (band != RtlBand::adsb && !rtl_device_ready()) {
      Serial.println("RTL_TUNE_UNAVAILABLE device not ready");
      return;
    }
    queue_local_rtl_listen(band, static_cast<uint32_t>(freq_hz));
    Serial.printf("RTL_TUNE_OK band=%s frequency_hz=%lu\n", rtl_band_name(band), freq_hz);
    return;
  }
  if (strcmp(command, "RTL_FREQ") == 0) {
    Serial.printf("RTL_FREQ_STATUS band=%s frequency_hz=%u mode=%s\n",
                  rtl_band_name(rtl_ui_band), rtl_ui_frequency_hz,
                  rtl_mode_name(rtl_ui_band));
    return;
  }
  if (strncmp(command, "RTL_FREQ ", 9) == 0 && authenticated) {
    const unsigned long freq_hz = strtoul(command + 9, nullptr, 10);
    if (freq_hz == 0) {
      Serial.println("RTL_FREQ_INVALID usage: RTL_FREQ <HZ>");
      return;
    }
    request_hot_retune(static_cast<uint32_t>(freq_hz));
    Serial.printf("RTL_FREQ_OK band=%s frequency_hz=%lu\n", rtl_band_name(rtl_ui_band), freq_hz);
    return;
  }
  if (strcmp(command, "RTL_VOLUME") == 0) {
    Serial.printf("RTL_VOLUME_STATUS volume=%u\n", rtl_ui_volume);
    return;
  }
  if (strcmp(command, "RTL_SOUND") == 0) {
    Serial.printf("RTL_SOUND_STATUS enabled=%d\n",
                  rtl_audio_enabled.load(std::memory_order_relaxed) ? 1 : 0);
    return;
  }
  if (strcmp(command, "RTL_SOUND ON") == 0 || strcmp(command, "RTL_SOUND OFF") == 0) {
    const bool enable = command[10] == 'O' && command[11] == 'N';
    set_rtl_audio_user_enabled(enable);
    Serial.printf("RTL_SOUND_OK enabled=%d\n", enable ? 1 : 0);
    return;
  }
  if (strcmp(command, "RTL_AUDIO_TEST STATUS") == 0) {
    rtl_audio_test_emit_status();
    return;
  }
  if (strcmp(command, "RTL_AUDIO_TEST TONE") == 0) {
    rtl_audio_test_start_tone();
    return;
  }
  if (strcmp(command, "RTL_AUDIO_TEST FM") == 0) {
    rtl_audio_test_start_fm();
    return;
  }
  if (strcmp(command, "RTL_AUDIO_TEST STOP") == 0) {
    rtl_audio_test_stop();
    return;
  }
  if (strcmp(command, "RTL_SIGNAL") == 0) {
    const int signal_tenths = static_cast<int>(lroundf(rtl_signal_dbfs_smooth * 10.0f));
    const int left_tenths = static_cast<int>(
        lroundf(rtl_audio_left_dbfs.load(std::memory_order_relaxed) * 10.0f));
    const int right_tenths = static_cast<int>(
        lroundf(rtl_audio_right_dbfs.load(std::memory_order_relaxed) * 10.0f));
    const int rds_tenths = static_cast<int>(
        lroundf(rtl_rds_signal_dbfs.load(std::memory_order_relaxed) * 10.0f));
    Serial.printf(
        "RTL_SIGNAL_STATUS band=%s frequency_hz=%u signal_dbfs_tenths=%d "
        "stereo_locked=%d left_dbfs_tenths=%d right_dbfs_tenths=%d "
        "rds_carrier=%d rds_signal_tenths=%d\n",
        rtl_band_name(rtl_ui_band), rtl_ui_frequency_hz, signal_tenths,
        rtl_stereo_locked.load(std::memory_order_relaxed) ? 1 : 0,
        left_tenths, right_tenths,
        rtl_rds_carrier_present.load(std::memory_order_relaxed) ? 1 : 0,
        rds_tenths);
    return;
  }
  if (strcmp(command, "RTL_PRESET_SCAN") == 0 && authenticated) {
    if (rtl_ui_band != RtlBand::fm) {
      Serial.println("RTL_PRESET_SCAN_INVALID FM band only");
      return;
    }
    rtl_fm_preset_scan_requested.store(true, std::memory_order_relaxed);
    Serial.println("RTL_PRESET_SCAN_QUEUED");
    return;
  }
  if (strcmp(command, "RTL_PRESET_LIST") == 0) {
    Serial.printf("RTL_PRESET_LIST_BEGIN count=%d\n", fm_preset_count);
    for (int i = 0; i < fm_preset_count; ++i) {
      Serial.printf("RTL_PRESET %d frequency_hz=%u level=%.1f\n", i + 1,
                    fm_presets[i].freq_hz, static_cast<double>(fm_presets[i].level_dbfs));
    }
    Serial.println("RTL_PRESET_LIST_END");
    return;
  }
  if (strncmp(command, "RTL_PRESET_TUNE ", 17) == 0 && authenticated) {
    const int index = atoi(command + 17) - 1;
    if (index < 0 || index >= fm_preset_count) {
      Serial.println("RTL_PRESET_TUNE_INVALID index out of range");
      return;
    }
    queue_local_rtl_listen(RtlBand::fm, fm_presets[index].freq_hz);
    persist_fm_frequency(fm_presets[index].freq_hz);
    Serial.printf("RTL_PRESET_TUNE_OK index=%d frequency_hz=%u\n", index + 1,
                  fm_presets[index].freq_hz);
    return;
  }
  if (strcmp(command, "RTL_RDS_CAPTURE_START") == 0) {
    (void)rds_capture_start();
    return;
  }
  if (strcmp(command, "RTL_RDS_CAPTURE_STOP") == 0 ||
      strcmp(command, "RTL_RDS_CAPTURE_SAVE") == 0) {
    (void)rds_capture_stop_and_export();
    return;
  }
  if (strcmp(command, "RTL_RDS_CAPTURE_STATUS") == 0) {
    rds_capture_status_print();
    return;
  }
  if (strncmp(command, "RTL_RDS_REPLAY ", 15) == 0) {
    (void)rds_replay(command + 15);
    return;
  }
  if (strcmp(command, "RTL_RDS_STATUS") == 0) {
    const RdsSelection selection = rds_select();
    const RdsHypothesis& best = *selection.best;
    rtl_sdr_v4_esp_metrics_t metrics{};
    if (g_rtl != nullptr) (void)rtl_sdr_v4_esp_get_metrics(g_rtl, &metrics);
    const float bler = best.total_blocks > 0
                           ? 100.0f * (1.0f - static_cast<float>(best.good_blocks) /
                                                   static_cast<float>(best.total_blocks))
                           : 100.0f;
    Serial.printf(
        "RDS_STATUS carrier=%d carrier_signal=%.1f block_locked=%d bler=%.1f%% "
        "good=%lu total=%lu hyp0_streak=%d hyp1_streak=%d "
        "timing_chip_rate=%.2f timing_correction_ppm=%.1f "
        "nco_freq_off=%.6f i_lpf=%.2f q_lpf=%.2f mu=%.3f "
        "A=%04x B=%04x C=%04x D=%04x driver_overruns=%u driver_drops=%u "
        "effective_sps=%u audio_chunks=%u audio_drops=%u\n",
        rtl_rds_carrier_present.load(std::memory_order_relaxed) ? 1 : 0,
        static_cast<double>(rtl_rds_signal_dbfs.load(std::memory_order_relaxed)),
        selection.locked, static_cast<double>(bler),
        static_cast<unsigned long>(best.good_blocks),
        static_cast<unsigned long>(best.total_blocks), selection.parity_streak[0],
        selection.parity_streak[1],
        static_cast<double>(kRdsChipInc * kRdsMpxRateHz),
        static_cast<double>(kRdsChipRateCorrectionPpm),
        0.0,
        static_cast<double>(rtl_audio.rds_i_lpf),
        static_cast<double>(rtl_audio.rds_q_lpf),
        static_cast<double>(selection.chip_phase),
        best.group_info[0], best.group_info[1], best.group_info[2], best.group_info[3],
        metrics.overruns, metrics.consumer_drops, metrics.effective_sps,
        rtl_audio.queued_chunks, rtl_audio.dropped_chunks);
    return;
  }

  if (strncmp(command, "PAIR ", 5) == 0) {
    uint8_t candidate[sizeof(pairing_key)];
    if (!decode_hex(command + 5, candidate, sizeof(candidate))) {
      Serial.println("PAIR_INVALID");
      return;
    }
    if (!paired) {
      memcpy(pairing_key, candidate, sizeof(pairing_key));
      preferences.putBytes("pair_key", pairing_key, sizeof(pairing_key));
      paired = true;
      append_journal("paired");
    }
    uint8_t difference = 0;
    for (size_t index = 0; index < sizeof(pairing_key); ++index) {
      difference |= pairing_key[index] ^ candidate[index];
    }
    Serial.println(difference == 0 ? "PAIR_OK" : "PAIR_LOCKED");
    return;
  }
  if (strncmp(command, "AUTH ", 5) == 0 && paired) {
    uint8_t nonce[16];
    uint8_t host_proof[32];
    uint8_t expected_host_proof[32];
    uint8_t signature[32];
    char* proof_hex = strchr(command + 5, ' ');
    if (proof_hex == nullptr) {
      Serial.println("AUTH_INVALID");
      return;
    }
    *proof_hex++ = '\0';
    if (!decode_hex(command + 5, nonce, sizeof(nonce)) ||
        !decode_hex(proof_hex, host_proof, sizeof(host_proof))) {
      Serial.println("AUTH_INVALID");
      return;
    }
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    uint8_t host_input[sizeof("host") - 1 + sizeof(nonce)];
    memcpy(host_input, "host", sizeof("host") - 1);
    memcpy(host_input + sizeof("host") - 1, nonce, sizeof(nonce));
    if (info == nullptr ||
        mbedtls_md_hmac(info, pairing_key, sizeof(pairing_key), host_input,
                        sizeof(host_input), expected_host_proof) != 0) {
      Serial.println("AUTH_ERROR");
      return;
    }
    uint8_t difference = 0;
    for (size_t index = 0; index < sizeof(host_proof); ++index) {
      difference |= host_proof[index] ^ expected_host_proof[index];
    }
    if (difference != 0) {
      Serial.println("AUTH_DENIED");
      return;
    }
    uint8_t device_input[sizeof("device") - 1 + sizeof(nonce)];
    memcpy(device_input, "device", sizeof("device") - 1);
    memcpy(device_input + sizeof("device") - 1, nonce, sizeof(nonce));
    if (mbedtls_md_hmac(info, pairing_key, sizeof(pairing_key), device_input,
                        sizeof(device_input), signature) != 0) {
      Serial.println("AUTH_ERROR");
      return;
    }
    Serial.print("AUTH_OK ");
    print_hex(signature, sizeof(signature));
    Serial.println();
    authenticated = true;
    offline_transition_handled = false;
    last_ping_ms = millis();
    set_online();
    return;
  }
  if (strcmp(command, "PING") == 0 && authenticated) {
    last_ping_ms = millis();
    return;
  }
  if (strncmp(command, "PREPARE_FLASH ", 14) == 0 && authenticated) {
    uint8_t digest[32];
    if (!decode_hex(command + 14, digest, sizeof(digest))) {
      Serial.println("FLASH_INVALID");
      return;
    }
    draw_session_state("Approved firmware update starting", TFT_CYAN);
    append_journal("firmware_approved");
    Serial.print("FLASH_READY ");
    print_hex(digest, sizeof(digest));
    Serial.println();
    emit_identity();
    emit_pending_journal();
    return;
  }
  if (strncmp(command, "SET_WIFI ", 9) == 0 && authenticated) {
    char* ssid_hex = command + 9;
    char* password_hex = strchr(ssid_hex, ' ');
    if (password_hex == nullptr) {
      Serial.println("WIFI_INVALID");
      return;
    }
    *password_hex++ = '\0';
    char* signature_text = strchr(password_hex, ' ');
    if (signature_text == nullptr) {
      Serial.println("WIFI_INVALID");
      return;
    }
    *signature_text++ = '\0';
    char candidate_ssid[sizeof(wifi_ssid)];
    char candidate_password[sizeof(wifi_password)];
    uint8_t signature[32];
    char signed_value[208];
    if (strlen(ssid_hex) > 2 * (sizeof(candidate_ssid) - 1) ||
        strlen(password_hex) > 2 * (sizeof(candidate_password) - 1) ||
        strlen(signature_text) != 2 * sizeof(signature)) {
      Serial.println("WIFI_INVALID");
      return;
    }
    snprintf(signed_value, sizeof(signed_value), "wifi|%s|%s", ssid_hex, password_hex);
    if (!decode_hex_text(ssid_hex, candidate_ssid, sizeof(candidate_ssid)) ||
        candidate_ssid[0] == '\0' ||
        !decode_hex_text(password_hex, candidate_password, sizeof(candidate_password)) ||
        !decode_hex(signature_text, signature, sizeof(signature)) ||
        !hmac_matches(signed_value, signature)) {
      Serial.println("WIFI_INVALID");
      return;
    }
    strlcpy(wifi_ssid, candidate_ssid, sizeof(wifi_ssid));
    strlcpy(wifi_password, candidate_password, sizeof(wifi_password));
    preferences.putString("wifi_ssid", wifi_ssid);
    preferences.putString("wifi_pass", wifi_password);
    strlcpy(wifi_profiles[0].ssid, wifi_ssid, sizeof(wifi_profiles[0].ssid));
    strlcpy(wifi_profiles[0].password, wifi_password, sizeof(wifi_profiles[0].password));
    if (wifi_profile_count == 0) wifi_profile_count = 1;
    preferences.putString("wifi0_ssid", wifi_profiles[0].ssid);
    preferences.putString("wifi0_pass", wifi_profiles[0].password);
    wifi_configured = true;
    append_journal("wifi_configured");
    Serial.println("WIFI_CONFIGURED");
    start_wifi_connection();
    emit_identity();
    emit_pending_journal();
    return;
  }
  if (strncmp(command, "INSTALL_STATUS ", 15) == 0 && authenticated) {
    char* revision_text = command + 15;
    char* max_runs_text = strchr(revision_text, ' ');
    if (max_runs_text == nullptr) {
      Serial.println("WORKFLOW_INVALID");
      return;
    }
    *max_runs_text++ = '\0';
    char* signature_text = strchr(max_runs_text, ' ');
    if (signature_text == nullptr) {
      Serial.println("WORKFLOW_INVALID");
      return;
    }
    *signature_text++ = '\0';
    const uint32_t revision = strtoul(revision_text, nullptr, 10);
    const uint32_t max_runs = strtoul(max_runs_text, nullptr, 10);
    uint8_t signature[32];
    char signed_value[48];
    snprintf(signed_value, sizeof(signed_value), "workflow|%lu|%lu",
             static_cast<unsigned long>(revision), static_cast<unsigned long>(max_runs));
    if (revision == 0 || max_runs == 0 || max_runs > UINT16_MAX ||
        !decode_hex(signature_text, signature, sizeof(signature)) ||
        !hmac_matches(signed_value, signature)) {
      Serial.println("WORKFLOW_INVALID");
      return;
    }
    if (revision <= workflow.config_revision) {
      Serial.println("WORKFLOW_STALE");
      return;
    }
    workflow.config_revision = revision;
    workflow.max_runs = static_cast<uint16_t>(max_runs);
    workflow.runs = 0;
    persist_workflow();
    append_journal("workflow_installed");
    Serial.printf("WORKFLOW_OK %lu\n", static_cast<unsigned long>(revision));
    emit_identity();
    emit_pending_journal();
    return;
  }
  if (strncmp(command, "ROTATE_KEY ", 11) == 0 && authenticated) {
    char* key_text = command + 11;
    char* signature_text = strchr(key_text, ' ');
    if (signature_text == nullptr) {
      Serial.println("ROTATE_INVALID");
      return;
    }
    *signature_text++ = '\0';
    uint8_t replacement[sizeof(pairing_key)];
    uint8_t signature[32];
    char signed_value[80];
    if (strlen(key_text) != 2 * sizeof(replacement) ||
        strlen(signature_text) != 2 * sizeof(signature)) {
      Serial.println("ROTATE_INVALID");
      return;
    }
    snprintf(signed_value, sizeof(signed_value), "rotate|%s", key_text);
    if (!decode_hex(key_text, replacement, sizeof(replacement)) ||
        !decode_hex(signature_text, signature, sizeof(signature)) ||
        !hmac_matches(signed_value, signature)) {
      Serial.println("ROTATE_INVALID");
      return;
    }
    memcpy(pairing_key, replacement, sizeof(pairing_key));
    preferences.putBytes("pair_key", pairing_key, sizeof(pairing_key));
    append_journal("credential_rotated");
    Serial.println("KEY_ROTATED");
    authenticated = false;
    offline_transition_handled = false;
    last_ping_ms = millis();
    draw_session_state("Credential rotated - host reconnect required", TFT_YELLOW);
    return;
  }
  if (strcmp(command, "TEST_PRESSURE") == 0 && authenticated) {
    for (int index = 0; index < 10; ++index) append_journal("pressure_test");
    Serial.printf("PRESSURE_OK %lu\n", static_cast<unsigned long>(journal.dropped_events));
    emit_identity();
    emit_pending_journal();
    return;
  }
  if (strcmp(command, "PRESSURE_ACK") == 0 && authenticated) {
    journal.dropped_events = 0;
    persist_journal();
    Serial.println("PRESSURE_CLEARED");
    emit_identity();
    return;
  }
  if (strncmp(command, "ACK ", 4) == 0 && authenticated) {
    const uint32_t sequence = strtoul(command + 4, nullptr, 10);
    if (sequence <= journal.next_sequence) acknowledge_journal(sequence);
  }
}

void poll_serial() {
  while (Serial.available() > 0) {
    const char value = static_cast<char>(Serial.read());
    if (value == '\n') {
      serial_input[serial_input_length] = '\0';
      if (serial_input_length > 0) process_command(serial_input);
      serial_input_length = 0;
    } else if (value != '\r' && serial_input_length < sizeof(serial_input) - 1) {
      serial_input[serial_input_length++] = value;
    }
  }
}

void emit_power_sample(const char* tag) {
  Serial.printf(
      "POWER_SAMPLE tag=%s uptime_ms=%lu battery_mv=%d battery_ma=%ld battery_pct=%ld "
      "charging=%s\n",
      tag ? tag : "sample", static_cast<unsigned long>(millis()),
      M5.Power.getBatteryVoltage(), static_cast<long>(M5.Power.getBatteryCurrent()),
      static_cast<long>(M5.Power.getBatteryLevel()), charging_state());
}

void begin_power_monitor(const char* tag, uint32_t duration_ms) {
  strlcpy(power_monitor_tag, tag ? tag : "action", sizeof(power_monitor_tag));
  power_monitor_until_ms = millis() + duration_ms;
  power_monitor_next_ms = millis() + 200;
  emit_power_sample(power_monitor_tag);
}

void service_power_monitor() {
  if (!power_monitor_until_ms || static_cast<int32_t>(millis() - power_monitor_until_ms) >= 0) {
    power_monitor_until_ms = 0;
    return;
  }
  if (static_cast<int32_t>(millis() - power_monitor_next_ms) < 0) return;
  power_monitor_next_ms = millis() + 200;
  emit_power_sample(power_monitor_tag);
}

const char* boot_init_stage_name(BootInitStage stage) {
  switch (stage) {
    case BootInitStage::usb_power_off: return "usb_power_off";
    case BootInitStage::usb_power_settle: return "usb_power_settle";
    case BootInitStage::rtl_enumerating: return "rtl_enumerating";
    case BootInitStage::speaker_settle: return "speaker_settle";
    case BootInitStage::ready: return "ready";
    default: return "idle";
  }
}

void set_boot_init_stage(BootInitStage stage) {
  boot_init_stage = stage;
  boot_init_stage_started_ms = millis();
  Serial.printf("BOOT_STAGE %s\n", boot_init_stage_name(stage));
  begin_power_monitor(boot_init_stage_name(stage));
}

void begin_boot_device_staging() {
  boot_auto_start_allowed = false;
  M5.Power.setExtOutput(false, m5::ext_USB);
  set_rtl_sdr_status("Boot: USB-A rail disabled");
  set_boot_init_stage(BootInitStage::usb_power_off);
}

void service_boot_device_staging() {
  const uint32_t elapsed_ms = millis() - boot_init_stage_started_ms;
  switch (boot_init_stage) {
    case BootInitStage::usb_power_off:
      if (elapsed_ms < 150) return;
      M5.Power.setExtOutput(true, m5::ext_USB);
      set_rtl_sdr_status("Boot: USB-A rail settling");
      set_boot_init_stage(BootInitStage::usb_power_settle);
      return;
    case BootInitStage::usb_power_settle:
      if (elapsed_ms < 350) return;
      set_rtl_sdr_status("Boot: starting RTL-SDR host");
      initialize_rtl_sdr_host();
      set_boot_init_stage(BootInitStage::rtl_enumerating);
      return;
    case BootInitStage::rtl_enumerating:
      if (!rtl_device_ready()) {
        if (elapsed_ms >= 8000) {
          Serial.println("BOOT_RTL_TIMEOUT no_device");
          boot_auto_start_allowed = true;
          set_boot_init_stage(BootInitStage::ready);
        }
        return;
      }
      sync_rtl_audio_for_band(rtl_ui_band);
      allow_boot_speaker();
      set_boot_init_stage(BootInitStage::speaker_settle);
      return;
    case BootInitStage::speaker_settle:
      if (elapsed_ms < 300) return;
      boot_auto_start_allowed = true;
      set_boot_init_stage(BootInitStage::ready);
      return;
    default:
      return;
  }
}
}  // namespace

void orcsdr_splash_poll_serial(void) {
  poll_serial();
}

void setup() {
  Serial.begin(115200);

  auto config = M5.config();
  M5.begin(config);
  // Tab5 codec/amp remains owned by M5Unified. Configure its one worker before first begin.
  auto speaker_config = M5.Speaker.config();
  speaker_config.sample_rate = 48000;
  speaker_config.stereo = true;
  // Speaker_Class allocates its mixing buffer on spk_task's stack. 256 frames
  // leaves too little stack on Tab5; 512 retains a safe task stack margin.
  speaker_config.dma_buf_len = 512;
  speaker_config.dma_buf_count = 8;
  speaker_config.task_priority = 6;
  speaker_config.task_pinned_core = 1;
  M5.Speaker.config(speaker_config);
  M5.Display.setRotation(1);
  M5.Display.setBrightness(180);
  if (!orcsdr::adsb::self_check() || !orcsdr::adsb_rx::Decoder::self_check()) {
    Serial.println("RTL_ADSB_SELF_CHECK_FAIL");
    abort();
  }
  Serial.println("RTL_ADSB_SELF_CHECK_OK");
  if (!orcsdr::fm::self_check()) {
    Serial.println("RTL_FM_DASHBOARD_SELF_CHECK_FAIL");
    abort();
  }
  Serial.println("RTL_FM_DASHBOARD_SELF_CHECK_OK");
  if (!orcsdr::fmconfig::self_check()) {
    Serial.println("RTL_FM_CONFIG_SELF_CHECK_FAIL");
    abort();
  }
  Serial.println("RTL_FM_CONFIG_SELF_CHECK_OK");
  if (!orcsdr::p25::self_check()) {
    Serial.println("RTL_P25_DASHBOARD_SELF_CHECK_FAIL");
    abort();
  }
  Serial.println("RTL_P25_DASHBOARD_SELF_CHECK_OK");
  if (!orcsdr::p25decoder::self_check()) {
    Serial.println("RTL_P25_DECODER_SELF_CHECK_FAIL");
    abort();
  }
  Serial.println("RTL_P25_DECODER_SELF_CHECK_OK");
  if (!p25_voice_self_check()) {
    Serial.println("RTL_P25_VOICE_SELF_CHECK_FAIL");
    abort();
  }
  Serial.println("RTL_P25_VOICE_SELF_CHECK_OK");
  if (!orcsdr::settings::self_check()) {
    Serial.println("ORC_SETTINGS_SELF_CHECK_FAIL");
    abort();
  }
  Serial.println("ORC_SETTINGS_SELF_CHECK_OK");
  if (!orcsdr::catalog::self_check()) {
    Serial.println("ORC_CATALOG_SELF_CHECK_FAIL");
    abort();
  }
  Serial.println("ORC_CATALOG_SELF_CHECK_OK");
  orcsdr::catalog::begin(nullptr);
  if (!orcsdr::home::self_check()) {
    Serial.println("ORC_HOME_SELF_CHECK_FAIL");
    abort();
  }
  Serial.println("ORC_HOME_SELF_CHECK_OK");
  if (!ui_doc_self_check()) {
    Serial.println("UI_DOC_SELF_CHECK_FAIL");
    abort();
  }
  Serial.println("UI_DOC_SELF_CHECK_OK");

#if ORC_LORA_TEST_BUILD
  g_suppress_home_paint = true;
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
  M5.Display.setTextSize(3);
  M5.Display.drawString("LORA TEST", M5.Display.width() / 2, 260);
  M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.drawString("Starting RTL-SDR...", M5.Display.width() / 2, 315);

  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_BASE);
  snprintf(node_id, sizeof(node_id), "m5tab5_%02x%02x%02x%02x%02x%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  load_state();
  const bool test_sd_ready = ensure_tab5_sd();
  initialize_rtl_sdr_host();

  const uint32_t device_deadline = millis() + 15000;
  while (!rtl_device_ready() && static_cast<int32_t>(device_deadline - millis()) > 0) {
    M5.update();
    poll_serial();
    delay(10);
  }
  g_suppress_home_paint = false;
  if (rtl_device_ready()) {
    set_orc_tool(OrcTool::Radio);
    queue_local_rtl_listen(RtlBand::lora, kLoraDefaultHz);
    Serial.printf("LORA_TEST_READY frequency_hz=%u sf=11 bw=250000 trigger_margin_db=%.1f sd=%s\n",
                  kLoraDefaultHz, static_cast<double>(kLoraTriggerMarginDb),
                  test_sd_ready ? "ready" : "missing");
  } else {
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_RED, TFT_BLACK);
    M5.Display.drawString("RTL-SDR NOT FOUND", M5.Display.width() / 2, 315);
    Serial.println("LORA_TEST_ERROR rtl_sdr_not_found");
  }
  append_journal("lora_test_boot");
  last_ping_ms = millis();
  offline_transition_handled = true;
  return;
#else

  /*
   * Loading splash owns the display while dependencies come up.
   * Status pill updates each boot step; the ready button gates entry to home.
   */
  g_suppress_home_paint = true;
  (void)orcsdr_splash_begin();

  orcsdr_splash_set_status("Loading device identity…");
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_BASE);
  snprintf(node_id, sizeof(node_id), "m5tab5_%02x%02x%02x%02x%02x%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  orcsdr_splash_set_status("Loading saved settings…");
  load_state();

  orcsdr_splash_set_status("Starting RTL-SDR USB host…");
  begin_boot_device_staging();

  /* Dependencies are up: reveal the gate while the background keeps looping. */
  orcsdr_splash_set_ready(true);
  /* Skipped for now (autonomous RDS dev cycle: flash -> reboot -> resume
   * FM reception with no touch input) — the splash previously blocked
   * indefinitely on a screen tap, so every post-flash reboot sat looping
   * the animation forever instead of restoring the last band. Flip back
   * to (void)orcsdr_splash_wait_start(); to restore the tap gate. */
  constexpr bool kSkipSplashGate = true;
  if (!kSkipSplashGate) (void)orcsdr_splash_wait_start();
  orcsdr_splash_end();
  g_suppress_home_paint = false;

  // "OrcLink" branded home screen removed — with the auto-start above,
  // it was showing as a second splash-like gate between the boot splash
  // and the radio UI actually appearing, which is exactly what it's not
  // supposed to do anymore.
  show_home();
  append_journal("boot");
  last_ping_ms = millis();
  offline_transition_handled = !paired;
  draw_session_state(paired ? "Waiting for authenticated host" : "Unpaired - USB provisioning",
                     paired ? TFT_YELLOW : TFT_ORANGE);
  draw_power_state();
#endif
}

void loop() {
  const bool adsb_ui = rtl_ui_band == RtlBand::adsb && orcsdr::adsb::active();
  const bool radio_ui = rtl_ui_active.load(std::memory_order_acquire);
  const bool settings_ui = orcsdr::settings::active();
  // Single-owner rule: while radio UI streams, only the USB task may M5.update().
  // Dual M5.update() (loop + stream) was correlated with total speaker silence.
  if (!radio_ui || adsb_ui) {
    M5.update();
  }
  poll_serial();
  if (ui_documentation_mode) {
    delay(10);
    return;
  }
  rtl_audio_test_service();
  service_boot_device_staging();
  service_power_monitor();
  if (boot_auto_start_allowed) poll_wifi();
  if (fm_config_save_pending.exchange(false, std::memory_order_acq_rel)) {
    char error[64]{};
    const auto config = fm_config_from_runtime();
    if (ensure_tab5_sd() && g_sd_fs != nullptr &&
        orcsdr::fmconfig::save(*g_sd_fs, config, error, sizeof(error))) {
      Serial.printf("RTL_FM_CONFIG_SAVE frequency_hz=%lu presets=%u\n",
                    static_cast<unsigned long>(config.startup_frequency_hz),
                    static_cast<unsigned>(config.preset_count));
    } else {
      Serial.printf("RTL_FM_CONFIG_ERROR detail=\"%s\"\n", error);
    }
  }
  if (p25_config_save_pending.exchange(false, std::memory_order_acq_rel)) {
    char error[64]{};
    if (ensure_tab5_sd() && g_sd_fs != nullptr &&
        orcsdr::p25config::save(*g_sd_fs, p25_config, error, sizeof(error))) {
      strlcpy(p25_config_status, "P25.cfg saved", sizeof(p25_config_status));
      Serial.printf("RTL_P25_CONFIG_SAVE control_hz=%lu\n",
                    static_cast<unsigned long>(p25_config.last_control_channel_hz));
    } else {
      snprintf(p25_config_status, sizeof(p25_config_status), "P25.cfg save: %.46s", error);
      Serial.printf("RTL_P25_CONFIG_ERROR detail=\"%s\"\n", p25_config_status);
    }
    ++p25_config_revision;
  }
  if (adsb_settings_persist_pending.exchange(false, std::memory_order_acq_rel)) {
    preferences.putBool("adsb_loc_set", adsb_settings.location_configured);
    preferences.putInt("adsb_lat_e7", adsb_settings.latitude_e7);
    preferences.putInt("adsb_lon_e7", adsb_settings.longitude_e7);
    preferences.putUShort("adsb_range", adsb_settings.radar_range_nm);
    Serial.printf("RTL_ADSB_SETTINGS_SAVE configured=%d range_nm=%u\n",
                  adsb_settings.location_configured ? 1 : 0,
                  adsb_settings.radar_range_nm);
  }
  uint32_t audio_persist_due =
      rtl_audio_settings_persist_due_ms.load(std::memory_order_acquire);
  if (audio_persist_due != 0 &&
      static_cast<int32_t>(millis() - audio_persist_due) >= 0 &&
      rtl_audio_settings_persist_due_ms.compare_exchange_strong(
          audio_persist_due, 0, std::memory_order_acq_rel)) {
    const uint8_t volume = rtl_live_volume.load(std::memory_order_acquire);
    const bool sound_enabled = rtl_audio_user_enabled.load(std::memory_order_acquire);
    if (preferences.getUChar("set_volume", kRtlVolumeDefault) != volume)
      preferences.putUChar("set_volume", volume);
    if (preferences.getBool("set_sound", true) != sound_enabled)
      preferences.putBool("set_sound", sound_enabled);
    settings_sound_default = sound_enabled;
    Serial.printf("RTL_AUDIO_SETTINGS_SAVE volume=%u sound=%d\n", volume,
                  sound_enabled ? 1 : 0);
  }

  const uint32_t current_rtl_sdr_status_revision =
      rtl_sdr_status_revision.load(std::memory_order_acquire);
  if (drawn_rtl_sdr_status_revision != current_rtl_sdr_status_revision) {
    drawn_rtl_sdr_status_revision = current_rtl_sdr_status_revision;
    if (!radio_ui && !adsb_ui && !settings_ui) draw_rtl_sdr_state();
  }

  if (settings_ui && (adsb_ui || !radio_ui)) {
    static uint32_t settings_last_update_ms = 0;
    if (millis() - settings_last_update_ms >= 500) {
      settings_last_update_ms = millis();
      orcsdr::settings::update(global_settings_state());
    }
    const auto touch = M5.Touch.getDetail(0);
    const bool pressed = touch.isPressed() || touch.wasPressed();
    if (pressed && !was_pressed) handle_global_settings_touch(touch.x, touch.y);
    was_pressed = pressed;
  } else if (adsb_ui) {
    enrich_one_adsb_track();
    publish_adsb_snapshot(millis());
    orcsdr::adsb::update();
    const auto touch = M5.Touch.getDetail(0);
    const bool pressed = touch.isPressed() || touch.wasPressed();
    if (pressed && !was_pressed) handle_sdr_touch(touch.x, touch.y);
    was_pressed = pressed;
  // Home-screen taps when radio UI is not active.
  } else if (!radio_ui) {
    // One-shot auto-start once the RTL-SDR is ready, restoring the band the
    // device was last on instead of waiting for a home-screen tap (that tap
    // gate was blocking every autonomous flash-reboot-resume cycle during
    // headless/serial-driven development; see phasing.md's RDS Phase 5).
    static bool auto_start_done = false;
    if (!auto_start_done && boot_auto_start_allowed && settings_auto_start_reception && !wifi_scan_running &&
        rtl_device_ready()) {
      auto_start_done = true;
      queue_local_rtl_listen(rtl_ui_band, rtl_ui_frequency_hz);
      // The whole demod chain (mono, stereo, RDS) only runs when audio is
      // enabled -- see the EVT_IQ_BLOCK gate in the streaming task. Without
      // this, RDS status stays at floor forever regardless of how strong
      // the RF signal is, since demodulate_fm() itself never executes.
    }
    const auto touch = M5.Touch.getDetail(0);
    if (orcsdr::home::active()) {
      handle_home_action(orcsdr::home::handle_touch(touch.x, touch.y,
                                                     touch.isPressed()));
      static uint32_t home_last_update_ms = 0;
      if (millis() - home_last_update_ms >= 500) {
        home_last_update_ms = millis();
        orcsdr::home::update(home_dashboard_snapshot());
      }
    } else {
      const bool pressed = touch.isPressed() || touch.wasPressed();
      if (pressed && !was_pressed) {
        if (touch.x >= 1211 && touch.x < 1269 && touch.y >= 8 && touch.y < 66) {
          open_global_settings(orcsdr::settings::Section::connectivity);
        } else if (point_in_button(touch.x, touch.y)) {
          if (rtl_device_ready()) queue_local_rtl_listen(RtlBand::fm, rtl_saved_fm_hz);
          else emit_touch(touch.x, touch.y);
        }
      }
      was_pressed = pressed;
    }
  }

  const uint32_t now = millis();
  if (!rtl_ui_active.load(std::memory_order_acquire) &&
      now - last_power_draw_ms >= 10000) {
    last_power_draw_ms = now;
    draw_power_state();
  }
  if (!offline_transition_handled && now - last_ping_ms > kSessionTimeoutMs) {
    authenticated = false;
    offline_transition_handled = true;
    if (!rtl_ui_active.load(std::memory_order_acquire)) {
      draw_session_state("Host offline - local journal active", TFT_ORANGE);
    }
    append_journal("session_degraded");
    run_offline_workflow();
  }
  if (authenticated && now - last_heartbeat_ms >= 2000) {
    last_heartbeat_ms = now;
    ++heartbeat_sequence;
    Serial.printf(
        "{\"type\":\"heartbeat\",\"message_id\":\"m5tab5_heartbeat_%llu\","
        "\"protocol_version\":{\"major\":1,\"minor\":0},"
        "\"payload\":{\"node_id\":\"%s\",\"sequence\":%llu,"
        "\"uptime_ms\":%u,\"free_heap_bytes\":%u,\"journal_pending\":%u}}\n",
        static_cast<unsigned long long>(heartbeat_sequence), node_id,
        static_cast<unsigned long long>(heartbeat_sequence), now, ESP.getFreeHeap(),
        journal.count);
  }

  delay(10);
}
