#include <Arduino.h>
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
#include <cstring>

#include "rtl_sdr_v4_transfers.h"

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
constexpr size_t kRtlAudioBufferSamples = 832;
constexpr size_t kRtlSpectrumBins = 128;
// Dual-core: Core0 USB host refills bulk IQ into a ring; Core1 DSP/UI consumes.
// Spectrum budget is no longer on the USB critical path.
constexpr uint32_t kRtlSpectrumIntervalMs = 100;
constexpr uint32_t kRtlSpectrumTraceIntervalMs = 250;
constexpr size_t kRtlRingDepth = 3;
// Skip spectrum for the first N ms after tune so the speaker queue can prime.
constexpr uint32_t kRtlAudioPrimeMs = 400;
constexpr int kSpectrumX = 64;
constexpr int kSpectrumY = 96;
constexpr int kSpectrumWidth = 1152;
constexpr int kSpectrumHeight = 200;
constexpr int kWaterfallY = 316;
constexpr int kWaterfallHeight = 250;
// Two control rows under the waterfall; keep spectrum/waterfall dominant.
constexpr int kSdrBandY = 580;
constexpr int kSdrTuneY = 648;
constexpr int kSdrControlsHeight = 52;
constexpr int kSdrGap = 12;
constexpr int kSdrEdge = 48;
constexpr uint8_t kRtlVolumeMin = 0;
constexpr uint8_t kRtlVolumeMax = 255;
// ~50% of the M5 speaker scale (0-255). Operator found 220 too loud as a start.
constexpr uint8_t kRtlVolumeDefault = 128;
constexpr uint8_t kRtlVolumeStep = 16;
constexpr uint32_t kRtlFmMinHz = 87500000;
constexpr uint32_t kRtlFmMaxHz = 108000000;
constexpr uint32_t kRtlFmStepHz = 100000;
constexpr uint32_t kRtlFmDefaultHz = 96100000;
constexpr uint32_t kRtlAmMinHz = 520000;
constexpr uint32_t kRtlAmMaxHz = 1710000;
constexpr uint32_t kRtlAmStepHz = 10000;
constexpr uint32_t kRtlAmDefaultHz = 1000000;
constexpr uint32_t kRtlWxHz = 162400000;
// Clean-room LO offset: LO = RF + 1.814972 MHz (from 100 MHz observation).
constexpr double kRtlIfOffsetHz = 1814972.0;
constexpr double kRtlXtalHz = 28800000.0;

enum class RtlBand : uint8_t { fm, am, wx };

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
  int32_t i_sum = 0;
  int32_t q_sum = 0;
  uint8_t rf_phase = 0;
  float previous_i = 0;
  float previous_q = 0;
  bool have_previous = false;
  float channel_filter = 0;
  float audio_sum = 0;
  uint8_t audio_phase = 0;
  float deemphasis = 0;
  float dc = 0;
  float envelope_filter = 0;
  float agc_gain = 1.0f;
  float agc_level = 2000.0f;
  float last_out = 0;
  uint16_t fade_in = 0;
  uint8_t buffer = 0;
  uint64_t samples = 0;
  uint32_t queued_chunks = 0;
  uint32_t dropped_chunks = 0;
  int16_t peak = 0;
  double square_sum = 0;
};

