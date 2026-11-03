#include "lora_dashboard_host_fakes.hpp"

void landscape_navigation_and_revocation() {
  setup(1280,720);
  auto s=packet_profile();
  lora::show_documentation_view(lora::View::tx_lab,s);
  assert(lora::active()&&lora::view()==lora::View::tx_lab);
  assert(has("ARM LOCKED")&&has("SEND LOCKED"));
  assert(lora::handle_touch(100,560).kind==lora::ActionKind::none);
  assert(lora::handle_touch(430,560).kind==lora::ActionKind::none);
  const size_t prior=host_display::prims.size();
  lora::update(s);
  assert(host_display::prims.size()==prior); // no idle redraw

  host_display::reset();
  lora::handle_touch(950,190); // messages mode
  assert(has("COMPOSE PACKET")&&has("SEND LOCKED"));
  host_display::reset();
  lora::handle_touch(1120,190); // dual radio
  assert(has("RTL-SDR / IQ MONITOR"));
  assert(has("SX1262 / PACKET"));

  host_display::reset();
  s.tx.readiness=tx::Readiness::absent; // device disappeared
  s.revision++;
  lora::update(s);
  assert(lora::view()==lora::View::overview);
  assert(full_clears()==1);
  assert(lora::handle_touch(1200,670).kind==lora::ActionKind::select_view);
  assert(lora::handle_touch(1200,670).value==4); // 5 tabs, not hidden 6th
  host_display::reset();
  s.tx.readiness=tx::Readiness::profile_only; // radio reattached
  s.revision++;
  lora::update(s);
  assert(full_clears()==1); // rebuild navigation when radio comes back
  assert(lora::handle_touch(1200,670).value==5); // 6th tab is back
  std::puts("INTEGRATED_LANDSCAPE_REVOCATION_AND_REATTACH PASS");
}
void admitted_controls_still_only_return_actions() {
  setup(1280,720);
  auto s=packet_profile();
  s.tx.readiness=tx::Readiness::tx_verified;
  s.tx.antenna_path_verified=true;
  s.tx.regional_tx_profile_approved=true;
  s.tx.frequency_approved=true;
  s.tx.frequency_hz=915000000;
  s.tx.max_payload_bytes=24;
  lora::show_documentation_view(lora::View::tx_lab,s);
  assert(lora::handle_touch(120,559).kind==lora::ActionKind::tx_request_arm);
  assert(lora::handle_touch(450,559).kind==lora::ActionKind::none);
  s.tx.operator_armed=true;
  host_display::reset();
  lora::update(s);
  assert(has("DISARM TX")&&has("SEND TEST"));
  assert(full_clears()==0); // bounded updates only
  assert(lora::handle_touch(450,559).kind==lora::ActionKind::tx_send_test_packet);
  assert(lora::handle_touch(120,559).kind==lora::ActionKind::tx_request_disarm);
  s.tx.busy=true; // RF backend is busy
  lora::update(s);
  assert(lora::handle_touch(450,559).kind==lora::ActionKind::none);
  assert(lora::handle_touch(120,559).kind==lora::ActionKind::none);
  std::puts("INTEGRATED_TX_ACTION_GATES PASS");
}
void portrait_presence_and_ui(){
  setup(568,1232);auto s=packet_profile();lora::enter(s);
  const auto layout=lora::tx_native::make({0,0,568,1232});
  assert(lora::view()==lora::View::tx_lab&&has("ARM LOCKED"));
  for(unsigned page=0;page<3;++page){const auto r=layout.mode(page);lora::handle_touch(r.x+r.w/2,r.y+r.h/2);assert(lora::select_tx_page(page));}
  host_display::reset();s.tx.readiness=tx::Readiness::absent;++s.revision;lora::update(s);
  assert(lora::view()==lora::View::overview&&!has("ARM TX")&&!has("SEND TEST"));
  s.tx.readiness=tx::Readiness::profile_only;++s.revision;lora::update(s);
  assert(lora::select_view(lora::View::tx_lab));
  assert(has("ARM LOCKED")&&has("SEND LOCKED"));
  std::puts("INTEGRATED_PORTRAIT_REVOCATION_AND_REATTACH PASS");
}
void rx_only_regression() {
  setup(1280,720);
  lora::Snapshot s{};
  s.frequency_hz=906875000u;
  s.span_hz=500000u;
  s.bandwidth_hz=250000u;
  lora::show_documentation_view(lora::View::overview,s);
  assert(has("RTL STOPPED")&&has("FREQ MHz"));
  assert(!has("TX LAB"));
  assert(lora::handle_touch(1190,665).kind==lora::ActionKind::select_view);
  assert(lora::handle_touch(1190,665).value==4);
  assert(lora::handle_touch(110,560).kind==lora::ActionKind::none);
  std::puts("INTEGRATED_RX_ONLY PASS");
}
void native_safe_views() {
  for(bool vertical:{false,true}) {
    setup(vertical?568:1232,vertical?1232:568);
    const orcsdr::audio_header::HeaderRect safe{72,72,vertical?424:1088,vertical?1088:424};
    const auto layout=lora::tx_native::make(safe);assert(layout.valid);
    assert(lora::set_tx_viewport(safe));
    auto s=packet_profile();lora::enter(s);
    for(int page=0;page<3;++page) {
      host_display::reset();
      const auto mode=layout.mode(page);
      lora::handle_touch(mode.x+mode.w/2,mode.y+mode.h/2);
      lora::draw();
      assert(has("PROFILE ONLY")&&has("ARM LOCKED")&&has("SEND LOCKED"));
      if(!vertical&&page==0)assert(has("MODULATION: LORA / SX1262"));
      std::vector<Prim> text_boxes;
      for(const auto& p:host_display::prims) {
        if(p.w<=0||p.h<=0)continue;
        if(p.kind==Prim::text)assert(p.h>=16); // no unreadable 8-pixel native labels
        if(!(p.x>=safe.x&&p.y>=safe.y&&p.x+p.w<=safe.x+safe.w&&p.y+p.h<=safe.y+safe.h)) {
          std::fprintf(stderr,"native bounds %s: %s [%d,%d %dx%d]\n",vertical?"portrait":"landscape",p.str.c_str(),p.x,p.y,p.w,p.h);assert(false);
        }
        if(p.kind==Prim::text) {
          for(const auto& before:text_boxes) {
            if(before.x==p.x&&before.y==p.y&&before.str==p.str)continue;
            const bool overlap=p.x<before.x+before.w&&before.x<p.x+p.w&&p.y<before.y+before.h&&before.y<p.y+p.h;
            if(overlap)std::fprintf(stderr,"native text overlap: %s / %s\n",before.str.c_str(),p.str.c_str());
            assert(!overlap);
          }
          text_boxes.push_back(p);
        }
      }
      for(bool send:{false,true}) {
        const auto r=layout.action(send);assert(r.w>=44&&r.h>=44);
        assert(lora::handle_touch(r.x+r.w/2,r.y+r.h/2).kind==lora::ActionKind::none);
      }
      host_display::reset();s.revision++;lora::update(s);
      assert(host_display::prims.empty()); // receiver revisions cannot flash TX cards
    }
    host_display::reset();s.tx.readiness=tx::Readiness::absent;lora::update(s);
    assert(lora::view()==lora::View::overview&&!has("ARM TX")&&!has("SEND TEST"));
    s.tx.readiness=tx::Readiness::profile_only;lora::update(s);
    assert(lora::select_view(lora::View::tx_lab));
    assert(has("SX1262 PACKET TX")||has("LORA / SX1262 TX LAB"));
  }
  assert(lora::set_tx_viewport({0,0,1280,720}));
  std::puts("INTEGRATED_NATIVE_SAFE_TX_VIEWS PASS");
}
void message_only_redraws() {
  struct Case { int w,h; bool native; };
  for (const auto c : {Case{1280,720,false},Case{568,1232,false},
                       Case{1232,568,true},Case{568,1232,true}}) {
    setup(c.w,c.h);
    auto s=packet_profile();
    if(c.native) {
      const orcsdr::audio_header::HeaderRect safe{72,72,c.w-144,c.h-144};
      assert(lora::set_tx_viewport(safe));
      lora::enter(s);
    } else if(c.w==1280) {
      lora::show_documentation_view(lora::View::tx_lab,s);
    } else {
      lora::enter(s);
    }
    assert(lora::select_tx_page(1));
    host_display::reset();
    s.event_count=1;
    auto& e=s.events[0];e.verified=true;e.sender=0x1234;e.packet_id=1;e.received_ms=100;
    std::snprintf(e.text,sizeof(e.text),"First verified mesh message");
    ++s.revision;lora::update(s);
    assert(has("First verified"));
    assert(full_clears()==0);
    assert(host_display::prims.size()<12); // one card value region, no chrome/actions

    host_display::reset();
    e.packet_id=2;e.received_ms=200;
    std::snprintf(e.text,sizeof(e.text),"Second verified message replaces the first");
    ++s.revision;lora::update(s);
    assert(has("Second verified"));
    assert(!has("First verified"));
    assert(full_clears()==0);

    host_display::reset();
    e.verified=false;
    ++s.revision;lora::update(s);
    assert(has("UNVERIFIED EVENT / TEXT HIDDEN"));
    assert(!has("Second verified"));
    assert(full_clears()==0);
  }
  std::puts("INTEGRATED_MESSAGE_ONLY_REDRAWS PASS");
}
void packet_rx_control_states() {
  struct Case { int w,h; bool native; };
  for (const auto c : {Case{1280,720,false},Case{568,1232,false},
                       Case{1232,568,true},Case{568,1232,true}}) {
    setup(c.w,c.h);
    auto s=packet_profile();
    orcsdr::audio_header::HeaderRect safe{};
    orcsdr::lora::tx_native::Layout layout{};
    if(c.native) {
      safe={72,72,c.w-144,c.h-144};
      layout=orcsdr::lora::tx_native::make(safe);
      assert(layout.valid);
      assert(lora::set_tx_viewport(safe));
      lora::enter(s);
    } else {
      assert(lora::set_tx_viewport({0,0,1280,720}));
      if(c.w==1280) lora::show_documentation_view(lora::View::tx_lab,s);
      else lora::enter(s);
    }
    assert(lora::select_tx_page(1));
    const bool vertical=c.h>c.w;
    assert(has("SX1262 / LONGFAST / MMCX1"));
    assert(has("RX FREQUENCY")&&has("LONGFAST / MMCX1"));
    assert(has("TX COMPOSER UNAVAILABLE")&&has("SEND LOCKED")&&has("ARM LOCKED"));
    bool composer_status_bounded=false;
    const int rx_card_bottom=c.native?layout.y(vertical?796:481):(vertical?796:481);
    const int activity_card_bottom=c.native?layout.y(vertical?820:519):(vertical?820:519);
    for(const auto& p:host_display::prims) if(p.kind==Prim::text&&
        p.str=="TX COMPOSER UNAVAILABLE") {
      int x,y,w,h;assert(host_display::effective(p,&x,&y,&w,&h));
      assert(y>=rx_card_bottom);
      assert(y+h<=activity_card_bottom);
      composer_status_bounded=true;
    }
    assert(composer_status_bounded);
    s.event_count=1;
    auto& event=s.events[0];event.verified=true;event.sender=0x2345;
    event.packet_id=9;event.received_ms=300;
    std::snprintf(event.text,sizeof(event.text),"A populated message stays separate from RX controls");
    s.packet_rx_supported=true;
    s.packet_rx_available=true;
    host_display::reset();
    lora::update(s);
    assert(has("START RX")&&has("A populated message"));
    assert(full_clears()==0);
    assert(!s.tx.operator_armed);
    std::vector<Prim> changed_text;
    for(const auto& p:host_display::prims) if(p.kind==Prim::text) changed_text.push_back(p);
    for(size_t i=0;i<changed_text.size();++i) for(size_t j=i+1;j<changed_text.size();++j) {
      const auto& a=changed_text[i];const auto& b=changed_text[j];
      const bool overlap=a.x<b.x+b.w&&b.x<a.x+a.w&&a.y<b.y+b.h&&b.y<a.y+a.h;
      if(overlap) std::fprintf(stderr,"packet RX text overlap %s [%d,%d %dx%d] / %s [%d,%d %dx%d]\n",
                               a.str.c_str(),a.x,a.y,a.w,a.h,b.str.c_str(),b.x,b.y,b.w,b.h);
      assert(!overlap);
    }

    const auto button_source=orcsdr::lora::tx_views::packet_rx_control_rect(vertical);
    assert(lora::view()==lora::View::tx_lab);
    assert(orcsdr::lora::tx_views::packet_rx_control_hit(
        button_source.x+button_source.w/2,button_source.y+button_source.h/2,vertical));
    auto button=orcsdr::audio_header::HeaderRect{button_source.x,button_source.y,
                                                  button_source.w,button_source.h};
    if(c.native) button=layout.bounds(button.x,button.y,button.w,button.h);
    assert(button.w>=44&&button.h>=44);
    const auto start=lora::handle_touch(button.x+button.w/2,button.y+button.h/2);
    assert(start.kind==lora::ActionKind::packet_rx_toggle);
    assert(!s.tx.operator_armed&&s.tx.readiness==tx::Readiness::profile_only);

    // Requested but not yet receiving must remain cancellable and redraw only
    // the button rectangle, even when availability has fallen away.
    s.packet_rx_available=false;s.packet_rx_requested=true;
    host_display::reset();lora::update(s);
    assert(has("CANCEL RX")&&full_clears()==0);
    for(const auto& p:host_display::prims) {
      assert(p.x>=button.x&&p.y>=button.y&&p.x+p.w<=button.x+button.w&&p.y+p.h<=button.y+button.h);
    }
    assert(lora::handle_touch(button.x+button.w/2,button.y+button.h/2).kind==
           lora::ActionKind::packet_rx_toggle);

    s.packet_rx_receiving=true;
    host_display::reset();lora::update(s);
    assert(has("STOP RX")&&full_clears()==0);
    assert(lora::handle_touch(button.x+button.w/2,button.y+button.h/2).kind==
           lora::ActionKind::packet_rx_toggle);

    s.packet_rx_requested=false;s.packet_rx_receiving=false;
    host_display::reset();lora::update(s);
    assert(has("RX UNAVAILABLE")&&full_clears()==0);
    assert(lora::handle_touch(button.x+button.w/2,button.y+button.h/2).kind==
           lora::ActionKind::none);

    const auto frequency_source=orcsdr::lora::tx_views::packet_rx_frequency_rect(vertical);
    auto frequency=orcsdr::audio_header::HeaderRect{frequency_source.x,frequency_source.y,
                                                     frequency_source.w,frequency_source.h};
    if(c.native) frequency=layout.bounds(frequency.x,frequency.y,frequency.w,frequency.h);
    host_display::reset();
    s.packet_rx_frequency_hz=906875000;
    lora::update(s);
    assert(has("906.875 MHz")&&!has("LONGFAST / MMCX1")&&full_clears()==0);
    for(const auto& p:host_display::prims)
      assert(p.x>=frequency.x&&p.y>=frequency.y&&p.x+p.w<=frequency.x+frequency.w&&
             p.y+p.h<=frequency.y+frequency.h);
  }
  std::puts("INTEGRATED_PACKET_RX_CONTROLS PASS");
}
int main() {
  rx_only_regression();
  landscape_navigation_and_revocation();
  admitted_controls_still_only_return_actions();
  portrait_presence_and_ui();
  native_safe_views();
  message_only_redraws();
  packet_rx_control_states();
  std::puts("LoRa TX full dashboard host simulation PASS");
  return 0;
}
