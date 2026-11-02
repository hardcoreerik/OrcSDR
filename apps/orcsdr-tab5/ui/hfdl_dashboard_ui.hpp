#pragma once
// Pure UI controller. All touch targets come from the same layout as the renderer.
#include <cstdint>
#include "hfdl_dashboard_layout.hpp"
#include "hfdl_dashboard_model.hpp"

namespace orcsdr::hfdl_ui {
enum class ActionKind:uint8_t { none,changed,exit_home };
struct Action{ActionKind kind=ActionKind::none;};
class Controller {
 public:
  void reset();
  uint32_t revision() const {return revision_;}
  Action touch(int32_t x,int32_t y,Model& model,const layout::View& view);
  bool open_detail(uint32_t seq,Model& model);
  void close_detail();
  bool detail_open() const {return detail_open_;}
  const MessageRecord& detail_record() const {return detail_record_;}
 private:
  void bump(){++revision_;}
  bool detail_open_=false;
  uint32_t revision_=1;
  MessageRecord detail_record_{};
};
}  // namespace orcsdr::hfdl_ui
