#include "hfdl_dashboard_ui.hpp"

namespace orcsdr::hfdl_ui {

void Controller::reset() {
  detail_open_ = false;
  revision_ = 1;
  detail_record_ = {};
}

Action Controller::touch(int32_t /*x*/, int32_t /*y*/, Model& /*model*/) {
  // Hit-testing for list rows and detail close will be filled once layout is fixed.
  return {};
}

bool Controller::open_detail(uint32_t seq, const Model& model) {
  const auto& snap = model.snapshot();
  for (size_t i = 0; i < snap.message_count; ++i) {
    if (snap.messages[i].seq == seq) {
      detail_record_ = snap.messages[i];
      detail_open_ = true;
      bump();
      return true;
    }
  }
  return false;
}

void Controller::close_detail() {
  if (detail_open_) {
    detail_open_ = false;
    bump();
  }
}

}  // namespace orcsdr::hfdl_ui
