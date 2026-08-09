#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_intr_alloc.h>
#include <esp_heap_caps.h>
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>
#include <driver/usb_serial_jtag.h>
#include <usb/usb_helpers.h>
#include <usb/usb_host.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdlib>
#include <cstring>

#if !defined(RTL_USE_LEGACY_USB)
#define RTL_USE_LEGACY_USB 0
#endif

#include "rtl_sdr_v4_transfers.h"
#include "orcsdr_splash.hpp"
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
/* Demod staging (~one URB). playRaw is fed from a larger batch buffer. */
constexpr size_t kRtlAudioBufferSamples = 2048;
/* ~25 ms batches at 48 kHz — fewer playRaw calls, more stable codec feed. */
constexpr size_t kRtlAudioPlayBatchSamples = 1200;
constexpr size_t kRtlAudioPlayBufferSamples = 4096;
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
 * Scope FFT size. 256 bins @ 960 kS/s ≈ 3.75 kHz/bin (was 128 / 7.5 kHz).
 * Welch multi-window average runs only when GFX is on and audio is not stressed.
 */
constexpr size_t kRtlSpectrumBins = 256;
/** Average this many non-overlapping windows for a quieter, more precise trace. */
constexpr size_t kRtlSpectrumWelchWindows = 2;
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
constexpr char kLoraDashboardPath[] = "/orcsdr/lora_dashboard_384x470.jpg";
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
// Clean-room LO offset: LO = RF + 1.814972 MHz (from 100 MHz observation).
constexpr double kRtlIfOffsetHz = 1814972.0;
constexpr double kRtlXtalHz = 28800000.0;

enum class RtlBand : uint8_t { fm, am, wx, cb, lora, browse };
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
    {462550000, 467725000, 462562500, RtlBand::browse, "FRS / GMRS", "UHF / personal two-way", false},
    {kLoraMinHz, kLoraMaxHz, kLoraDefaultHz, RtlBand::lora, "LORA / ISM", "UHF / LoRa CSS and mesh data", true},
    {977900000, 978100000, 978000000, RtlBand::browse, "ADS-B UAT", "UHF / aircraft position", false},
    {1089900000, 1090100000, 1090000000, RtlBand::browse, "ADS-B / MODE S", "L-band / aircraft tracking", true},
    {1525000000, 1559000000, 1545000000, RtlBand::browse, "SATCOM", "L-band / satellite downlinks", true},
    {1575000000, 1576000000, 1575420000, RtlBand::browse, "GNSS / GPS", "L-band / navigation", false},
    {1610600000, 1626500000, 1620000000, RtlBand::browse, "SATCOM", "L-band / mobile satellite", false},
};
static_assert(std::size(kRfBandGuide) == 20);
constexpr const char* kRfQuickLabels[] = {
    "CB 27", "HAM 10M", "HAM 6M", "FM RADIO", "AIRBAND", "NOAA SAT",
    "HAM 2M", "NOAA WX", "HAM 70CM", "LORA 915", "ADS-B 1090", "SATCOM L"};
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
};

RtlAudioState rtl_audio;

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
}
int16_t rtl_audio_buffers[3][kRtlAudioBufferSamples];
/* Batch small demod bursts into longer playRaw chunks (reduces chop). */
static int16_t rtl_audio_play_batch[kRtlAudioPlayBufferSamples];
static size_t rtl_audio_play_count = 0;
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
static char lora_last_message[96] = "";
static uint32_t lora_last_sender = 0;
static uint32_t lora_last_packet_id = 0;
static uint32_t lora_last_message_ms = 0;
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
static bool g_sd_tried = false;
static char g_audio_rec_last_path[64] = "";
static std::atomic<uint8_t> g_orc_tool{static_cast<uint8_t>(OrcTool::Radio)};
static std::atomic<bool> g_audio_rec_export_pending{false};

/* ---- Raw CU8 IQ capture and adaptive LoRa energy trigger ---- */
constexpr size_t kIqRecSeconds = 3;
constexpr size_t kIqRecMaxBytes = kRtlSampleRateSps * 2u * kIqRecSeconds;
constexpr size_t kLoraPreRollBytes = kRtlSampleRateSps / 2u;  // 250 ms CU8 IQ
static uint8_t* g_iq_rec_buf = nullptr;
static uint8_t* g_lora_pre_roll_buf = nullptr;
static std::atomic<size_t> g_iq_rec_write{0};
static std::atomic<bool> g_iq_rec_active{false};
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
bool offline_transition_handled = false;
Preferences preferences;
JournalState journal{};
WorkflowState workflow{};
uint8_t pairing_key[32];
char serial_input[320];
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
uint8_t g_sd_put_chunk[kSdPutChunkBytes];
bool wifi_station_ready = false;
bool wifi_scan_running = false;
bool wifi_configured = false;
bool wifi_connected = false;
int wifi_network_count = -1;
char wifi_ssid[33]{};
char wifi_password[64]{};
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
std::atomic<bool> rtl_audio_enabled{false};
std::atomic<bool> rtl_volume_changed{false};
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
uint64_t rtl_capture_bytes = 0;
uint8_t rtl_capture_min = 0;
uint8_t rtl_capture_max = 0;
double rtl_capture_mean = 0;
char rtl_capture_sha256[65]{};
char rtl_capture_error[64] = "not run";

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
bool handle_lora_touch(int32_t x, int32_t y);
void draw_rf_band_guide(uint32_t frequency_hz);
int spectrum_draw_width();
void redraw_spectrum_panel();
void draw_sdr_controls(RtlBand band, bool running);
void handle_sdr_touch(int32_t x, int32_t y);
void poll_sdr_touch_from_stream();
void request_hot_retune(uint32_t frequency_hz);
void queue_local_rtl_listen(RtlBand band, uint32_t frequency_hz);
void draw_sdr_screen(RtlBand band, uint32_t frequency_hz, uint8_t volume);
void draw_nav_panel();
bool handle_nav_touch(int32_t x, int32_t y);
void spectrum_offer_iq_snapshot(const uint8_t* iq, size_t bytes);
bool audio_rec_ensure_buffer();
bool audio_rec_start();
bool audio_rec_stop_and_export();
void audio_rec_append(const int16_t* samples, size_t count);
void audio_rec_status_print();
bool ensure_tab5_sd();
const char* orc_tool_name(OrcTool tool);
OrcTool orc_tool_current();
void set_orc_tool(OrcTool tool);
void draw_tool_tabs();
void draw_capture_tool_panel();
bool handle_tool_tab_touch(int32_t x, int32_t y);

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
  if (g_suppress_home_paint) return;
  M5.Display.fillRect(250, 210, 780, 55, TFT_BLACK);
  M5.Display.setTextColor(color, TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(2);
  M5.Display.drawString(message, 640, 237);
}