RtlAudioState rtl_audio;
int16_t rtl_audio_buffers[3][kRtlAudioBufferSamples];
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
static TaskHandle_t rtl_dsp_task_handle = nullptr;
static std::atomic<uint32_t> rtl_usb_overruns{0};
static std::atomic<uint32_t> rtl_dsp_blocks{0};
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
float rtl_spectrum_window[kRtlSpectrumBins];
int16_t rtl_spectrum_y[kRtlSpectrumBins];
uint16_t rtl_waterfall_row[kSpectrumWidth];
bool rtl_spectrum_window_ready = false;
bool rtl_spectrum_trace_valid = false;
uint32_t rtl_spectrum_last_ms = 0;
uint32_t rtl_spectrum_trace_last_ms = 0;
uint32_t rtl_spectrum_frames = 0;
uint32_t rtl_spectrum_fps_window_ms = 0;
uint16_t rtl_spectrum_fps = 0;

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
usb_host_client_handle_t usb_client = nullptr;
usb_device_handle_t rtl_sdr_device = nullptr;
bool rtl_sdr_gone = false;
std::atomic<RtlCaptureState> rtl_capture_state{RtlCaptureState::disconnected};
std::atomic<bool> rtl_capture_requested{false};
std::atomic<RtlBand> rtl_requested_band{RtlBand::fm};
std::atomic<uint32_t> rtl_requested_frequency_hz{kRtlFmDefaultHz};
// Non-zero = apply PLL retune without tearing down the IQ stream (fluid scroll).
std::atomic<uint32_t> rtl_hot_retune_hz{0};
std::atomic<uint8_t> rtl_requested_volume{kRtlVolumeDefault};
std::atomic<uint8_t> rtl_live_volume{kRtlVolumeDefault};
std::atomic<bool> rtl_volume_changed{false};
// Displayed RF span under the scope (matches axis markers ±480 kHz).
constexpr double kRtlScopeSpanHz = 960000.0;
std::atomic<bool> rtl_continuous_requested{false};
std::atomic<bool> rtl_stop_requested{false};
std::atomic<bool> rtl_restart_requested{false};
std::atomic<bool> rtl_ui_active{false};
std::atomic<bool> usb_transfer_done{false};
std::atomic<uint32_t> rtl_ui_revision{0};
uint32_t drawn_rtl_ui_revision = 0;
RtlBand rtl_ui_band = RtlBand::fm;
uint32_t rtl_ui_frequency_hz = kRtlFmDefaultHz;
uint8_t rtl_ui_volume = kRtlVolumeDefault;
uint64_t rtl_capture_bytes = 0;
uint8_t rtl_capture_min = 0;
uint8_t rtl_capture_max = 0;
double rtl_capture_mean = 0;
char rtl_capture_sha256[65]{};
char rtl_capture_error[64] = "not run";

void emit_identity();
void reset_spectrum_renderer();
void draw_spectrum_grid();
void handle_sdr_touch(int32_t x, int32_t y);
void poll_sdr_touch_from_stream();

