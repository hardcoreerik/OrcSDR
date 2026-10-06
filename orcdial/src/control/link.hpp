#pragma once
#include "protocol.hpp"
#include "secure_runtime.hpp"
#include "../state.hpp"
#include "../controller.hpp"
#include <Arduino.h>
#include <esp_now.h>
#include <freertos/queue.h>

namespace orc {
class Link {
 public:
  bool begin();
  void poll();
  void start_pairing();
  void forget_and_pair();  // pairs only if the forget succeeded (a failed revocation blocks replacement pairing)
  void cancel_pairing() { secure_.action(secure::Action::cancel); }
  void confirm_pairing(uint32_t code) { secure_.action(secure::Action::confirm,code); }
  void connect() { secure_.action(secure::Action::connect); }
  void forget() { secure_.action(secure::Action::forget); }
  void boot_connect(bool enabled) { secure_.action(secure::Action::boot,enabled); }
  secure::Status security_status() const { return secure_.status(); }
  void disconnect(bool notify = true);
  bool paused() const { return paused_; }
  bool command(Type type, int32_t value);
  bool command_action(Action action);
  bool connected() const { return connected_ && secure_.status().state==secure::State::connected; }
  bool pairing() const { auto s=secure_.status().state;return s==secure::State::searching || s==secure::State::exchanging || s==secure::State::verify; }
  bool pending() const { return pending_sequence_ != 0; }
  uint32_t pending_sequence() const { return pending_sequence_; }
  const RadioState& state() const { return state_; }
  uint32_t last_ack() const { return last_ack_; }
  // Radio diagnostics: counters and a bounded event trace that separate "frame accepted by the
  // ESP-NOW stack" from "frame completed on air" and show which channel each event happened on.
  // Counters are updated from several tasks without locking; they are for diagnosis only.
  uint8_t channel() const { return channel_; }
  void rf_report() const;
  void rf_reset();
  void rf_trace(uint8_t mode) { trace_mode_ = mode; }  // 0 off (default), 1 on, 2 on for the first 60 s after boot
 private:
  enum RfKind : uint8_t { rf_tx_accepted = 1, rf_tx_refused, rf_cb_ok, rf_cb_fail, rf_rx, rf_scan };
  struct RfEvent { uint32_t ms; uint8_t kind, channel; };
  static constexpr size_t rf_ring_size = 64;
  static void sent(const uint8_t* mac, esp_now_send_status_t status);
  static void note(RfKind kind);
  static bool evidence();  // a unicast frame was acknowledged recently: the peer hears us on this channel
  struct Incoming { uint8_t mac[6]; uint8_t data[packet_size]; uint8_t size; };
  static void receive(const uint8_t* mac, const uint8_t* data, int size);
  static bool transmit(const uint8_t* mac,const uint8_t* wire);
  static void scan(uint8_t channel);
  void handle(const Incoming& incoming);
  bool send(Type type, int32_t value = 0, uint32_t sequence = 0, ActionKind action = ActionKind::none);
  uint32_t device_id_ = 0;
  uint32_t next_sequence_ = 1;
  uint32_t pending_sequence_ = 0;
  uint32_t last_ack_ = 0;
  uint32_t last_sender_ = 0;
  uint32_t last_state_sequence_ = 0;
  uint32_t last_rx_ms_ = 0;
  uint32_t last_tx_ms_ = 0;
  uint32_t pending_ms_ = 0;
  uint8_t retries_ = 0;
  bool connected_ = false;
  bool paused_ = false;
  Packet pending_{};
  RadioState state_{};
  secure::Runtime secure_;
  static Link* active_;
  static volatile uint8_t channel_;
  static volatile uint32_t last_unicast_ok_ms_;
  static volatile uint32_t set_channel_errors_;
  uint8_t saved_channel_ = 0;
  uint32_t connected_at_ = 0;
  static volatile uint8_t trace_mode_;
  static RfEvent ring_[rf_ring_size];
  static volatile uint32_t ring_head_, ring_tail_;
  static portMUX_TYPE ring_lock_;
  static volatile uint32_t tx_accepted_, tx_refused_, cb_ok_, cb_fail_, rx_frames_;
  static volatile uint32_t tx_by_channel_[14], rx_by_channel_[14];
};
} // namespace orc
