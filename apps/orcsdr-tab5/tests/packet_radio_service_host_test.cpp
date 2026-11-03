#include "packet_radio_service.hpp"
#include "c6_recovery_app.hpp"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "orcsdr_board_profile.h"
#include "orcsdr_sx126x.h"
#include "wifi_app.hpp"

#include <cassert>
#include <cstring>
#include <deque>
#include <stdexcept>
#include <vector>

struct FakeQueue {
  unsigned capacity = 0, item_size = 0;
  std::deque<std::vector<unsigned char>> items;
};
struct FakeTask {};
namespace {
struct WorkerExit {};
FakeQueue queue_storage;
FakeTask task_storage;
TaskFunction_t worker_function = nullptr;
void* worker_argument = nullptr;
orcsdr_board_profile_t board{};
bool fail_queue = false, fail_task = false, c6_busy = false, retune_on_send = false;
unsigned queue_creates = 0, task_creates = 0, driver_calls = 0;
unsigned expected_frequency = 0;
enum class DriverMode { cancel_check, fail, emit_packet, emit_nine, retune_then_stop };
DriverMode driver_mode = DriverMode::fail;
}

namespace c6_recovery_app {
Status status() { return {Phase::idle, 0, 0, c6_busy}; }
}
namespace wifi_app {
RecoveryAccess recovery_access() { return RecoveryAccess::idle; }
}
extern "C" const orcsdr_board_profile_t* orcsdr_board_profile(void) { return &board; }
int64_t esp_timer_get_time() { return 1234567; }

QueueHandle_t xQueueCreate(unsigned length, unsigned item_size) {
  ++queue_creates;
  if (fail_queue) { fail_queue = false; return nullptr; }
  queue_storage.capacity = length;
  queue_storage.item_size = item_size;
  queue_storage.items.clear();
  return &queue_storage;
}
void vQueueDelete(QueueHandle_t queue) { if (queue) queue->items.clear(); }
BaseType_t xQueueSend(QueueHandle_t queue, const void* item, TickType_t) {
  if (retune_on_send) {
    retune_on_send = false;
    assert(packet_radio::request_receive(expected_frequency, false));
  }
  if (queue->items.size() == queue->capacity) return pdFALSE;
  const auto* bytes = static_cast<const unsigned char*>(item);
  queue->items.emplace_back(bytes, bytes + queue->item_size);
  return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t queue, void* item, TickType_t) {
  if (queue->items.empty()) return pdFALSE;
  std::memcpy(item, queue->items.front().data(), queue->item_size);
  queue->items.pop_front();
  return pdTRUE;
}
BaseType_t xTaskCreate(TaskFunction_t function, const char*, unsigned, void* argument,
                       unsigned, TaskHandle_t* handle) {
  ++task_creates;
  if (fail_task) { fail_task = false; return 0; }
  worker_function = function;
  worker_argument = argument;
  *handle = &task_storage;
  return pdPASS;
}
void vTaskDelete(TaskHandle_t) {}
uint32_t ulTaskNotifyTake(BaseType_t, TickType_t) { throw WorkerExit{}; }
void xTaskNotifyGive(TaskHandle_t) {}

namespace orcsdr::lora_native {
bool decode_payload(const uint8_t* data, size_t size, const Config&, Packet* packet) {
  if (!data || size == 0 || !packet) return false;
  std::strcpy(packet->text, "host parser stub");
  return true;
}
}

extern "C" esp_err_t orcsdr_sx126x_receive_longfast(
    const orcsdr_sx126x_rx_request_t* request, orcsdr_sx126x_probe_result_t* result) {
  ++driver_calls;
  result->detected = true;
  result->clock_ready = true;
  if (driver_mode == DriverMode::cancel_check) {
    assert(!request->keep_running(request->context));
    return ESP_OK;
  }
  if (driver_mode == DriverMode::fail) return ESP_FAIL;
  if (driver_mode == DriverMode::retune_then_stop && driver_calls > 1) {
    orcsdr_sx126x_rx_event_t frame{};
    frame.kind = ORCSDR_SX126X_RX_PACKET;
    frame.length = 1;
    frame.data[0] = 0x42;
    request->event(request->context, &frame);
    assert(packet_radio::request_stop());
    return ESP_OK;
  }
  orcsdr_sx126x_rx_event_t event{};
  event.kind = ORCSDR_SX126X_RX_READY;
  request->event(request->context, &event);
  event.kind = ORCSDR_SX126X_RX_REJECTED;
  request->event(request->context, &event);
  event.kind = ORCSDR_SX126X_RX_PACKET;
  event.length = 1;
  event.data[0] = 0x42;
  event.rssi_dbm = -91;
  event.snr_db = 7;
  event.dio1_high = true;
  request->event(request->context, &event);
  if (driver_mode == DriverMode::emit_nine) {
    for (unsigned i = 1; i < 9; ++i) request->event(request->context, &event);
    throw WorkerExit{};
  }
  if (driver_mode == DriverMode::emit_packet) throw WorkerExit{};
  return ESP_OK;
}

