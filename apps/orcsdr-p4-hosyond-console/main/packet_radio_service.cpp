#include "packet_radio_service.hpp"
#include <atomic>
#include <cstring>
#include "c6_recovery_app.hpp"
#include "wifi_app.hpp"
#include "orcsdr_board_profile.h"
#include "orcsdr_sx126x.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
namespace packet_radio {
namespace {
portMUX_TYPE g_lock=portMUX_INITIALIZER_UNLOCKED;
Status g_status;
QueueHandle_t g_packets=nullptr;
std::atomic<TaskHandle_t> g_worker{nullptr};
std::atomic<bool> g_initializing{false};
struct Operation {uint32_t generation,frequency_hz;orcsdr_sx126x_probe_result_t* probe;};
bool update_busy() {
  return c6_recovery_app::status().busy || wifi_app::recovery_access()!=wifi_app::RecoveryAccess::idle;
}
bool keep_running(void* context) {
  if(update_busy()) {request_stop();return false;}
  const auto* operation=static_cast<Operation*>(context);
  portENTER_CRITICAL(&g_lock);
  const bool run=g_status.requested && g_status.generation==operation->generation;
  portEXIT_CRITICAL(&g_lock);
  return run;
}
void event(void* context,const orcsdr_sx126x_rx_event_t* input) {
  auto* operation=static_cast<Operation*>(context);
  Packet packet{};
  if(input->kind==ORCSDR_SX126X_RX_PACKET) {
    packet.interpreted=orcsdr::lora_native::decode_payload(input->data,input->length,{},&packet.mesh);
    packet.generation=operation->generation;packet.frequency_hz=operation->frequency_hz;
    packet.received_us=esp_timer_get_time();packet.length=input->length;
    packet.rssi_dbm=input->rssi_dbm;packet.snr_db=input->snr_db;packet.dio1_high=input->dio1_high;
  }
  portENTER_CRITICAL(&g_lock);
  if(!g_status.requested || g_status.generation!=operation->generation) {
    portEXIT_CRITICAL(&g_lock);return;
  }
  if(input->kind==ORCSDR_SX126X_RX_READY) {
    g_status.starting=false;g_status.receiving=true;g_status.frequency_hz=operation->frequency_hz;
    g_status.detected=operation->probe->detected;g_status.clock_checked=operation->probe->clock_ready;
  } else if(input->kind==ORCSDR_SX126X_RX_REJECTED) {
    ++g_status.rejected;
  } else {
    packet.sequence=++g_status.packets;
    g_status.latest=packet;
  }
  portEXIT_CRITICAL(&g_lock);
  if(input->kind==ORCSDR_SX126X_RX_PACKET && xQueueSend(g_packets,&packet,0)!=pdTRUE) {
    portENTER_CRITICAL(&g_lock);++g_status.dropped;portEXIT_CRITICAL(&g_lock);
  }
}
void worker(void*) {
  for(;;) {
    const Status requested=status();
    if(!requested.requested) {ulTaskNotifyTake(pdTRUE,portMAX_DELAY);continue;}
    orcsdr_sx126x_probe_result_t probe{};
    Operation operation{requested.generation,requested.requested_hz,&probe};
    portENTER_CRITICAL(&g_lock);
    if(g_status.generation==operation.generation) {
      g_status.starting=true;g_status.last_error=ESP_OK;
      g_status.detected=false;g_status.clock_checked=false;
    }
    portEXIT_CRITICAL(&g_lock);
    const orcsdr_sx126x_rx_request_t input{requested.requested_hz,requested.external_antenna,keep_running,event,&operation};
    const esp_err_t result=orcsdr_sx126x_receive_longfast(&input,&probe);
    portENTER_CRITICAL(&g_lock);
    g_status.starting=false;g_status.receiving=false;g_status.frequency_hz=0;
    if(g_status.generation==operation.generation) {
      g_status.last_error=result;g_status.detected=probe.detected;g_status.clock_checked=probe.clock_ready;
      // Errors require an explicit new request, rather than an unbounded reset loop.
      g_status.requested=false;
    } else if(!g_status.requested && result!=ESP_OK) {
      g_status.last_error=result;
    }
    portEXIT_CRITICAL(&g_lock);
  }
}
} // namespace
Status status() {
  portENTER_CRITICAL(&g_lock);const Status copy=g_status;portEXIT_CRITICAL(&g_lock);
  return copy;
}
bool start() {
  if(g_initializing.exchange(true))return status().started;
  const auto* profile=orcsdr_board_profile();
  const bool supported=profile->has_radio && profile->radio_part && !std::strcmp(profile->radio_part,"SX1262") &&
    profile->radio_tcxo_mv && profile->radio_tcxo_startup_us && profile->radio_ant_polarity!=ORCSDR_RADIO_ANT_UNKNOWN;
  portENTER_CRITICAL(&g_lock);g_status.supported=supported;portEXIT_CRITICAL(&g_lock);
  if(!supported) {g_initializing=false;return false;}
  g_packets=xQueueCreate(8,sizeof(Packet));
  if(!g_packets) {g_initializing=false;return false;}
  TaskHandle_t task=nullptr;
  if(xTaskCreate(worker,"packet_radio",8192,nullptr,1,&task)!=pdPASS) {
    vQueueDelete(g_packets);g_packets=nullptr;g_initializing=false;return false;
  }
  g_worker=task;
  portENTER_CRITICAL(&g_lock);g_status.started=true;portEXIT_CRITICAL(&g_lock);
  return true;
}
bool receive_available() {
  if(update_busy())return false;
  portENTER_CRITICAL(&g_lock);const bool available=g_status.supported&&g_status.started;portEXIT_CRITICAL(&g_lock);
  return available;
}
bool request_receive(uint32_t frequency_hz,bool external_antenna) {
  if(frequency_hz<150000000 || frequency_hz>960000000 || update_busy())return false;
  portENTER_CRITICAL(&g_lock);
  if(!g_status.started) {portEXIT_CRITICAL(&g_lock);return false;}
  ++g_status.generation;
  g_status.requested=true;g_status.requested_hz=frequency_hz;g_status.external_antenna=external_antenna;
  portEXIT_CRITICAL(&g_lock);
  xTaskNotifyGive(g_worker.load());
  return true;
}
bool request_stop() {
  portENTER_CRITICAL(&g_lock);
  if(!g_status.started) {portEXIT_CRITICAL(&g_lock);return false;}
  if(g_status.requested) {++g_status.generation;g_status.requested=false;}
  portEXIT_CRITICAL(&g_lock);
  xTaskNotifyGive(g_worker.load());
  return true;
}
bool take_packet(Packet* packet) {
  if(!packet || !status().started)return false;
  Packet candidate{};
  for(unsigned i=0;i<8 && xQueueReceive(g_packets,&candidate,0)==pdTRUE;++i) {
    portENTER_CRITICAL(&g_lock);
    const bool current=g_status.requested && candidate.generation==g_status.generation;
    if(!current)++g_status.stale_discarded;
    portEXIT_CRITICAL(&g_lock);
    if(current){*packet=candidate;return true;}
  }
  return false;
}
} // namespace packet_radio
