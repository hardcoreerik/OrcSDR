#include "hfdl_dashboard_ui.hpp"
#include <algorithm>

namespace orcsdr::hfdl_ui {
void Controller::reset(){detail_open_=false;revision_=1;detail_record_={};}
bool Controller::open_detail(uint32_t seq,Model& model){
  MessageRecord found{};
  if(!model.find_message(seq,&found))return false;
  detail_record_=found;detail_open_=true;bump();return true;
}
void Controller::close_detail(){if(detail_open_){detail_open_=false;bump();}}
Action Controller::touch(int32_t x,int32_t y,Model& model,const layout::View& view){
  if(!view.valid || !view.screen.contains(x,y))return {};
  if(view.home.contains(x,y))return {ActionKind::exit_home};
  if(detail_open_){
    if(view.close.contains(x,y)){close_detail();return {ActionKind::changed};}
    return {};
  }
  const auto& snap=model.snapshot();
  for(size_t i=0;i<snap.message_count && i<view.visible_rows;++i){
    if(view.rows[i].contains(x,y) && open_detail(snap.messages[i].seq,model))
      return {ActionKind::changed};
  }
  if(view.newer.contains(x,y)){
    const size_t offset=model.message_offset();
    if(offset && model.set_message_offset(offset>view.visible_rows?
                                         offset-view.visible_rows:0))
      return {ActionKind::changed};
  }
  if(view.older.contains(x,y)){
    const size_t offset=model.message_offset();
    if(offset+view.visible_rows<snap.messages_held &&
       model.set_message_offset(offset+view.visible_rows))
      return {ActionKind::changed};
  }
  return {};
}
}  // namespace orcsdr::hfdl_ui