void draw_wifi_state() {
  if (g_suppress_home_paint) return;
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
  if (g_suppress_home_paint) return;
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
    default: return "FM";
  }
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
    default: return rtl_saved_fm_hz;
  }
}

uint32_t rtl_filter_default_hz(RtlBand band) {
  if (band == RtlBand::lora) return lora_bandwidth_hz.load(std::memory_order_relaxed);
  if (band == RtlBand::am || band == RtlBand::cb) return kRtlAmFilterDefaultHz;
  if (band == RtlBand::wx || band == RtlBand::browse) return kRtlWxFilterDefaultHz;
  return kRtlFmFilterDefaultHz;
}

uint32_t rtl_clamp_filter_hz(RtlBand band, uint32_t bandwidth_hz) {
  if (band == RtlBand::lora) {
    if (bandwidth_hz <= 93750) return 62500;
    if (bandwidth_hz <= 187500) return 125000;
    if (bandwidth_hz <= 375000) return 250000;
    return 500000;
  }
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
  Serial.printf("RTL_FM_SAVE frequency_hz=%u\n", frequency_hz);
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
  static bool dma_configured = false;
  if (!dma_configured) {
    // Deep DMA queue: absorbs retune gaps and spectrum draws without underruns.
    auto cfg = M5.Speaker.config();
    cfg.dma_buf_count = 24;
    cfg.dma_buf_len = 512;
    M5.Speaker.config(cfg);
    dma_configured = true;
  }
  apply_speaker_volume(volume);
  if (!M5.Speaker.isEnabled()) return false;
  if (!M5.Speaker.isRunning() && !M5.Speaker.begin()) return false;
  apply_speaker_volume(volume);
  return M5.Speaker.isRunning() || M5.Speaker.isEnabled();
}

