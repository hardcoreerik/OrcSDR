#include "../ui/hfdl_dashboard_model.hpp"
#include "../ui/hfdl_dashboard_ui.hpp"
#include "../ui/hfdl_dashboard_layout.hpp"
#include "../hfdl/hfdl_decoder.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
using namespace orcsdr;
namespace L=orcsdr::hfdl_ui::layout;
static void check_view(int w,int h,int expected_rows){
  const L::View v=L::make_view(w,h);
  assert(v.valid && v.visible_rows==expected_rows);
  assert(L::inside(v.screen,v.header) && L::inside(v.screen,v.summary));
  assert(L::inside(v.screen,v.list) && L::inside(v.screen,v.footer));
  assert(L::inside(v.screen,v.detail));
  for(int i=0;i<v.visible_rows;++i){
    assert(L::inside(v.list,v.rows[i]));
    assert(v.rows[i].w>=44 && v.rows[i].h>=44);
    for(int j=i+1;j<v.visible_rows;++j)assert(!L::overlaps(v.rows[i],v.rows[j]));
  }
  assert(L::inside(v.footer,v.newer)&&L::inside(v.footer,v.older));
  assert(!L::overlaps(v.newer,v.older));
  assert(L::inside(v.detail,v.close));
  hfdl_ui::Model model;
  hfdl_ui::Controller controller;
  for(unsigned i=0;i<16;++i){
    hfdl_ui::MessageRecord m{};
    m.frequency_hz=10081000u+i*1000;
    std::snprintf(m.preview,sizeof(m.preview),"VERIFIED TEST %u",i);
    assert(model.post_message(m));
    model.service();
  }
  auto snap=model.snapshot();
  assert(snap.messages_total==16&&snap.messages[0].seq==16);
  const auto action=controller.touch(v.rows[0].x+5,v.rows[0].y+5,model,v);
  assert(action.kind==hfdl_ui::ActionKind::changed&&controller.detail_open());
  assert(controller.detail_record().seq==16);
  assert(controller.touch(v.close.x+5,v.close.y+5,model,v).kind==
         hfdl_ui::ActionKind::changed && !controller.detail_open());
  assert(controller.touch(v.older.x+5,v.older.y+5,model,v).kind==
         hfdl_ui::ActionKind::changed);
  assert(model.snapshot().message_offset==v.visible_rows);
  assert(controller.touch(v.newer.x+5,v.newer.y+5,model,v).kind==
         hfdl_ui::ActionKind::changed);
  assert(model.snapshot().message_offset==0);
  assert(controller.touch(v.home.x+5,v.home.y+5,model,v).kind==
         hfdl_ui::ActionKind::exit_home);
  assert(controller.touch(-1,-1,model,v).kind==hfdl_ui::ActionKind::none);
}
static void check_model(){
  hfdl_ui::Model model;
  assert(model.snapshot().messages_total==0);
  hfdl_ui::MessageRecord m{};
  m.frequency_hz=11184000;std::strcpy(m.preview,"TEST VERIFIED SPDU");
  for(int i=0;i<8;++i)assert(model.post_message(m));
  assert(!model.post_message(m));
  model.post_receiver_state(hfdl_ui::ReceiverState::running);
  model.service();
  assert(model.snapshot().queue_drops==1);
  assert(model.snapshot().messages_total==8);
  assert(model.snapshot().messages[0].seq==8);
  assert(model.snapshot().receiver==hfdl_ui::ReceiverState::running);
  hfdl_ui::MessageRecord found{};
  assert(model.find_message(8,&found)&&found.seq==8);
  assert(!model.find_message(50,&found));
  for(int i=0;i<28;++i){assert(model.post_message(m));model.service();}
  assert(model.snapshot().messages_held==24);
  assert(model.snapshot().messages_total==36);
  assert(model.snapshot().messages[0].seq==36);
  assert(!model.find_message(1,&found));
  assert(model.set_message_offset(999));
  assert(model.snapshot().message_offset==23);
  assert(model.snapshot().message_count==1);
  model.post_receiver_state(hfdl_ui::ReceiverState::error);
  model.service();assert(model.snapshot().receiver==hfdl_ui::ReceiverState::error);
}
static void check_frontend(){
  hfdl::Decoder d;
  assert(d.valid_config());
  constexpr size_t n=48000;
  static float tone[n];
  for(size_t i=0;i<n;++i)
    tone[i]=std::sin(2.0*3.141592653589793*1440.0*static_cast<double>(i)/48000.0);
  assert(!d.process_audio(tone,n));
  assert(d.frontend_stats().input_samples==n);
  assert(d.frontend_stats().channel_samples==12000);
  assert(d.frontend_stats().channel_power>0.05f);
  assert(d.messages_posted()==0&&d.spdus_seen()==0);
  hfdl::Config invalid{};invalid.sample_rate=44100;d.set_config(invalid);
  assert(!d.valid_config() && !d.process_audio(tone,1));
  assert(d.frontend_stats().invalid_blocks==1);
  hfdl::Config slow{};slow.sample_rate=12000;d.set_config(slow);
  static float silence[400]{};
  for(int i=0;i<1000;++i)assert(!d.process_audio(silence,400));
  assert(d.frontend_stats().input_samples==400000);
  assert(d.spdus_seen()==0&&d.messages_posted()==0);
}
int main(){
  check_view(1280,720,7);
  check_view(720,720,6);
  check_view(800,400,3);
  check_view(568,1232,8);
  assert(!L::make_view(640,480).valid);
  check_model();
  check_frontend();
  std::printf("hfdl_dashboard_test PASS / Model %zu bytes / Snapshot %zu bytes\n",
             sizeof(hfdl_ui::Model),sizeof(hfdl_ui::Snapshot));
}