void run_worker() {
  assert(worker_function != nullptr);
  try { worker_function(worker_argument); }
  catch (const WorkerExit&) {}
}
void setup_supported_board() {
  board.has_radio = true;
  board.radio_part = "SX1262";
  board.radio_tcxo_mv = 1600;
  board.radio_tcxo_startup_us = 5000;
  board.radio_ant_polarity = ORCSDR_RADIO_ANT_EXTERNAL_LOW;
}

void startup_retry_case() {
  setup_supported_board();
  board.has_radio = false;
  assert(!packet_radio::start());
  board.has_radio = true;
  fail_queue = true;
  assert(!packet_radio::start());
  fail_task = true;
  assert(!packet_radio::start());
  assert(packet_radio::start());
  const unsigned created = task_creates;
  assert(packet_radio::start());
  assert(task_creates == created && queue_creates == 3);
}

void cancellation_and_retry_case() {
  setup_supported_board();
  assert(packet_radio::start());
  c6_busy = false;
  assert(packet_radio::request_receive(906875000, false));
  c6_busy = true;
  driver_mode = DriverMode::cancel_check;
  run_worker();
  assert(!packet_radio::status().requested);
  c6_busy = false;
  assert(packet_radio::request_receive(906875000, false));
  driver_mode = DriverMode::fail;
  const unsigned before = driver_calls;
  run_worker();
  assert(driver_calls == before + 1);
  assert(packet_radio::status().last_error == ESP_FAIL);
  assert(!packet_radio::status().requested);
  run_worker();
  assert(driver_calls == before + 1); // no automatic retry
  assert(packet_radio::request_receive(906875000, false));
  const unsigned retry_before = driver_calls;
  run_worker();
  assert(driver_calls == retry_before + 1);
  assert(packet_radio::status().last_error == ESP_FAIL);
}

void current_packet_case() {
  setup_supported_board();
  assert(packet_radio::start());
  assert(packet_radio::request_receive(906875000, true));
  driver_mode = DriverMode::emit_packet;
  run_worker(); // fake driver exits after the service has handled callbacks
  const auto state = packet_radio::status();
  assert(state.packets == 1 && state.rejected == 1 && state.receiving);
  packet_radio::Packet packet{};
  assert(packet_radio::take_packet(&packet));
  assert(packet.frequency_hz == 906875000 && packet.rssi_dbm == -91);
  assert(packet.dio1_high && packet.interpreted);
}

void overflow_case() {
  setup_supported_board();
  assert(packet_radio::start());
  assert(packet_radio::request_receive(906875000, false));
  driver_mode = DriverMode::emit_nine;
  run_worker();
  assert(packet_radio::status().packets == 9);
  assert(packet_radio::status().dropped == 1);
  packet_radio::Packet packet{};
  for (unsigned i = 0; i < 8; ++i) assert(packet_radio::take_packet(&packet));
  assert(packet.sequence == 8);
  assert(!packet_radio::take_packet(&packet));
}

void stale_generation_case() {
  setup_supported_board();
  assert(packet_radio::start());
  const uint32_t first = 906875000, second = 906900000;
  assert(packet_radio::request_receive(first, false));
  expected_frequency = second;
  retune_on_send = true; // advance generation inside the callback's queue send
  driver_mode = DriverMode::retune_then_stop;
  run_worker();
  packet_radio::Packet packet{};
  assert(!packet_radio::take_packet(&packet));
  const auto state = packet_radio::status();
  assert(!state.requested && state.generation == 3);
  assert(state.stale_discarded == 2);
}

int main(int argc, char** argv) {
  assert(argc == 2);
  if (!std::strcmp(argv[1], "startup")) startup_retry_case();
  else if (!std::strcmp(argv[1], "cancel_retry")) cancellation_and_retry_case();
  else if (!std::strcmp(argv[1], "current")) current_packet_case();
  else if (!std::strcmp(argv[1], "overflow")) overflow_case();
  else if (!std::strcmp(argv[1], "stale")) stale_generation_case();
  else assert(false && "unknown scenario");
}