void flush_audio_play_batch(bool force) {
  if (rtl_audio_play_count == 0) return;
  if (!force && rtl_audio_play_count < kRtlAudioPlayBatchSamples) return;
  if (rtl_volume_changed.exchange(false, std::memory_order_acq_rel)) {
    apply_speaker_volume(rtl_live_volume.load(std::memory_order_acquire));
  }
  if (!M5.Speaker.isRunning()) {
    ensure_speaker_running(rtl_live_volume.load(std::memory_order_acquire));
  }
  /* Capture the exact PCM going to the speaker (post AGC/limiter). */
  if (g_audio_rec_active.load(std::memory_order_acquire)) {
    audio_rec_append(rtl_audio_play_batch, rtl_audio_play_count);
  }
  if (!rtl_audio_enabled.load(std::memory_order_acquire)) {
    rtl_audio_play_count = 0;
    return;
  }
  if (M5.Speaker.playRaw(rtl_audio_play_batch, rtl_audio_play_count, 48000, false, 1, 0,
                         false)) {
    ++rtl_audio.queued_chunks;
  } else {
    ++rtl_audio.dropped_chunks;
    if ((rtl_audio.dropped_chunks % 32) == 1) {
      Serial.printf("RTL_AUDIO_DROP chunks_ok=%u dropped=%u peak=%d volume=%u running=%s\n",
                    rtl_audio.queued_chunks, rtl_audio.dropped_chunks, rtl_audio.peak,
                    rtl_live_volume.load(std::memory_order_acquire),
                    M5.Speaker.isRunning() ? "true" : "false");
    }
  }
  rtl_audio_play_count = 0;
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
  SPI.begin(kTab5SdSckPin, kTab5SdMisoPin, kTab5SdMosiPin, kTab5SdCsPin);
  /* 10 MHz is safer on long-ish Tab5 SPI than 25 MHz during concurrent USB. */
  if (!SD.begin(kTab5SdCsPin, SPI, 10000000)) {
    Serial.println("RTL_REC_SD missing_or_fail");
    g_sd_ready = false;
    return false;
  }
  g_sd_ready = true;
  Serial.println("RTL_REC_SD ready");
  return true;
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
    g_iq_rec_export_pending.store(true, std::memory_order_release);
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
  const float trigger = constrain(g_lora_noise_floor_dbfs + 9.0f, -78.0f, -25.0f);
  lora_noise_dbfs.store(g_lora_noise_floor_dbfs, std::memory_order_relaxed);
  lora_trigger_dbfs.store(trigger, std::memory_order_relaxed);
  if (level < trigger - 3.0f) {
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
  SD.mkdir("/orcsdr");
  ++g_iq_rec_file_seq;
  char path[96];
  snprintf(path, sizeof(path), "/orcsdr/iq_%03u_%u_sf%u_bw%u.orciq",
           static_cast<unsigned>(g_iq_rec_file_seq), g_iq_rec_frequency_hz,
           static_cast<unsigned>(g_iq_rec_sf),
           static_cast<unsigned>(g_iq_rec_bandwidth_hz));
  if (SD.exists(path) && !SD.remove(path)) return finish(false);
  File file = SD.open(path, FILE_WRITE, true);
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
  SD.mkdir("/orcsdr");
  // FILE_WRITE appends; sequence numbers restart after boot, so replace collisions.
  if (SD.exists(path) && !SD.remove(path)) {
    Serial.printf("RTL_REC_WAV_ERR replace path=%s\n", path);
    return false;
  }
  File f = SD.open(path, FILE_WRITE, true);
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
  draw_rf_band_guide(rtl_ui_frequency_hz);
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
  snprintf(label, sizeof(label), "STEP: %u kHz  v", step / 1000u);
  button(780, 325, 416, 48, label, TFT_DARKCYAN, 3);

  if (rtl_nav_dropdown == SdrNavDropdown::Pinch) {
    button(780, 385, 200, 58, "SPAN", TFT_NAVY, 3);
    button(996, 385, 200, 58, "FILTER", TFT_DARKCYAN, 3);
    return;
  }
  if (rtl_nav_dropdown == SdrNavDropdown::Step) {
    static const uint32_t steps[] = {1000, 5000, 10000, 50000, 100000, 1000000};
    for (int index = 0; index < 6; ++index) {
      snprintf(label, sizeof(label), "%u kHz", steps[index] / 1000u);
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
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  M5.Display.drawString("tap=tune  drag=pan  yellow edges=filter", 988, 530);
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
      const RtlCaptureState state = rtl_capture_state.load(std::memory_order_acquire);
      if (state == RtlCaptureState::running && rtl_ui_band == entry->mode) {
        request_hot_retune(entry->preset_hz);
      } else {
        queue_local_rtl_listen(entry->mode, entry->preset_hz);
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
  File directory = SD.open("/orcsdr");
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

void sd_get_abort(const char* reason) {
  if (g_sd_get.file) g_sd_get.file.close();
  if (g_sd_get.active) mbedtls_sha256_free(&g_sd_get.sha);
  g_sd_get = {};
  Serial.printf("SD_GET_ERROR %s\n", reason ? reason : "aborted");
}

void sd_get_begin(const char* path_hex) {
  if (g_sd_get.active || g_sd_put.active) {
    Serial.println("SD_GET_ERROR transfer_busy");
    return;
  }
  if (sd_transfer_radio_busy()) {
    Serial.println("SD_GET_ERROR radio_busy");
    return;
  }
  char path[sizeof(g_sd_get.path)];
  if (!decode_hex_text(path_hex, path, sizeof(path)) || !sd_put_path_allowed(path)) {
    Serial.println("SD_GET_ERROR invalid_path");
    return;
  }
  if (orcsdr_splash_is_active()) orcsdr_splash_end();
  if (!ensure_tab5_sd()) {
    Serial.println("SD_GET_ERROR sd_unavailable");
    return;
  }
  g_sd_get.file = SD.open(path, FILE_READ);
  if (!g_sd_get.file || g_sd_get.file.isDirectory()) {
    g_sd_get = {};
    Serial.println("SD_GET_ERROR open_failed");
    return;
  }
  g_sd_get.size = g_sd_get.file.size();
  if (g_sd_get.size == 0 || g_sd_get.size > kSdPutMaxBytes) {
    g_sd_get.file.close();
    g_sd_get = {};
    Serial.println("SD_GET_ERROR invalid_size");
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
  g_sd_get = {};
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
  if (!SD.exists(path)) {
    Serial.printf("SD_REMOVE_MISSING path=\"%s\"\n", path);
    return;
  }
  Serial.printf(SD.remove(path) ? "SD_REMOVE_DONE path=\"%s\"\n"
                                : "SD_REMOVE_ERROR remove_failed path=\"%s\"\n",
                path);
}

void sd_put_abort(const char* reason) {
  if (g_sd_put.file) g_sd_put.file.close();
  if (g_sd_put.temporary[0]) SD.remove(g_sd_put.temporary);
  if (g_sd_put.active) mbedtls_sha256_free(&g_sd_put.sha);
  g_sd_put = {};
  Serial.printf("SD_PUT_ERROR %s\n", reason ? reason : "aborted");
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
    SD.remove(g_sd_put.temporary);
    Serial.println("SD_PUT_ERROR sha_mismatch");
    g_sd_put = {};
    return false;
  }

  char backup[136];
  snprintf(backup, sizeof(backup), "%s.bak", g_sd_put.target);
  SD.remove(backup);
  const bool had_target = SD.exists(g_sd_put.target);
  if (had_target && !SD.rename(g_sd_put.target, backup)) {
    SD.remove(g_sd_put.temporary);
    Serial.println("SD_PUT_ERROR backup_failed");
    g_sd_put = {};
    return false;
  }
  if (!SD.rename(g_sd_put.temporary, g_sd_put.target)) {
    if (had_target) (void)SD.rename(backup, g_sd_put.target);
    SD.remove(g_sd_put.temporary);
    Serial.println("SD_PUT_ERROR rename_failed");
    g_sd_put = {};
    return false;
  }
  if (had_target) SD.remove(backup);
  Serial.printf("SD_PUT_DONE bytes=%llu sha256=",
                static_cast<unsigned long long>(g_sd_put.received));
  print_hex(digest, sizeof(digest));
  Serial.printf(" path=\"%s\"\n", g_sd_put.target);
  g_sd_put = {};
  return true;
}

void sd_put_begin(char* arguments) {
  if (g_sd_put.active || g_sd_get.active) {
    Serial.println("SD_PUT_ERROR already_active");
    return;
  }
  if (sd_transfer_radio_busy()) {
    Serial.println("SD_PUT_ERROR radio_busy");
    return;
  }
  char* sha_text = strchr(arguments, ' ');
  if (sha_text == nullptr) {
    Serial.println("SD_PUT_ERROR invalid_begin");
    return;
  }
  *sha_text++ = '\0';
  char* path_hex = strchr(sha_text, ' ');
  if (path_hex == nullptr) {
    Serial.println("SD_PUT_ERROR invalid_begin");
    return;
  }
  *path_hex++ = '\0';
  char target[sizeof(g_sd_put.target)];
  const uint64_t size = strtoull(arguments, nullptr, 10);
  uint8_t digest[32];
  if (size == 0 || size > kSdPutMaxBytes || !decode_hex(sha_text, digest, sizeof(digest)) ||
      !decode_hex_text(path_hex, target, sizeof(target)) || !sd_put_path_allowed(target)) {
    Serial.println("SD_PUT_ERROR invalid_begin");
    return;
  }
  /* Stop the SD-backed loading animation before taking exclusive card ownership. */
  if (orcsdr_splash_is_active()) orcsdr_splash_end();
  if (!ensure_tab5_sd() || (!SD.exists("/orcsdr") && !SD.mkdir("/orcsdr"))) {
    Serial.println("SD_PUT_ERROR sd_unavailable");
    return;
  }

  strlcpy(g_sd_put.target, target, sizeof(g_sd_put.target));
  snprintf(g_sd_put.temporary, sizeof(g_sd_put.temporary), "%s.part", target);
  SD.remove(g_sd_put.temporary);
  g_sd_put.file = SD.open(g_sd_put.temporary, FILE_WRITE);
  if (!g_sd_put.file) {
    g_sd_put = {};
    Serial.println("SD_PUT_ERROR open_failed");
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
  snprintf(line, sizeof(line), "buffered: %.2f / %u s   samples=%u   rate=%u",
           static_cast<double>(sec), static_cast<unsigned>(kAudioRecMaxSeconds),
           static_cast<unsigned>(samples), kAudioRecRateHz);
  M5.Display.drawString(line, kSpectrumX + 16, panel_y + 72);
  snprintf(line, sizeof(line), "LO meta: %s  %u Hz", rtl_band_name(g_audio_rec_band),
           g_audio_rec_freq_hz);
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
  M5.Display.fillRect(0, kSdrBandY - 6, 1280, 720 - (kSdrBandY - 6), TFT_BLACK);
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
      {0, 220, band == RtlBand::lora ? "DATA MODE" : sound_on ? "SOUND ON" : "SOUND OFF",
       static_cast<uint32_t>(band == RtlBand::lora ? TFT_DARKCYAN
                                                   : sound_on ? TFT_DARKGREEN : TFT_MAROON)},
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
  static bool s_chrome_drawn = false;

  const int fill_w = static_cast<int>(t * (kBarW - 2));
  const int db_i = static_cast<int>(lroundf(rtl_signal_dbfs_smooth));

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
  }

  if (fill_w != s_last_fill_w) {
    /* Clear track interior then paint fill — avoids full-strip SPI every tick. */
    M5.Display.fillRect(kBarX + 1, kBarY + 1, kBarW - 2, kBarH - 2, TFT_BLACK);
    if (fill_w > 0) {
      uint32_t color = TFT_GREEN;
      if (t > 0.85f) color = TFT_RED;
      else if (t > 0.65f) color = TFT_YELLOW;
      M5.Display.fillRect(kBarX + 1, kBarY + 1, fill_w, kBarH - 2, color);
    }
    s_last_fill_w = fill_w;
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
  M5.Display.drawString(label, 1100, 48);
  draw_tool_tabs();
}

void draw_cb_dashboard(bool static_panel) {
  if (rtl_ui_band != RtlBand::cb || rtl_nav_open) return;
  if (static_panel) {
    const bool image_ok = ensure_tab5_sd() && SD.exists(kCbDashboardPath) &&
                          M5.Display.drawJpgFile(SD, kCbDashboardPath,
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

void draw_lora_dashboard(bool static_panel) {
  if (rtl_ui_band != RtlBand::lora || rtl_nav_open) return;
  if (static_panel) {
    const bool image_ok = ensure_tab5_sd() && SD.exists(kLoraDashboardPath) &&
                          M5.Display.drawJpgFile(SD, kLoraDashboardPath,
                                                 kCbPanelX, kCbPanelY);
    if (!image_ok) {
      M5.Display.fillRoundRect(kCbPanelX, kCbPanelY, kCbPanelWidth,
                               kCbPanelHeight, 10, 0x2104);
      M5.Display.drawRoundRect(kCbPanelX, kCbPanelY, kCbPanelWidth,
                               kCbPanelHeight, 10, TFT_CYAN);
      M5.Display.fillRoundRect(kCbPanelX + 70, kCbPanelY + 183, 244, 58,
                               8, TFT_BLACK);
    }
    M5.Display.setTextDatum(middle_center);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_WHITE);
    constexpr const char* labels[] = {"F-", "F+", "BW", "SF", "IQ CAP", "NAV"};
    for (size_t i = 0; i < std::size(labels); ++i) {
      M5.Display.drawString(labels[i], kCbPanelX + 51 + static_cast<int>(i) * 56,
                            kCbPanelY + 426);
    }
  }

  const float trigger = lora_trigger_dbfs.load(std::memory_order_relaxed);
  const bool above = rtl_signal_dbfs_smooth >= trigger;
  M5.Display.fillRect(kCbPanelX + 75, kCbPanelY + 187, 234, 48, TFT_BLACK);
  char message[sizeof(lora_last_message)];
  uint32_t sender = 0;
  uint32_t received_ms = 0;
  portENTER_CRITICAL(&lora_message_mux);
  strlcpy(message, lora_last_message, sizeof(message));
  sender = lora_last_sender;
  received_ms = lora_last_message_ms;
  portEXIT_CRITICAL(&lora_message_mux);
  if (message[0] != '\0') {
    constexpr size_t kPageChars = 36;
    constexpr size_t kLineChars = 18;
    const size_t length = strlen(message);
    const size_t pages = (length + kPageChars - 1) / kPageChars;
    const size_t page = ((millis() - received_ms) / 3000u) % pages;
    const size_t start = page * kPageChars;
    char line1[kLineChars + 1]{};
    char line2[kLineChars + 1]{};
    strlcpy(line1, message + start, sizeof(line1));
    if (start + kLineChars < length) {
      strlcpy(line2, message + start + kLineChars, sizeof(line2));
    }
    char source[44];
    snprintf(source, sizeof(source), "HOST CRC+KEY OK !%08lx %u/%u",
             static_cast<unsigned long>(sender), static_cast<unsigned>(page + 1),
             static_cast<unsigned>(pages));
    M5.Display.setTextDatum(middle_center);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
    M5.Display.drawString(source, kCbPanelX + 192, kCbPanelY + 193);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.drawString(line1, kCbPanelX + 192, kCbPanelY + 209);
    M5.Display.drawString(line2, kCbPanelX + 192, kCbPanelY + 226);
    return;
  }
  char text[48];
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
  snprintf(text, sizeof(text), "%.3f MHz", rtl_ui_frequency_hz / 1000000.0);
  M5.Display.drawString(text, kCbPanelX + 192, kCbPanelY + 198);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_MAGENTA, TFT_BLACK);
  snprintf(text, sizeof(text), "SF%u  BW%uk  CR4/5",
           static_cast<unsigned>(lora_sf.load(std::memory_order_relaxed)),
           static_cast<unsigned>(lora_bandwidth_hz.load(std::memory_order_relaxed) / 1000));
  M5.Display.drawString(text, kCbPanelX + 192, kCbPanelY + 216);
  M5.Display.setTextColor(above ? TFT_GREEN : TFT_LIGHTGREY, TFT_BLACK);
  const size_t captured = g_iq_rec_write.load(std::memory_order_relaxed);
  snprintf(text, sizeof(text), "AUTO %s EVT%lu %s %u%%",
           lora_detector_enabled.load(std::memory_order_relaxed) ? "ON" : "OFF",
           static_cast<unsigned long>(lora_rf_events.load(std::memory_order_relaxed)),
           g_iq_rec_active.load(std::memory_order_relaxed) ? "CAP" : "SEARCH",
           static_cast<unsigned>((captured * 100u) / kIqRecMaxBytes));
  M5.Display.drawString(text, kCbPanelX + 192, kCbPanelY + 229);
}

void draw_sdr_screen(RtlBand band, uint32_t frequency_hz, uint8_t volume) {
  if (!rtl_spectrum_window_ready) {
    constexpr float kPi = 3.14159265358979323846f;
    for (size_t index = 0; index < kRtlSpectrumBins; ++index) {
      rtl_spectrum_window[index] =
          0.5f - 0.5f * cosf(2.0f * kPi * index / (kRtlSpectrumBins - 1));
    }
    rtl_spectrum_window_ready = true;
  }
  M5.Display.fillScreen(TFT_BLACK);
  draw_sdr_header(band, frequency_hz, volume);
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
void draw_spectrum(const uint8_t* iq, size_t bytes) {
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
    rtl_spectrum_levels[bin] = rtl_spectrum_smooth[bin];
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
    Serial.printf("RTL_SPECTRUM_FPS fps=%u bins=%u welch=%u tool=%s audio_dropped=%u "
                  "audio_chunks=%u audio_peak=%d dsp_load_pct=%u dsp_blocks=%u "
                  "dsp_block_us_max=%u\n",
                  rtl_spectrum_fps, static_cast<unsigned>(kRtlSpectrumBins),
                  static_cast<unsigned>(windows), orc_tool_name(tool),
                  rtl_audio.dropped_chunks, rtl_audio.queued_chunks, rtl_audio.peak,
                  dsp_load_pct, dsp_blocks, dsp_max_us);
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
  constexpr float kLimit = 12000.0f;
  const float normalized = x / kLimit;
  const float normalized_sq = normalized * normalized;
  if (normalized_sq < 9.0f) {
    x = kLimit * normalized * (27.0f + normalized_sq) /
        (27.0f + 9.0f * normalized_sq);
  } else {
    x = x < 0.0f ? -kLimit : kLimit;
  }
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
  /* Append into batch buffer; flush ~20 ms blocks for smoother codec feed. */
  for (size_t i = 0; i < audio_count; ++i) {
    if (rtl_audio_play_count >= kRtlAudioPlayBufferSamples) {
      flush_audio_play_batch(true);
    }
    rtl_audio_play_batch[rtl_audio_play_count++] = audio[i];
  }
  flush_audio_play_batch(false);
  rtl_audio.buffer = (rtl_audio.buffer + 1) % std::size(rtl_audio_buffers);
}

/**
 * FM / NFM polar demod at kRtlSampleRateSps.
 * wbfm=true: broadcast mono path (channel LPF + 75 µs de-emphasis).
 * wbfm=false: NFM/WX — tighter audio LPF, light de-emphasis only.
 * No blanker / heavy post-LPF (those muffled on prior A/B).
 */
void demodulate_fm(const uint8_t* iq, size_t bytes, float audio_scale, bool wbfm) {
  int16_t* audio = rtl_audio_buffers[rtl_audio.buffer];
  size_t audio_count = 0;
  const float iq_lpf_k = rtl_filter_alpha(wbfm ? RtlBand::fm : RtlBand::wx);
  const float audio_lpf_k = wbfm ? kWbfmAudioLpfK : kNfmAudioLpfK;
  const float deemph_k = wbfm ? kWbfmDeemphK : kNfmDeemphK;
  const float inv_audio_decim = 1.0f / static_cast<float>(kFmAudioDecim);

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
      /* Post-discriminator mono audio LPF, then boxcar to 48 kHz. */
      rtl_audio.channel_filter += audio_lpf_k * (phase - rtl_audio.channel_filter);
      rtl_audio.audio_sum += rtl_audio.channel_filter;
      if (++rtl_audio.audio_phase == kFmAudioDecim) {
        const float demodulated = rtl_audio.audio_sum * inv_audio_decim;
        rtl_audio.audio_sum = 0;
        rtl_audio.audio_phase = 0;
        rtl_audio.deemphasis += deemph_k * (demodulated - rtl_audio.deemphasis);
        rtl_audio.dc += 0.0008f * (rtl_audio.deemphasis - rtl_audio.dc);
        const int16_t sample =
            shape_audio_sample(rtl_audio.deemphasis - rtl_audio.dc, audio_scale);
        audio[audio_count++] = sample;
        const int16_t magnitude = sample < 0 ? -sample : sample;
        if (magnitude > rtl_audio.peak) rtl_audio.peak = magnitude;
        rtl_audio.square_sum += static_cast<uint32_t>(sample * sample);
        ++rtl_audio.samples;
      }
    }
    rtl_audio.previous_i = i;
    rtl_audio.previous_q = q;
    rtl_audio.have_previous = true;
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
      update_signal_level_from_iq(iq->data, n);
      /* The callback owns this borrowed block until return; avoid a full-URB copy. */
      spectrum_offer_iq_snapshot(iq->data, n);
      if (g_stream_band == RtlBand::lora) lora_iq_offer(iq->data, n);
      if (g_stream_band != RtlBand::lora &&
          (rtl_audio_enabled.load(std::memory_order_relaxed) ||
           g_audio_rec_active.load(std::memory_order_relaxed))) {
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
      rtl_signal_dbfs.store(-90.0f, std::memory_order_relaxed);
      rtl_signal_dbfs_smooth = -80.0f;
      rtl_signal_meter_last_ms = 0;
      if (band == RtlBand::lora) lora_iq_reset_detector();
      rtl_capture_state.store(RtlCaptureState::running, std::memory_order_release);
      rtl_ui_active.store(true, std::memory_order_release);
      set_rtl_sdr_status("RTL-SDR V4: continuous listening (driver)");
      draw_sdr_screen(band, frequency_hz, volume);
      M5.Speaker.stop();
      if (rtl_audio_enabled.load(std::memory_order_acquire)) {
        delay(20);
        (void)ensure_speaker_running(volume);
      }

      rtl_sdr_v4_esp_stream_config_t st;
      rtl_sdr_v4_esp_stream_config_default(&st);
      st.preset = RTL_SDR_V4_ESP_PRESET_CUSTOM_HZ;
      st.frequency_hz = frequency_hz;
      st.sample_rate_sps = RTL_SDR_V4_ESP_RATE_960K;
      esp_err_t err = rtl_sdr_v4_esp_start(g_rtl, &st);
      Serial.printf("RTL_START %s\n", rtl_sdr_v4_esp_err_to_name(err));
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
        uint32_t spectrum_last_ms = 0;
        uint32_t last_lo_applied_hz = frequency_hz;
        uint32_t last_lo_apply_ms = 0;
        bool auto_fm_scanning = false;
        uint32_t auto_fm_frequency_hz = kRtlFmMinHz + kRtlFmAutoStepHz / 2;
        uint32_t auto_fm_sample_at_ms = 0;
        uint32_t auto_fm_best_hz = frequency_hz;
        float auto_fm_best_level = -120.0f;
        while (!rtl_stop_requested.load(std::memory_order_acquire) &&
               g_rtl_device_ready.load(std::memory_order_acquire)) {
          /* Touch + hot retune on this task (not in IQ callback): responsive STOP/FREQ. */
          poll_sdr_touch_from_stream();

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
            draw_sdr_controls(g_stream_band, true);
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
              draw_sdr_controls(g_stream_band, true);
              Serial.printf("RTL_AUTO_FM done frequency_hz=%u level=%.1f\n",
                            auto_fm_best_hz, static_cast<double>(auto_fm_best_level));
            } else {
              auto_fm_frequency_hz += kRtlFmAutoStepHz;
              reset_spectrum_renderer();
              request_hot_retune(auto_fm_frequency_hz);
              auto_fm_sample_at_ms = now_retune + kRtlFmAutoSettleMs;
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
          if (ui_revision != drawn_rtl_ui_revision) {
            drawn_rtl_ui_revision = ui_revision;
            draw_sdr_header(g_stream_band, rtl_ui_frequency_hz,
                            rtl_live_volume.load(std::memory_order_acquire));
          }

          const uint32_t now = millis();
          /* SIG meter stays on even with GFX off (antenna peaking). */
          if (now - rtl_signal_meter_last_ms >= kRtlSignalMeterIntervalMs) {
            rtl_signal_meter_last_ms = now;
            draw_signal_meter(false);
            draw_cb_dashboard(false);
            draw_lora_dashboard(false);
          }
          /* Auto-export WAV after buffer fills (never write SD on the IQ callback). */
          if (g_audio_rec_export_pending.exchange(false, std::memory_order_acq_rel)) {
            (void)audio_rec_stop_and_export();
            if (orc_tool_current() == OrcTool::Capture) draw_capture_tool_panel();
            draw_sdr_controls(g_stream_band, true);
          }
          if (g_iq_rec_export_pending.exchange(false, std::memory_order_acq_rel)) {
            (void)iq_rec_stop_and_export();
            draw_lora_dashboard(false);
          }
          const bool gfx_on = rtl_graphics_enabled.load(std::memory_order_acquire);
          if (gfx_on && orc_tool_current() != OrcTool::Capture) {
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
        (void)rtl_sdr_v4_esp_stop(g_rtl, 2000);
        rtl_capture_state.store(RtlCaptureState::complete, std::memory_order_release);
        set_rtl_sdr_status("RTL-SDR V4: stopped");
        /* Keep rtl_ui_active true so home/power does not paint over SDR controls. */
        draw_sdr_controls(g_stream_band, false);
        Serial.printf("RTL_STOP bytes=%llu\n",
                      static_cast<unsigned long long>(rtl_capture_bytes));
      }
      if (rtl_restart_requested.exchange(false, std::memory_order_acq_rel) &&
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
  M5.Power.setExtOutput(true, m5::ext_USB);
  set_rtl_sdr_status("RTL-SDR: USB-A power enabled");
  delay(200);

  rtl_sdr_v4_esp_config_t cfg;
  rtl_sdr_v4_esp_config_default(&cfg);
  cfg.event_cb = on_rtl_driver_event;
  /*
   * Fewer, larger URBs → fewer demod wakeups, longer continuous audio runs.
   * 3 × 32 KiB is the measured continuous-listen profile from Tab5.
   */
  cfg.transfer_bytes = 32768;
  cfg.transfer_count = 3;
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
  if (xTaskCreatePinnedToCore(rtl_driver_app_task, "rtl_app", 8192, nullptr, kRtlAppTaskPrio,
                              nullptr, 1) != pdPASS) {
    set_rtl_sdr_status("RTL-SDR: app task failed");
  }
  Serial.println("RTL_CORE_SPLIT usb=core0 iq_demod+ui=core1 (inline demod)");
}
#endif /* RTL_USE_LEGACY_USB */

void initialize_wifi() {
  delay(1500);
  WiFi.setPins(kWifiClockPin, kWifiCommandPin, kWifiData0Pin, kWifiData1Pin,
               kWifiData2Pin, kWifiData3Pin, kWifiResetPin);
  wifi_station_ready = WiFi.mode(WIFI_STA);
  draw_wifi_state();
}

void start_wifi_inventory() {
  if (!wifi_station_ready || wifi_configured) return;
  wifi_network_count = WiFi.scanNetworks(true, true);
  wifi_scan_running = wifi_network_count == WIFI_SCAN_RUNNING;
  if (!wifi_scan_running) WiFi.scanDelete();
  draw_wifi_state();
}

void start_wifi_connection() {
  if (!wifi_station_ready || !wifi_configured) return;
  if (wifi_scan_running) WiFi.scanDelete();
  wifi_scan_running = false;
  wifi_network_count = -1;
  wifi_connected = false;
  WiFi.begin(wifi_ssid, wifi_password);
  draw_wifi_state();
}

void poll_wifi() {
  bool state_changed = false;
  if (wifi_scan_running) {
    const int result = WiFi.scanComplete();
    if (result != WIFI_SCAN_RUNNING) {
      wifi_scan_running = false;
      wifi_network_count = result;
      if (result >= 0) WiFi.scanDelete();
      state_changed = true;
    }
  }
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected != wifi_connected) {
    wifi_connected = connected;
    state_changed = true;
  }
  if (state_changed) {
    draw_wifi_state();
    if (authenticated) emit_identity();
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
  char message[sizeof(lora_last_message)];
  if (!parse_hex_u32_exact(sender_text, &sender) ||
      !parse_hex_u32_exact(packet_text, &packet_id) ||
      !decode_hex_text(message_hex, message, sizeof(message)) || message[0] == '\0') {
    return false;
  }
  for (char* p = message; *p; ++p) {
    const unsigned char value = static_cast<unsigned char>(*p);
    if (value < 0x20 || value == 0x7f) *p = ' ';
  }
  portENTER_CRITICAL(&lora_message_mux);
  strlcpy(lora_last_message, message, sizeof(lora_last_message));
  lora_last_sender = sender;
  lora_last_packet_id = packet_id;
  lora_last_message_ms = millis();
  portEXIT_CRITICAL(&lora_message_mux);
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
  wifi_configured = !stored_ssid.isEmpty() && stored_ssid.length() <= 32 &&
                    stored_password.length() <= 63;
  if (wifi_configured) {
    stored_ssid.toCharArray(wifi_ssid, sizeof(wifi_ssid));
    stored_password.toCharArray(wifi_password, sizeof(wifi_password));
  }
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
  rtl_ui_frequency_hz = rtl_saved_fm_hz;
  rtl_requested_frequency_hz.store(rtl_saved_fm_hz, std::memory_order_release);
  Serial.printf("RTL_FM_LOAD frequency_hz=%u\n", rtl_saved_fm_hz);
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

void queue_local_rtl_listen(RtlBand band, uint32_t frequency_hz) {
#if RTL_USE_LEGACY_USB
  if (rtl_sdr_device == nullptr) return;
#else
  if (!g_rtl_device_ready.load(std::memory_order_acquire) || g_rtl == nullptr) return;
#endif
  frequency_hz = rtl_clamp_frequency(band, frequency_hz);
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
    rtl_audio_enabled.store(false, std::memory_order_relaxed);
    rtl_audio_play_count = 0;
    M5.Speaker.stop();
  }
  if (band == RtlBand::fm) {
    persist_fm_frequency(frequency_hz);
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
  append_journal(band == RtlBand::am       ? "sdr_am"
                 : band == RtlBand::wx     ? "sdr_wx"
                 : band == RtlBand::cb     ? "sdr_cb"
                 : band == RtlBand::lora   ? "sdr_lora"
                 : band == RtlBand::browse ? "sdr_browse"
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
  // NVS journal writes block for milliseconds; skip while the live stream owns
  // the audio path so VOL taps stay responsive and do not glitch the speaker.
  if (!rtl_ui_active.load(std::memory_order_acquire)) {
    append_journal("sdr_volume");
  }
}

bool point_in_scope(int32_t x, int32_t y) {
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
  if (rtl_ui_band != RtlBand::lora || x < kCbPanelX ||
      x >= kCbPanelX + kCbPanelWidth || y < kCbPanelY ||
      y >= kCbPanelY + kCbPanelHeight) return false;
  const int dx = x - (kCbPanelX + 192);
  const int dy = y - (kCbPanelY + 317);
  if (dx * dx + dy * dy <= 78 * 78) {
    constexpr float kPi = 3.14159265358979323846f;
    const float turn = (atan2f(static_cast<float>(dy), static_cast<float>(dx)) + kPi) /
                       (2.0f * kPi);
    lora_sf.store(static_cast<uint8_t>(7 + min(static_cast<int>(turn * 6.0f), 5)),
                  std::memory_order_relaxed);
    draw_lora_dashboard(false);
    return true;
  }
  if (y >= kCbPanelY + 395 && y <= kCbPanelY + 458) {
    const int control = constrain((x - (kCbPanelX + 23)) / 56, 0, 5);
    if (control == 0 || control == 1) {
      const uint32_t next = rtl_step_frequency(RtlBand::lora, rtl_ui_frequency_hz,
                                               control == 0 ? -1 : 1);
      if (rtl_capture_state.load(std::memory_order_acquire) == RtlCaptureState::running) {
        request_hot_retune(next);
      } else {
        queue_local_rtl_listen(RtlBand::lora, next);
      }
    } else if (control == 2) {
      uint32_t bandwidth = lora_bandwidth_hz.load(std::memory_order_relaxed);
      bandwidth = bandwidth == 125000 ? 250000 : bandwidth == 250000 ? 500000 : 125000;
      lora_bandwidth_hz.store(bandwidth, std::memory_order_relaxed);
      rtl_filter_bandwidth_hz.store(bandwidth, std::memory_order_relaxed);
      redraw_spectrum_panel();
    } else if (control == 3) {
      uint8_t sf = lora_sf.load(std::memory_order_relaxed);
      lora_sf.store(sf >= 12 ? 7 : sf + 1, std::memory_order_relaxed);
    } else if (control == 4) {
      if (g_iq_rec_active.load(std::memory_order_acquire)) {
        (void)iq_rec_stop_and_export();
      } else {
        (void)iq_rec_start();
      }
    } else {
      rtl_nav_open = true;
      draw_nav_panel();
    }
    draw_lora_dashboard(false);
    return true;
  }
  return true;
}

void request_hot_retune(uint32_t frequency_hz) {
  if (rtl_ui_band == RtlBand::wx) return;
  frequency_hz = rtl_clamp_frequency(rtl_ui_band, frequency_hz);
  if (frequency_hz == 0) return;
  /* UI: 1 kHz display quantize. */
  const uint32_t ui_hz = (frequency_hz / 1000u) * 1000u;
  /* LO: 5 kHz — avoids thrashing bulk/EP0. */
  const uint32_t lo_hz =
      (frequency_hz / kRtlHotRetuneQuantHz) * kRtlHotRetuneQuantHz;
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
  if (handle_tool_tab_touch(x, y)) return;
  if (handle_cb_touch(x, y)) return;
  if (handle_lora_touch(x, y)) return;

  static constexpr int kBandWidths[] = {110, 110, 110, 120, 140, 160, 170, 200};
  static constexpr int kTuneWidths[] = {170, 170, 220, 150, 150, 220};
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
    if (rtl_ui_band == RtlBand::lora) return;
    const bool next = !rtl_audio_enabled.load(std::memory_order_acquire);
    rtl_audio_enabled.store(next, std::memory_order_release);
    rtl_audio_play_count = 0;
    if (next) {
      (void)ensure_speaker_running(rtl_live_volume.load(std::memory_order_acquire));
    } else {
      M5.Speaker.stop();
    }
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

void process_command(char* command) {
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
    Serial.printf("RTL_IQ_STATUS active=%s mode=%s bytes=%u max_bytes=%u frequency_hz=%u sf=%u bw=%u auto=%s events=%u messages=%u noise_dbfs=%.1f trigger_dbfs=%.1f last_path=\"%s\"\n",
                  g_iq_rec_active.load(std::memory_order_acquire) ? "true" : "false",
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
  if (strncmp(command, "LORA_MESSAGE ", 13) == 0) {
    if (!lora_present_host_message(command + 13)) {
      Serial.println("LORA_MESSAGE_ERROR invalid_fields");
      return;
    }
    Serial.printf("LORA_MESSAGE_OK from=%08lx id=%08lx bytes=%u\n",
                  static_cast<unsigned long>(lora_last_sender),
                  static_cast<unsigned long>(lora_last_packet_id),
                  static_cast<unsigned>(strlen(lora_last_message)));
    return;
  }
  if (strcmp(command, "LORA_MESSAGE_CLEAR") == 0) {
    portENTER_CRITICAL(&lora_message_mux);
    lora_last_message[0] = '\0';
    lora_last_sender = 0;
    lora_last_packet_id = 0;
    portEXIT_CRITICAL(&lora_message_mux);
    Serial.println("LORA_MESSAGE_CLEARED");
    return;
  }
  if (strcmp(command, "RTL_IQ_RETRIEVE_BEGIN") == 0) {
    if (g_iq_rec_last_path[0] == '\0' ||
        g_iq_rec_active.load(std::memory_order_acquire) ||
        g_iq_rec_export_pending.load(std::memory_order_acquire)) {
      Serial.println("RTL_IQ_RETRIEVE_ERROR capture_not_ready");
      return;
    }
    const bool running = rtl_capture_state.load(std::memory_order_acquire) ==
                         RtlCaptureState::running;
    g_iq_retrieve_resume.store(running, std::memory_order_release);
    if (running) {
      rtl_restart_requested.store(false, std::memory_order_release);
      rtl_stop_requested.store(true, std::memory_order_release);
    }
    Serial.printf(running ? "RTL_IQ_RETRIEVE_STOPPING pathhex="
                          : "RTL_IQ_RETRIEVE_READY pathhex=");
    print_hex(reinterpret_cast<const uint8_t*>(g_iq_rec_last_path),
              strlen(g_iq_rec_last_path));
    Serial.println();
    return;
  }
  if (strcmp(command, "RTL_IQ_RETRIEVE_END") == 0) {
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
  if (strcmp(command, "RTL_STOP") == 0 && authenticated) {
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
}  // namespace

void orcsdr_splash_poll_serial(void) {
  poll_serial();
}

void setup() {
  Serial.begin(115200);

  auto config = M5.config();
  M5.begin(config);
  M5.Display.setRotation(1);
  M5.Display.setBrightness(180);

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

  orcsdr_splash_set_status("Starting Wi-Fi stack…");
  initialize_wifi();
  if (wifi_configured) {
    orcsdr_splash_set_status("Connecting Wi-Fi…");
    start_wifi_connection();
  } else {
    orcsdr_splash_set_status("Scanning Wi-Fi networks…");
    start_wifi_inventory();
  }

  orcsdr_splash_set_status("Starting RTL-SDR USB host…");
  initialize_rtl_sdr_host();

  /* Dependencies are up: reveal the gate while the background keeps looping. */
  orcsdr_splash_set_ready(true);
  (void)orcsdr_splash_wait_start();
  orcsdr_splash_end();
  g_suppress_home_paint = false;

  draw_ui();
  draw_rtl_sdr_state();
  append_journal("boot");
  last_ping_ms = millis();
  offline_transition_handled = !paired;
  draw_session_state(paired ? "Waiting for authenticated host" : "Unpaired - USB provisioning",
                     paired ? TFT_YELLOW : TFT_ORANGE);
  draw_power_state();
}

void loop() {
  const bool radio_ui = rtl_ui_active.load(std::memory_order_acquire);
  // Single-owner rule: while radio UI streams, only the USB task may M5.update().
  // Dual M5.update() (loop + stream) was correlated with total speaker silence.
  if (!radio_ui) {
    M5.update();
  }
  poll_serial();
  poll_wifi();

  const uint32_t current_rtl_sdr_status_revision =
      rtl_sdr_status_revision.load(std::memory_order_acquire);
  if (drawn_rtl_sdr_status_revision != current_rtl_sdr_status_revision) {
    drawn_rtl_sdr_status_revision = current_rtl_sdr_status_revision;
    if (!radio_ui) draw_rtl_sdr_state();
  }

  // Home-screen taps when radio UI is not active.
  if (!radio_ui) {
    const auto touch = M5.Touch.getDetail(0);
    const bool pressed = touch.isPressed() || touch.wasPressed();
    if (pressed && !was_pressed) {
      if (point_in_button(touch.x, touch.y)) {
        if (rtl_device_ready()) {
          queue_local_rtl_listen(RtlBand::fm, rtl_saved_fm_hz);
        } else {
          emit_touch(touch.x, touch.y);
        }
      }
    }
    was_pressed = pressed;
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