void draw_touch_state(const char* message, uint32_t color) {
  M5.Display.fillRect(300, 480, 680, 70, TFT_BLACK);
  M5.Display.setTextColor(color, TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawString(message, 640, 515);
}

void draw_session_state(const char* message, uint32_t color) {
  M5.Display.fillRect(250, 210, 780, 55, TFT_BLACK);
  M5.Display.setTextColor(color, TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawString(message, 640, 237);
}

void draw_wifi_state() {
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
  char message[96];
  const int32_t level = M5.Power.getBatteryLevel();
  const int16_t battery_mv = M5.Power.getBatteryVoltage();
  const int16_t vbus_mv = M5.Power.getVBUSVoltage();
  snprintf(message, sizeof(message), "Power: %ld%%  battery %dmV  USB %dmV",
           static_cast<long>(level), battery_mv, vbus_mv);
  M5.Display.fillRect(200, 655, 880, 45, TFT_BLACK);
  M5.Display.setTextColor(level >= 0 ? TFT_LIGHTGREY : TFT_ORANGE, TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawString(message, 640, 677);
}

void set_rtl_sdr_status(const char* status) {
  strlcpy(rtl_sdr_status, status, sizeof(rtl_sdr_status));
  rtl_sdr_status_revision.fetch_add(1, std::memory_order_release);
}

void draw_rtl_sdr_state() {
  const bool ready = strstr(rtl_sdr_status, "ready") != nullptr;
  M5.Display.fillRect(150, 545, 980, 40, TFT_BLACK);
  M5.Display.setTextColor(ready ? TFT_GREEN : TFT_ORANGE, TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawString(rtl_sdr_status, 640, 565);
  if (ready) {
    M5.Display.fillRoundRect(kButtonX, kButtonY, kButtonWidth, kButtonHeight, 18,
                             TFT_DARKGREEN);
    M5.Display.drawRoundRect(kButtonX, kButtonY, kButtonWidth, kButtonHeight, 18,
                             TFT_GREEN);
    M5.Display.setTextColor(TFT_WHITE, TFT_DARKGREEN);
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

const char* rtl_band_name(RtlBand band) {
  switch (band) {
    case RtlBand::am: return "AM";
    case RtlBand::wx: return "WX";
    default: return "FM";
  }
}

const char* rtl_mode_name(RtlBand band) {
  switch (band) {
    case RtlBand::am: return "AM";
    case RtlBand::wx: return "NFM";
    default: return "WBFM";
  }
}

uint32_t rtl_band_default_frequency(RtlBand band) {
  switch (band) {
    case RtlBand::am: return kRtlAmDefaultHz;
    case RtlBand::wx: return kRtlWxHz;
    default: return kRtlFmDefaultHz;
  }
}

uint32_t rtl_clamp_frequency(RtlBand band, uint32_t frequency_hz) {
  switch (band) {
    case RtlBand::am:
      if (frequency_hz < kRtlAmMinHz) return kRtlAmMinHz;
      if (frequency_hz > kRtlAmMaxHz) return kRtlAmMaxHz;
      return frequency_hz;
    case RtlBand::wx:
      return kRtlWxHz;
    default:
      if (frequency_hz < kRtlFmMinHz) return kRtlFmMinHz;
      if (frequency_hz > kRtlFmMaxHz) return kRtlFmMaxHz;
      return frequency_hz;
  }
}

uint32_t rtl_step_frequency(RtlBand band, uint32_t frequency_hz, int direction) {
  if (band == RtlBand::wx) return kRtlWxHz;
  const uint32_t step = band == RtlBand::am ? kRtlAmStepHz : kRtlFmStepHz;
  if (direction < 0) {
    if (frequency_hz <= step) return rtl_clamp_frequency(band, 0);
    return rtl_clamp_frequency(band, frequency_hz - step);
  }
  return rtl_clamp_frequency(band, frequency_hz + step);
}

void format_frequency(char* output, size_t output_size, uint32_t frequency_hz) {
  if (frequency_hz >= 1000000) {
    // One more digit so infinite-scroll tuning feels continuous.
    snprintf(output, output_size, "%.2f MHz", frequency_hz / 1000000.0);
  } else {
    snprintf(output, output_size, "%.0f kHz", frequency_hz / 1000.0);
  }
}

void apply_speaker_volume(uint8_t volume) {
  // Keep master and virtual-channel levels aligned; some M5 paths only honor one.
  M5.Speaker.setVolume(volume);
  M5.Speaker.setChannelVolume(0, volume);
}

bool ensure_speaker_running(uint8_t volume) {
  static bool dma_configured = false;
  if (!dma_configured) {
    // Extra DMA depth absorbs FFT/draw stalls without audible gaps.
    auto cfg = M5.Speaker.config();
    cfg.dma_buf_count = 16;
    cfg.dma_buf_len = 256;
    M5.Speaker.config(cfg);
    dma_configured = true;
  }
  apply_speaker_volume(volume);
  if (!M5.Speaker.isEnabled()) return false;
  if (!M5.Speaker.isRunning() && !M5.Speaker.begin()) return false;
  apply_speaker_volume(volume);
  return M5.Speaker.isRunning() || M5.Speaker.isEnabled();
}

void bump_rtl_ui() {
  rtl_ui_revision.fetch_add(1, std::memory_order_release);
}

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
    M5.Display.setTextSize(1);
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

void draw_sdr_controls(RtlBand band, uint8_t volume, bool running) {
  M5.Display.fillRect(0, kSdrBandY - 6, 1280, 720 - (kSdrBandY - 6), TFT_BLACK);
  char vol_label[16];
  snprintf(vol_label, sizeof(vol_label), "VOL %u", volume);
  const SdrButton band_row[] = {
      {0, 180, "FM",
       static_cast<uint32_t>(band == RtlBand::fm ? TFT_DARKGREEN : TFT_DARKGREY)},
      {0, 180, "AM",
       static_cast<uint32_t>(band == RtlBand::am ? TFT_DARKGREEN : TFT_DARKGREY)},
      {0, 180, "WX",
       static_cast<uint32_t>(band == RtlBand::wx ? TFT_DARKGREEN : TFT_DARKGREY)},
      {0, 280, vol_label, static_cast<uint32_t>(TFT_NAVY)},
      {0, 220, running ? "STOP" : "START",
       static_cast<uint32_t>(running ? TFT_MAROON : TFT_DARKGREEN)},
  };
  const SdrButton tune_row[] = {
      {0, 200, "FREQ -", TFT_DARKGREY},
      {0, 200, "FREQ +", TFT_DARKGREY},
      {0, 200, "VOL -", TFT_NAVY},
      {0, 200, "VOL +", TFT_NAVY},
      {0, 240, band == RtlBand::am ? "10 kHz step" : "100 kHz step", TFT_DARKGREY},
  };
  draw_sdr_button_row(kSdrBandY, band_row, std::size(band_row));
  draw_sdr_button_row(kSdrTuneY, tune_row, std::size(tune_row));
}

void draw_sdr_header(RtlBand band, uint32_t frequency_hz, uint8_t volume) {
  char label[96];
  char frequency_text[24];
  format_frequency(frequency_text, sizeof(frequency_text), frequency_hz);
  M5.Display.fillRect(0, 0, 1280, kSpectrumY - 4, TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(3);
  M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
  snprintf(label, sizeof(label), "%s  %s", rtl_band_name(band), frequency_text);
  M5.Display.drawString(label, 520, 34);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
  snprintf(label, sizeof(label), "VOL %u", volume);
  M5.Display.drawString(label, 1040, 34);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  snprintf(label, sizeof(label),
           "%s  |  960 kS/s  |  drag scope to scroll tune  |  tap band / VOL",
           rtl_mode_name(band));
  M5.Display.drawString(label, 640, 72);
}

void draw_sdr_screen(RtlBand band, uint32_t frequency_hz, uint8_t volume) {
  char label[64];
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
  M5.Display.drawRect(kSpectrumX, kSpectrumY, kSpectrumWidth, kSpectrumHeight,
                      TFT_DARKGREY);
  for (int line = 1; line < 4; ++line) {
    const int y = kSpectrumY + line * kSpectrumHeight / 4;
    M5.Display.drawFastHLine(kSpectrumX, y, kSpectrumWidth, 0x2104);
  }
  for (int line = 0; line <= 4; ++line) {
    const int x = kSpectrumX + line * kSpectrumWidth / 4;
    M5.Display.drawFastVLine(x, kSpectrumY, kSpectrumHeight, 0x2104);
  }
  const double center = frequency_hz / 1000000.0;
  const double span = 0.480;
  for (int marker = 0; marker <= 4; ++marker) {
    const double mark = center - span + marker * (span / 2.0);
    if (frequency_hz >= 1000000) {
      snprintf(label, sizeof(label), marker == 4 ? "%.2f MHz" : "%.2f", mark);
    } else {
      snprintf(label, sizeof(label), marker == 4 ? "%.0f kHz" : "%.0f", mark * 1000.0);
    }
    M5.Display.setTextColor(marker == 2 ? TFT_GREEN : TFT_LIGHTGREY, TFT_BLACK);
    M5.Display.drawString(label, kSpectrumX + marker * kSpectrumWidth / 4,
                          kSpectrumY + kSpectrumHeight + 14);
  }
  M5.Display.fillRect(kSpectrumX, kWaterfallY, kSpectrumWidth, kWaterfallHeight,
                      TFT_BLACK);
  M5.Display.drawRect(kSpectrumX, kWaterfallY, kSpectrumWidth, kWaterfallHeight,
                      TFT_DARKGREY);
  M5.Display.setScrollRect(kSpectrumX + 1, kWaterfallY + 1, kSpectrumWidth - 2,
                           kWaterfallHeight - 2, TFT_BLACK);
  reset_spectrum_renderer();
  draw_spectrum_grid();
  draw_sdr_controls(band, volume, true);
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
    rtl_spectrum_y[index] = kSpectrumY + kSpectrumHeight - 2;
  }
}

void draw_spectrum_grid() {
  for (int line = 1; line < 4; ++line) {
    const int y = kSpectrumY + line * kSpectrumHeight / 4;
    M5.Display.drawFastHLine(kSpectrumX + 1, y, kSpectrumWidth - 2, 0x2104);
  }
  M5.Display.drawFastVLine(kSpectrumX + kSpectrumWidth / 2, kSpectrumY + 1,
                           kSpectrumHeight - 2, TFT_GREEN);
}

void draw_spectrum(const uint8_t* iq, size_t bytes) {
  if (bytes < kRtlSpectrumBins * 2) return;
  const uint32_t now = millis();
  if (rtl_spectrum_last_ms != 0 &&
      now - rtl_spectrum_last_ms < kRtlSpectrumIntervalMs) {
    return;
  }
  rtl_spectrum_last_ms = now;

  constexpr float kPi = 3.14159265358979323846f;
  for (size_t index = 0; index < kRtlSpectrumBins; ++index) {
    rtl_spectrum_real[index] =
        (static_cast<int>(iq[index * 2]) - 128) * rtl_spectrum_window[index];
    rtl_spectrum_imaginary[index] =
        (static_cast<int>(iq[index * 2 + 1]) - 128) * rtl_spectrum_window[index];
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
    for (size_t base = 0; base < kRtlSpectrumBins; base += length) {
      float twiddle_real = 1;
      float twiddle_imaginary = 0;
      for (size_t offset = 0; offset < length / 2; ++offset) {
        const size_t upper = base + offset;
        const size_t lower = upper + length / 2;
        const float product_real = rtl_spectrum_real[lower] * twiddle_real -
                                   rtl_spectrum_imaginary[lower] * twiddle_imaginary;
        const float product_imaginary = rtl_spectrum_real[lower] * twiddle_imaginary +
                                        rtl_spectrum_imaginary[lower] * twiddle_real;
        rtl_spectrum_real[lower] = rtl_spectrum_real[upper] - product_real;
        rtl_spectrum_imaginary[lower] = rtl_spectrum_imaginary[upper] - product_imaginary;
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

  float maximum = -120.0f;
  for (size_t bin = 0; bin < kRtlSpectrumBins; ++bin) {
    const size_t shifted = (bin + kRtlSpectrumBins / 2) % kRtlSpectrumBins;
    const float level =
        10.0f * log10f(rtl_spectrum_real[shifted] * rtl_spectrum_real[shifted] +
                       rtl_spectrum_imaginary[shifted] * rtl_spectrum_imaginary[shifted] +
                       1.0f);
    // Light EMA so the trace is less sparkly at high refresh.
    rtl_spectrum_smooth[bin] = rtl_spectrum_trace_valid
                                   ? (0.65f * rtl_spectrum_smooth[bin] + 0.35f * level)
                                   : level;
    rtl_spectrum_levels[bin] = rtl_spectrum_smooth[bin];
    maximum = max(maximum, rtl_spectrum_levels[bin]);
  }
  const float floor = maximum - 48.0f;
  // Waterfall every spectrum tick; full cyan trace less often (line erase is costly).
  const bool redraw_trace =
      !rtl_spectrum_trace_valid ||
      (now - rtl_spectrum_trace_last_ms) >= kRtlSpectrumTraceIntervalMs;

  M5.Display.startWrite();
  if (redraw_trace && rtl_spectrum_trace_valid) {
    int previous_x = kSpectrumX;
    int previous_y = rtl_spectrum_y[0];
    for (size_t bin = 0; bin < kRtlSpectrumBins; ++bin) {
      const int x = kSpectrumX + static_cast<int>(bin * kSpectrumWidth /
                                                  (kRtlSpectrumBins - 1));
      const int y = rtl_spectrum_y[bin];
      if (bin != 0) M5.Display.drawLine(previous_x, previous_y, x, y, TFT_BLACK);
      previous_x = x;
      previous_y = y;
    }
    draw_spectrum_grid();
  }

  // One-pixel waterfall step; cheap pushImage vs many fillRects.
  M5.Display.scroll(0, -1);
  const int waterfall_width = kSpectrumWidth - 2;
  int previous_x = kSpectrumX;
  int previous_y = kSpectrumY + kSpectrumHeight - 2;
  for (size_t bin = 0; bin < kRtlSpectrumBins; ++bin) {
    const float normalized =
        constrain((rtl_spectrum_levels[bin] - floor) / 48.0f, 0.0f, 1.0f);
    const int x = kSpectrumX + static_cast<int>(bin * kSpectrumWidth /
                                                (kRtlSpectrumBins - 1));
    const int y = kSpectrumY + kSpectrumHeight - 2 -
                  static_cast<int>(normalized * (kSpectrumHeight - 4));
    if (redraw_trace) {
      rtl_spectrum_y[bin] = static_cast<int16_t>(y);
      if (bin != 0) M5.Display.drawLine(previous_x, previous_y, x, y, TFT_CYAN);
      previous_x = x;
      previous_y = y;
    }
    const int cell_x = static_cast<int>(bin * waterfall_width / kRtlSpectrumBins);
    const int next_x =
        static_cast<int>((bin + 1) * waterfall_width / kRtlSpectrumBins);
    const uint16_t color = waterfall_color(normalized);
    for (int pixel = cell_x; pixel < next_x; ++pixel) {
      rtl_waterfall_row[pixel] = color;
    }
  }
  M5.Display.pushImage(kSpectrumX + 1, kWaterfallY + kWaterfallHeight - 2,
                       waterfall_width, 1, rtl_waterfall_row);
  if (redraw_trace) {
    M5.Display.drawFastVLine(kSpectrumX + kSpectrumWidth / 2, kSpectrumY + 1,
                             kSpectrumHeight - 2, TFT_GREEN);
    rtl_spectrum_trace_last_ms = now;
    rtl_spectrum_trace_valid = true;
  }
  M5.Display.endWrite();

  ++rtl_spectrum_frames;
  if (now - rtl_spectrum_fps_window_ms >= 1000) {
    rtl_spectrum_fps = static_cast<uint16_t>(rtl_spectrum_frames);
    rtl_spectrum_frames = 0;
    rtl_spectrum_fps_window_ms = now;
    Serial.printf("RTL_SPECTRUM_FPS fps=%u audio_dropped=%u audio_chunks=%u "
                  "audio_peak=%d volume=%u speaker_running=%s\n",
                  rtl_spectrum_fps, rtl_audio.dropped_chunks, rtl_audio.queued_chunks,
                  rtl_audio.peak, rtl_live_volume.load(std::memory_order_acquire),
                  M5.Speaker.isRunning() ? "true" : "false");
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

// Soft AGC + tanh limiter: serial showed peak hard-stuck at 16000 (harsh/choppy).
int16_t shape_audio_sample(float demodulated, float base_scale) {
  float x = demodulated * base_scale * rtl_audio.agc_gain;
  const float ax = fabsf(x);
  rtl_audio.agc_level = 0.992f * rtl_audio.agc_level + 0.008f * ax;
  if (rtl_audio.agc_level > 400.0f) {
    const float desired = 5200.0f / rtl_audio.agc_level;
    rtl_audio.agc_gain = 0.97f * rtl_audio.agc_gain + 0.03f * desired;
    if (rtl_audio.agc_gain < 0.18f) rtl_audio.agc_gain = 0.18f;
    if (rtl_audio.agc_gain > 3.2f) rtl_audio.agc_gain = 3.2f;
  }
  // Soft knee instead of hard clip at ±16000.
  x = 13000.0f * tanhf(x / 13000.0f);
  if (rtl_audio.fade_in < 128) {
    x *= static_cast<float>(rtl_audio.fade_in) / 128.0f;
    ++rtl_audio.fade_in;
  }
  // Mild de-click blend.
  x = 0.88f * x + 0.12f * rtl_audio.last_out;
  rtl_audio.last_out = x;
  const int32_t sample = lroundf(x);
  return static_cast<int16_t>(constrain(sample, -16000, 16000));
}

void queue_audio_samples(int16_t* audio, size_t audio_count) {
  if (audio_count == 0) return;
  if (rtl_volume_changed.exchange(false, std::memory_order_acq_rel)) {
    apply_speaker_volume(rtl_live_volume.load(std::memory_order_acquire));
  }
  // If the codec was interrupted (e.g. by a contending M5.update), re-arm once.
  if (!M5.Speaker.isRunning()) {
    ensure_speaker_running(rtl_live_volume.load(std::memory_order_acquire));
  }
  if (M5.Speaker.playRaw(audio, audio_count, 48000, false, 1, 0, false)) {
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
  rtl_audio.buffer = (rtl_audio.buffer + 1) % std::size(rtl_audio_buffers);
}

void demodulate_fm(const uint8_t* iq, size_t bytes, float audio_scale) {
  int16_t* audio = rtl_audio_buffers[rtl_audio.buffer];
  size_t audio_count = 0;
  for (size_t offset = 0; offset + 1 < bytes; offset += 2) {
    rtl_audio.i_sum += static_cast<int32_t>(iq[offset]) - 128;
    rtl_audio.q_sum += static_cast<int32_t>(iq[offset + 1]) - 128;
    if (++rtl_audio.rf_phase != 4) continue;

    const float i = rtl_audio.i_sum;
    const float q = rtl_audio.q_sum;
    rtl_audio.i_sum = 0;
    rtl_audio.q_sum = 0;
    rtl_audio.rf_phase = 0;
    if (rtl_audio.have_previous) {
      const float phase = fast_phase(rtl_audio.previous_i * q -
                                         rtl_audio.previous_q * i,
                                     rtl_audio.previous_i * i +
                                         rtl_audio.previous_q * q);
      rtl_audio.channel_filter += 0.28f * (phase - rtl_audio.channel_filter);
      rtl_audio.audio_sum += rtl_audio.channel_filter;
      if (++rtl_audio.audio_phase == 5) {
        const float demodulated = rtl_audio.audio_sum * 0.2f;
        rtl_audio.audio_sum = 0;
        rtl_audio.audio_phase = 0;
        rtl_audio.deemphasis += 0.217f * (demodulated - rtl_audio.deemphasis);
        rtl_audio.dc += 0.001f * (rtl_audio.deemphasis - rtl_audio.dc);
        const int16_t sample =
            shape_audio_sample(rtl_audio.deemphasis - rtl_audio.dc, audio_scale);
        audio[audio_count++] = sample;
        const int16_t magnitude = sample < 0 ? -sample : sample;
        if (magnitude > rtl_audio.peak) rtl_audio.peak = magnitude;
        rtl_audio.square_sum += static_cast<double>(sample) * sample;
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
  for (size_t offset = 0; offset + 1 < bytes; offset += 2) {
    rtl_audio.i_sum += static_cast<int32_t>(iq[offset]) - 128;
    rtl_audio.q_sum += static_cast<int32_t>(iq[offset + 1]) - 128;
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
      rtl_audio.square_sum += static_cast<double>(sample) * sample;
      ++rtl_audio.samples;
    }
  }
  queue_audio_samples(audio, audio_count);
}

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
  const float audio_scale =
      band == RtlBand::wx ? 12000.0f : band == RtlBand::am ? 9000.0f : 5500.0f;
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
  delay(20);
  const bool speaker_ok = ensure_speaker_running(volume);
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
    // Audio first.
    if (band == RtlBand::am) {
      demodulate_am(rtl_iq_processing, completed_bytes, audio_scale);
    } else {
      demodulate_fm(rtl_iq_processing, completed_bytes, audio_scale);
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
          rtl_audio.fade_in = 0;
          rtl_audio.have_previous = false;
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
      char vol_label[16];
      snprintf(vol_label, sizeof(vol_label), "VOL %u", live_volume);
      M5.Display.fillRoundRect(kSdrEdge + 180 * 3 + kSdrGap * 3, kSdrBandY, 280,
                               kSdrControlsHeight, 10, TFT_NAVY);
      M5.Display.drawRoundRect(kSdrEdge + 180 * 3 + kSdrGap * 3, kSdrBandY, 280,
                               kSdrControlsHeight, 10, TFT_WHITE);
      M5.Display.setTextColor(TFT_WHITE, TFT_NAVY);
      M5.Display.setTextDatum(middle_center);
      M5.Display.setTextSize(1);
      M5.Display.drawString(vol_label, kSdrEdge + 180 * 3 + kSdrGap * 3 + 140,
                            kSdrBandY + kSdrControlsHeight / 2);
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
    draw_sdr_controls(band, rtl_ui_volume, false);
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
    draw_sdr_controls(band, rtl_ui_volume, false);
    Serial.printf("RTL_CAPTURE_ERROR bytes=%llu reason=\"%s\"\n",
                  static_cast<unsigned long long>(rtl_capture_bytes), rtl_capture_error);
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
  M5.Display.setTextSize(3);
  M5.Display.drawString("OrcLink", 640, 90);

  M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.drawString("M5Tab5 agent online", 640, 175);

  M5.Display.fillRoundRect(kButtonX, kButtonY, kButtonWidth, kButtonHeight, 18, TFT_DARKCYAN);
  M5.Display.drawRoundRect(kButtonX, kButtonY, kButtonWidth, kButtonHeight, 18, TFT_CYAN);
  M5.Display.setTextColor(TFT_WHITE, TFT_DARKCYAN);
  M5.Display.drawString("Tap to verify touch", 640, 360);

  M5.Display.setTextSize(2);
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
  if (rtl_sdr_device == nullptr) return;
  frequency_hz = rtl_clamp_frequency(band, frequency_hz);
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
  append_journal(band == RtlBand::am ? "sdr_am" : band == RtlBand::wx ? "sdr_wx" : "sdr_fm");
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
  return x >= kSpectrumX && x < kSpectrumX + kSpectrumWidth && y >= kSpectrumY &&
         y < kWaterfallY + kWaterfallHeight;
}

void request_hot_retune(uint32_t frequency_hz) {
  if (rtl_ui_band == RtlBand::wx) return;
  frequency_hz = rtl_clamp_frequency(rtl_ui_band, frequency_hz);
  // 5 kHz quantize while dragging keeps EP0 load reasonable; still continuous feel.
  frequency_hz = (frequency_hz / 5000u) * 5000u;
  if (frequency_hz == 0) return;
  rtl_ui_frequency_hz = frequency_hz;
  // Only queue; stream task applies when no bulk URB is outstanding.
  rtl_hot_retune_hz.store(frequency_hz, std::memory_order_release);
  bump_rtl_ui();
}

// Capture runs on the high-priority USB task and previously starved Arduino
// loop() touch polling. Service edges here so buttons work during waterfall.
// Only this path may call M5.update() while rtl_ui_active — concurrent update
// from loop() was silencing the ES8388 speaker path after 0.8.26.
void poll_sdr_touch_from_stream() {
  static uint32_t last_touch_poll_ms = 0;
  static bool flick_thresh_set = false;
  static bool scope_dragging = false;
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
  const auto touch = M5.Touch.getDetail(0);
  const bool pressed = touch.isPressed() || touch.wasPressed();

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
    const double hz_per_px = kRtlScopeSpanHz / static_cast<double>(kSpectrumWidth);
    int64_t next = static_cast<int64_t>(drag_anchor_hz) -
                   static_cast<int64_t>(llround(static_cast<double>(dx) * hz_per_px));
    if (next < 0) next = 0;
    const uint32_t tuned =
        rtl_clamp_frequency(rtl_ui_band, static_cast<uint32_t>(next));
    rtl_ui_frequency_hz = tuned;
    bump_rtl_ui();
    if (tuned != last_queued_hz && now - last_queue_ms >= 120) {
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
    const double hz_per_px = kRtlScopeSpanHz / static_cast<double>(kSpectrumWidth);
    int64_t next = static_cast<int64_t>(drag_anchor_hz) -
                   static_cast<int64_t>(llround(static_cast<double>(dx) * hz_per_px));
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
  static constexpr int kBandWidths[] = {180, 180, 180, 280, 220};
  static constexpr int kTuneWidths[] = {200, 200, 200, 200, 240};
  const int band_index = sdr_button_at(kSdrBandY, x, y, kBandWidths, std::size(kBandWidths));
  if (band_index >= 0) {
    if (band_index == 0) {
      queue_local_rtl_listen(RtlBand::fm, rtl_ui_band == RtlBand::fm
                                               ? rtl_ui_frequency_hz
                                               : kRtlFmDefaultHz);
    } else if (band_index == 1) {
      queue_local_rtl_listen(RtlBand::am, rtl_ui_band == RtlBand::am
                                               ? rtl_ui_frequency_hz
                                               : kRtlAmDefaultHz);
    } else if (band_index == 2) {
      queue_local_rtl_listen(RtlBand::wx, kRtlWxHz);
    } else if (band_index == 3) {
      // Center volume readout is a mute/unmute toggle between default and zero.
      if (rtl_ui_volume == 0) adjust_rtl_volume(kRtlVolumeDefault);
      else adjust_rtl_volume(-static_cast<int>(rtl_ui_volume));
    } else if (band_index == 4) {
      const RtlCaptureState state = rtl_capture_state.load(std::memory_order_acquire);
      if (state == RtlCaptureState::running) {
        rtl_restart_requested.store(false, std::memory_order_release);
        rtl_stop_requested.store(true, std::memory_order_release);
        append_journal("sdr_stop");
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
    queue_local_rtl_listen(rtl_ui_band, next);
  } else if (tune_index == 2) {
    adjust_rtl_volume(-static_cast<int>(kRtlVolumeStep));
  } else if (tune_index == 3) {
    adjust_rtl_volume(static_cast<int>(kRtlVolumeStep));
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
  if ((kzel_capture || noaa_capture || am_capture) && authenticated) {
    const RtlBand band =
        noaa_capture ? RtlBand::wx : am_capture ? RtlBand::am : RtlBand::fm;
    const uint32_t frequency_hz = rtl_band_default_frequency(band);
    RtlCaptureState expected = RtlCaptureState::ready;
    const RtlCaptureState current = rtl_capture_state.load(std::memory_order_acquire);
    if (current == RtlCaptureState::complete || current == RtlCaptureState::failed) {
      expected = current;
    }
    if (rtl_sdr_device == nullptr ||
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
                  rtl_sdr_device != nullptr ? "true" : "false", rtl_sdr_vid,
                  rtl_sdr_pid, rtl_sdr_speed, rtl_sdr_serial);
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

void setup() {
  Serial.begin(115200);

  auto config = M5.config();
  M5.begin(config);
  M5.Display.setRotation(1);
  M5.Display.setBrightness(180);
  draw_ui();
  draw_rtl_sdr_state();
  initialize_wifi();
  initialize_rtl_sdr_host();

  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_BASE);
  snprintf(node_id, sizeof(node_id), "m5tab5_%02x%02x%02x%02x%02x%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  load_state();
  if (wifi_configured) {
    start_wifi_connection();
  } else {
    start_wifi_inventory();
  }
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
        if (rtl_sdr_device != nullptr) {
          queue_local_rtl_listen(RtlBand::fm, kRtlFmDefaultHz);
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
