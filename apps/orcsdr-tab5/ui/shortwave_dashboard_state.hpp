#pragma once

#include "shortwave_model.hpp"

#include <cstdint>

namespace orcsdr::shortwave {

enum class Modal : uint8_t {
  none,
  frequency,
  memory_label,
  memory_notes,
  log_antenna,
  log_notes,
};

class DashboardState {
 public:
  void open(Modal modal);
  void close_modal();
  bool select_tab(Tab tab) {
    const bool leaving_hunt = tab_ == Tab::hunt && tab != Tab::hunt;
    tab_ = tab;
    return leaving_hunt;
  }
  Modal modal() const { return modal_; }
  Tab tab() const { return tab_; }
  bool background_redraw_allowed() const { return modal_ == Modal::none; }
  bool spectrum_allowed() const { return modal_ == Modal::none && tab_ == Tab::live; }

 private:
  Modal modal_ = Modal::none;
  Tab tab_ = Tab::live;
};

}  // namespace orcsdr::shortwave
