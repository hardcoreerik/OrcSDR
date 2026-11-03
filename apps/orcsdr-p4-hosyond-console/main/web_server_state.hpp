#pragma once

#include <cstddef>
#include <cstdio>
#include <cstring>

#include "freertos/FreeRTOS.h"

namespace web_server {
struct Status {
  bool enabled = false;
  bool listening = false;
  char url[40] = "offline";
};

// Small shared state boundary: writers update URL and flags together, while
// readers receive a bounded value copy rather than a pointer to mutable storage.
class State {
 public:
  Status snapshot() const {
    Status copy{};
    portENTER_CRITICAL(&mux_);
    copy.enabled = enabled_;
    copy.listening = listening_;
    std::memcpy(copy.url, url_, sizeof(copy.url));
    portEXIT_CRITICAL(&mux_);
    copy.url[sizeof(copy.url) - 1] = '\0';
    return copy;
  }

  void set_enabled_after_persist(bool enabled, bool persisted) {
    if (!persisted) return;
    portENTER_CRITICAL(&mux_);
    enabled_ = enabled;
    portEXIT_CRITICAL(&mux_);
  }

  void set_listening(bool listening, const char* ip = nullptr) {
    char next_url[sizeof(url_)] = "offline";
    if (listening && ip && *ip) std::snprintf(next_url, sizeof(next_url), "http://%s/", ip);
    portENTER_CRITICAL(&mux_);
    listening_ = listening;
    std::memcpy(url_, next_url, sizeof(url_));
    url_[sizeof(url_) - 1] = '\0';
    portEXIT_CRITICAL(&mux_);
  }

  bool claim_manager() {
    portENTER_CRITICAL(&mux_);
    const bool claimed = !manager_started_;
    if (claimed) manager_started_ = true;
    portEXIT_CRITICAL(&mux_);
    return claimed;
  }

  void manager_creation_failed() {
    portENTER_CRITICAL(&mux_);
    manager_started_ = false;
    portEXIT_CRITICAL(&mux_);
  }

 private:
  mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  bool enabled_ = false;
  bool listening_ = false;
  bool manager_started_ = false;
  char url_[40] = "offline";
};
}  // namespace web_server
