#include "tab5_keyboard.hpp"

#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_log.h>

#include <cstring>

namespace orcsdr::tab5_keyboard {
namespace {

constexpr const char* kTag = "tab5_kbd";
constexpr gpio_num_t kSda = GPIO_NUM_0;
constexpr gpio_num_t kScl = GPIO_NUM_1;
constexpr gpio_num_t kInt = GPIO_NUM_50;
constexpr uint8_t kAddress = 0x6D;
constexpr uint32_t kBusHz = 400000;
constexpr int kTimeoutMs = 20;

// Register map (M5Tab5-Keyboard-Internal-FW, user_i2c_reg.h).
constexpr uint8_t kRegIntrConfig = 0x00;
constexpr uint8_t kRegIntrStatus = 0x01;
constexpr uint8_t kRegEventCount = 0x02;
constexpr uint8_t kRegKeyboardMode = 0x10;
constexpr uint8_t kRegCharLength = 0x40;
constexpr uint8_t kRegCharEvent = 0x50;
constexpr uint8_t kRegFirmware = 0xFE;
constexpr uint8_t kModeChar = 2;
constexpr uint8_t kIntrCharEnable = 0x04;
constexpr uint8_t kMaxEventBytes = 17;  // modifier + up to 16 name characters

constexpr uint32_t kIdlePollMs = 100;      // safety poll in case INT is missed
constexpr uint32_t kReprobeMs = 2000;
constexpr uint8_t kMaxEventsPerPass = 8;
constexpr uint8_t kFailuresBeforeDetach = 5;
constexpr size_t kQueueSize = 16;

i2c_master_bus_handle_t g_bus = nullptr;
i2c_master_dev_handle_t g_device = nullptr;
Status g_status;
uint32_t g_last_poll_ms = 0;
uint32_t g_last_probe_ms = 0;
uint8_t g_failures = 0;

keyboard_input::Key g_queue[kQueueSize];
size_t g_head = 0;
size_t g_count = 0;

bool write_register(uint8_t reg, uint8_t value) {
  const uint8_t frame[2] = {reg, value};
  return i2c_master_transmit(g_device, frame, sizeof(frame), kTimeoutMs) == ESP_OK;
}

bool read_registers(uint8_t reg, uint8_t* out, size_t length) {
  return i2c_master_transmit_receive(g_device, &reg, 1, out, length, kTimeoutMs) == ESP_OK;
}

void note_bus_error() {
  ++g_status.bus_errors;
  if (++g_failures >= kFailuresBeforeDetach && g_device != nullptr) {
    i2c_master_bus_rm_device(g_device);
    g_device = nullptr;
    g_status.present = false;
    ESP_LOGW(kTag, "RTL_KEYBOARD detached after repeated I2C errors");
  }
}

void push(const keyboard_input::Key& key) {
  if (g_count == kQueueSize) {
    ++g_status.dropped;
    return;
  }
  g_queue[(g_head + g_count) % kQueueSize] = key;
  ++g_count;
}

bool attach() {
  if (g_bus == nullptr) return false;
  if (i2c_master_probe(g_bus, kAddress, kTimeoutMs) != ESP_OK) return false;
  i2c_device_config_t config = {};
  config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  config.device_address = kAddress;
  config.scl_speed_hz = kBusHz;
  if (i2c_master_bus_add_device(g_bus, &config, &g_device) != ESP_OK) {
    g_device = nullptr;
    return false;
  }
  uint8_t firmware = 0;
  uint8_t mode = 0;
  // Character mode; then discard anything queued before we took over and enable its interrupt.
  const bool ok = write_register(kRegKeyboardMode, kModeChar) &&
                  read_registers(kRegKeyboardMode, &mode, 1) && mode == kModeChar &&
                  read_registers(kRegFirmware, &firmware, 1) &&
                  write_register(kRegEventCount, 0) && write_register(kRegIntrStatus, 0) &&
                  write_register(kRegIntrConfig, kIntrCharEnable);
  if (!ok) {
    i2c_master_bus_rm_device(g_device);
    g_device = nullptr;
    return false;
  }
  g_failures = 0;
  g_status.present = true;
  g_status.firmware = firmware;
  g_status.mode = mode;
  ++g_status.attach_count;
  ESP_LOGI(kTag, "RTL_KEYBOARD attached addr=0x%02x firmware=%u mode=%u", kAddress,
           static_cast<unsigned>(firmware), static_cast<unsigned>(mode));
  return true;
}

void drain() {
  uint8_t count = 0;
  if (!read_registers(kRegEventCount, &count, 1)) {
    note_bus_error();
    return;
  }
  g_failures = 0;
  if (count == 0) {
    // Nothing pending: release INT if it is still held.
    if (gpio_get_level(kInt) == 0) (void)write_register(kRegIntrStatus, 0);
    return;
  }
  uint8_t handled = 0;
  while (handled < kMaxEventsPerPass && handled < count) {
    uint8_t length = 0;
    if (!read_registers(kRegCharLength, &length, 1)) {
      note_bus_error();
      return;
    }
    if (length == 0) break;
    if (length > kMaxEventBytes) {
      // Not a legal event; reset the FIFO rather than misread the stream.
      ++g_status.undecoded;
      (void)write_register(kRegEventCount, 0);
      break;
    }
    uint8_t data[kMaxEventBytes] = {};
    if (!read_registers(kRegCharEvent, data, length)) {
      note_bus_error();
      return;
    }
    ++handled;
    keyboard_input::Key key;
    if (keyboard_input::decode_char_event(data, length, &key)) {
      ++g_status.keys;
      push(key);
    } else {
      ++g_status.undecoded;
    }
  }
  if (gpio_get_level(kInt) == 0 && handled >= count) (void)write_register(kRegIntrStatus, 0);
}

}  // namespace

void begin() {
  gpio_config_t int_pin = {};
  int_pin.pin_bit_mask = 1ULL << kInt;
  int_pin.mode = GPIO_MODE_INPUT;
  int_pin.pull_up_en = GPIO_PULLUP_ENABLE;
  (void)gpio_config(&int_pin);

  i2c_master_bus_config_t bus = {};
  bus.i2c_port = -1;  // any free controller; M5Unified keeps the internal bus
  bus.sda_io_num = kSda;
  bus.scl_io_num = kScl;
  bus.clk_source = I2C_CLK_SRC_DEFAULT;
  bus.glitch_ignore_cnt = 7;
  bus.flags.enable_internal_pullup = true;
  if (i2c_new_master_bus(&bus, &g_bus) != ESP_OK) {
    g_bus = nullptr;
    ESP_LOGW(kTag, "RTL_KEYBOARD bus unavailable; keyboard support disabled");
    return;
  }
  (void)attach();
}

void service(uint32_t now_ms) {
  if (g_bus == nullptr) return;
  if (g_device == nullptr) {
    if (now_ms - g_last_probe_ms >= kReprobeMs) {
      g_last_probe_ms = now_ms;
      (void)attach();
    }
    return;
  }
  const bool int_low = gpio_get_level(kInt) == 0;
  g_status.int_low = int_low;
  if (!int_low && now_ms - g_last_poll_ms < kIdlePollMs) return;
  g_last_poll_ms = now_ms;
  drain();
}

bool pop(keyboard_input::Key* key) {
  if (key == nullptr || g_count == 0) return false;
  *key = g_queue[g_head];
  g_head = (g_head + 1) % kQueueSize;
  --g_count;
  return true;
}

Status status() {
  Status copy = g_status;
  copy.present = g_device != nullptr;
  return copy;
}

}  // namespace orcsdr::tab5_keyboard
