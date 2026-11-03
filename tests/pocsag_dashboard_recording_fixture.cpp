#define _CRT_SECURE_NO_WARNINGS
#include <M5Unified.h>
#include "../apps/orcsdr-tab5/ui/pocsag_dashboard.cpp"
#include <cassert>

namespace orcsdr::audio_header {
bool set_viewport(HeaderRect) {return true;}
void draw_brand(const char*) {}
void draw_home_button() {}
void draw_battery(int32_t) {}
void draw_mute_button(bool) {}
void draw_visualizer_button(bool) {}
void draw_settings_button() {}
bool home_hit(int32_t,int32_t) {return false;}
bool settings_hit(int32_t,int32_t) {return false;}
bool self_check() {return true;}
}

int main() {
  using namespace orcsdr::pocsag;
  const auto tap=[](layout::Rect r){return handle_touch(r.x+r.w/2,r.y+r.h/2);};
  for(auto viewport:{layout::Rect{0,0,800,480},layout::Rect{72,72,424,1088},layout::Rect{72,72,1088,424}}) {
    Snapshot snapshot{};snapshot.receiving=true;snapshot.frequency_hz=152007500;
    snapshot.message_count=kRecentMessageCapacity;snapshot.identity_count=kIdentityCapacity;
    snapshot.decoder_stats.detected_baud=1200;
    for(size_t i=0;i<kRecentMessageCapacity;++i) {
      auto& m=snapshot.messages[i];m.capcode=1234500+uint32_t(i);m.timestamp_ms=100+i;
      m.baud=1200;m.text_length=95;m.type=MessageType::alpha;
      snprintf(m.text,sizeof(m.text),"Record %u full message has a retained final token END%u",unsigned(i),unsigned(i));
      auto& id=snapshot.identities[i];id.capcode=m.capcode;id.hit_count=uint32_t(i+1);
      snprintf(id.alias,sizeof(id.alias),"Pager %u",unsigned(i));
    }
    assert(orcsdr::pocsag::set_viewport(viewport));set_live_snapshot(snapshot);enter(Settings{});
    snapshot.message_count=0;snapshot.receiving=false;set_live_snapshot(snapshot);native_live();lgfx::paints.clear();
    snapshot.receiving=true;set_live_snapshot(snapshot);native_live();
    bool receiving=false;for(const auto& p:lgfx::paints) receiving|=p.text.find("RECEIVER ACTIVE")!=std::string::npos;
    assert(receiving);
    snapshot.message_count=kRecentMessageCapacity;set_live_snapshot(snapshot);
    bool applied=false;
    assert(control(Control::view,99,&applied)==Action::none && !applied);
    control(Control::view,4,&applied);assert(applied && view()==4);
    control(Control::select,11,&applied);assert(applied && g_selected_message==11);
    control(Control::select,12,&applied);assert(!applied);
    control(Control::view,2,&applied);assert(applied && view()==2);
    assert(control(Control::baud,2400,&applied)==Action::settings_changed && applied && settings().baud_bps==2400);
    assert(control(Control::polarity,2,&applied)==Action::settings_changed && applied && settings().polarity_mode==2);
    control(Control::baud,999,&applied);assert(!applied);
    for(int tab=0;tab<5;++tab) {tap(g_layout.tab[tab]);assert(view()==tab);}
    for(int tab:{1,4}) {
      tap(g_layout.tab[tab]);const auto slot=tab==1?0:1;
      const size_t pages=(12+size_t(g_layout.list_row_count)-1)/size_t(g_layout.list_row_count);
      for(size_t i=1;i<pages;++i) tap(g_layout.list_next);
      const size_t last_row=11-g_list_page[slot]*size_t(g_layout.list_row_count);
      tap(g_layout.list_rows[last_row]);
      assert((tab==1?g_selected_id:g_selected_message)==11);
      std::string details;
      for(int i=0;i<12;++i) {
        lgfx::paints.clear();g_widget_valid[12]=false;native_list(tab==1);
        for(const auto& p:lgfx::paints) if(p.y>=g_layout.detail.y && p.y<g_layout.detail.y+g_layout.detail.h) details+=p.text;
        const size_t before=g_detail_page[slot];tap(g_layout.detail_next);
        if(before==g_detail_page[slot]) break;
      }
      assert(details.find("END11")!=std::string::npos);
      const auto target=viewport.w==424?layout::Rect{72,72,1088,424}:layout::Rect{72,72,424,1088};
      assert(orcsdr::pocsag::set_viewport(target));draw();assert(view()==tab);
      assert((tab==1?g_selected_id:g_selected_message)==11);
      assert(orcsdr::pocsag::set_viewport(viewport));draw();
    }
    tap(g_layout.tab[2]);const auto baud=settings().baud_bps;
    assert(tap(g_layout.baud)==Action::settings_changed && settings().baud_bps!=baud);
    assert(tap(g_layout.polarity)==Action::settings_changed);
    bool baud_focus=false,polarity_focus=false;
    for(size_t i=0;i<orcsdr::focus_nav::count();++i) {
      orcsdr::focus_nav::Rect r{};assert(orcsdr::focus_nav::get(i,&r));
      baud_focus|=r.x==g_layout.baud.x && r.y==g_layout.baud.y && r.w==g_layout.baud.w && r.h==g_layout.baud.h;
      polarity_focus|=r.x==g_layout.polarity.x && r.y==g_layout.polarity.y && r.w==g_layout.polarity.w && r.h==g_layout.polarity.h;
    }
    assert(baud_focus && polarity_focus);
    // Ordinary samples update widgets, preserve static chrome and never clear the content viewport.
    orcsdr::focus_nav::focus_at(g_layout.baud.x+8,g_layout.baud.y+8);
    const int clears=lgfx::screen_clears;lgfx::filled_rects.clear();lgfx::paints.clear();
    snapshot.decoder_stats.codewords_total=123;snapshot.decoder_stats.codewords_valid=122;
    snapshot.decoder_stats.codewords_corrected=123456789;snapshot.decoder_stats.corrected_bit_count=1234567890;
    snapshot.revision=1;set_live_snapshot(snapshot);test_millis+=1100;update();
    assert(lgfx::screen_clears==clears);
    bool changed=false,bits=false;for(const auto& p:lgfx::paints) {changed|=p.text=="123";bits|=p.text=="123456789/1234567890";}
    assert(changed && bits);
    orcsdr::focus_nav::Rect focused{};assert(orcsdr::focus_nav::focused(&focused));
    assert(focused.x==g_layout.baud.x && focused.y==g_layout.baud.y);
    assert(!lgfx::filled_rects.empty());
    for(const auto& f:lgfx::filled_rects) {
      assert(f.x>=viewport.x && f.y>=viewport.y && f.x+f.w<=viewport.x+viewport.w && f.y+f.h<=viewport.y+viewport.h);
      assert(!(f.x==g_layout.content.x && f.y==g_layout.content.y && f.w==g_layout.content.w && f.h==g_layout.content.h));
    }
    lgfx::paints.clear();snapshot.revision++;set_live_snapshot(snapshot);test_millis+=1100;update();
    assert(lgfx::paints.empty());
    leave();
  }
  assert(orcsdr::pocsag::set_viewport({0,0,1280,720}));assert(g_layout.legacy && self_check());
  enter(Settings{});bool applied=false;
  for(unsigned i=0;i<5;++i) {control(Control::view,i,&applied);assert(applied && view()==i);}
  control(Control::view,2,&applied);
  control(Control::baud,512,&applied);assert(applied && settings().baud_bps==512);
  control(Control::polarity,1,&applied);assert(applied && settings().polarity_mode==1);
  leave();
  static_assert(sizeof(g_layout)+sizeof(g_widget_signature)+sizeof(g_widget_valid)<=2048,"POCSAG layout/cache budget exceeded");
  puts("POCSAG production renderer/input tests PASS");
}
