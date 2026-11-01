#include "M5Unified.h"
#include "freq_keypad.hpp"
#include "focus_nav.hpp"
#include "dashboard_header_layout.hpp"
#include "dashboard_audio_control.hpp"
#include "home_layout.hpp"
#include <cstring>
RecordingM5 M5;

extern "C" __declspec(dllexport) int check_keypad(int width,int height,int safe) {
  using namespace orcsdr;
  M5.Display=RecordingDisplay{};M5.Display.w=width;M5.Display.h=height;
  focus_nav::clear();char entry[24]="";
  freq_keypad::Rect viewport{72,72,width-144,height-144};
  freq_keypad::draw(0,0,"ENTER FREQUENCY","Frequency","MHz",entry,safe?&viewport:nullptr);
  if (!freq_keypad::self_check()) return 1;
  const auto& l=freq_keypad::layout();
  if (focus_nav::count()!=14) return 2;
  for (const auto& key:l.keys) if (key.w<44 || key.h<44 || !freq_keypad::inside(l.viewport,key)) return 3;
  if (!freq_keypad::inside(l.viewport,l.cancel) || !freq_keypad::inside(l.viewport,l.tune)) return 4;
  const size_t before=M5.Display.fills.size();const int clears=M5.Display.full_clears;
  for (char key: {'1','2','.'}) {
    int x=-1,y=-1;
    if (!freq_keypad::key_point(key,&x,&y)) return 5;
    if (freq_keypad::handle_touch(x,y,entry,sizeof(entry))!=freq_keypad::Result::changed) return 6;
  }
  if (std::strcmp(entry,"12.") || M5.Display.last_text!="12. MHz") return 7;
  if (M5.Display.full_clears!=clears || M5.Display.fills.size()!=before+3) return 8;
  for (size_t i=before;i<M5.Display.fills.size();++i) {
    auto r=M5.Display.fills[i];
    if (r.x!=l.field.x || r.y!=l.field.y || r.w!=l.field.w || r.h!=l.field.h) return 9;
  }
  int x,y;freq_keypad::cancel_point(&x,&y);
  if (freq_keypad::handle_touch(x,y,entry,sizeof(entry))!=freq_keypad::Result::cancelled) return 10;
  freq_keypad::tune_point(&x,&y);
  if (freq_keypad::handle_touch(x,y,entry,sizeof(entry))!=freq_keypad::Result::submitted) return 11;
  if (freq_keypad::handle_touch(-1,-1,entry,sizeof(entry))!=freq_keypad::Result::none) return 12;
  // Entry belongs to the screen state and must survive an orientation/layout change.
  M5.Display.w=height;M5.Display.h=width;focus_nav::clear();
  freq_keypad::draw(0,0,"ENTER FREQUENCY","Frequency","MHz",entry);
  if (!freq_keypad::self_check() || std::strcmp(entry,"12.")) return 13;
  return 0;
}
extern "C" __declspec(dllexport) int check_tab5_legacy() {
  M5.Display=RecordingDisplay{};orcsdr::focus_nav::clear();
  orcsdr::freq_keypad::draw(128,0,"ENTER FREQUENCY","Frequency","MHz","");
  const auto& l=orcsdr::freq_keypad::layout();
  if (l.card.x!=220 || l.card.y!=140 || l.card.w!=840 || l.card.h!=572) return 1;
  int x,y;orcsdr::freq_keypad::key_point('1',&x,&y);
  if (x!=375 || y!=315) return 2;
  orcsdr::freq_keypad::tune_point(&x,&y);
  if (x!=840 || y!=666) return 3;
  return 0;
}

extern "C" __declspec(dllexport) int check_focus_viewport() {
  using namespace orcsdr::focus_nav;
  if (!set_viewport(640,1000)) return 1;
  clear();note(280,440,80,60);note(280,330,80,60);
  if (!move(Direction::right)) return 2;
  Rect r;
  if (!focused(&r) || r.y!=440) return 3;
  note(20,850,80,60); // portrait controls below the old hardcoded720 boundary
  if (count()!=3) return 4;
  if (!set_viewport(640,1000) || count()!=3) return 5;
  if (set_viewport(0,1000) || count()!=3) return 6;
  if (!set_viewport(1000,640) || count()!=0 || focused(&r)) return 7;
  if (!self_check()) return 8; // preserves the caller viewport after its own fixture
  return 0;
}

extern "C" __declspec(dllexport) int check_header_layouts() {
  using namespace orcsdr::audio_header;
  const HeaderRect views[]={{0,0,1280,720},{72,72,424,1088},{72,72,1088,424},
                            {0,0,800,480},{0,0,720,1280},{0,0,360,640}};
  for (auto v:views) {
    const auto l=make_header_layout(v);
    if (!l.valid) return 1;
    const HeaderRect buttons[]={l.home,l.mute,l.settings};
    for (int i=0;i<3;++i) {
      if (!header_inside(v,buttons[i]) || buttons[i].w<44 || buttons[i].h<44) return 2;
      for (int j=i+1;j<3;++j) if (header_overlap(buttons[i],buttons[j])) return 3;
    }
    if (!header_inside(v,l.tray) || l.slider_x<l.tray.x || l.slider_x+l.slider_w>l.tray.x+l.tray.w) return 4;
    if (l.content_top>=v.y+v.h || l.tray.y<l.mute.y+l.mute.h) return 5;
    if (!l.legacy && (header_overlap(l.brand,l.home) || header_overlap(l.battery,l.status))) return 6;
  }
  const auto legacy=make_header_layout({0,0,1280,720});
  if (!legacy.legacy || legacy.home.x!=1040 || legacy.home.y!=8 || legacy.mute.x!=1099 ||
      legacy.status.x!=420 || legacy.status.w!=446 || legacy.tray.x!=900 || legacy.slider_y!=113) return 7;
  if (make_header_layout({0,0,200,640}).valid || make_header_layout({0,0,800,100}).valid) return 8;
  return 0;
}

extern "C" __declspec(dllexport) int check_header_renderer(int width,int height,int safe) {
  using namespace orcsdr::audio_header;
  M5.Display=RecordingDisplay{};M5.Display.w=width;M5.Display.h=height;
  orcsdr::focus_nav::clear();
  HeaderRect v=safe?HeaderRect{72,72,width-144,height-144}:HeaderRect{0,0,width,height};
  if (!set_viewport(v)) return 1;
  draw_brand("HOME");draw_battery(50);draw_home_button();draw_visualizer_button(true);
  draw_settings_button();draw_mute_button(true);
  if (orcsdr::focus_nav::count()!=3 || !self_check()) return 2;
  const auto& l=layout();
  if (!home_hit(l.home.x+l.home.w/2,l.home.y+l.home.h/2) ||
      !mute_hit(l.mute.x+l.mute.w/2,l.mute.y+l.mute.h/2) ||
      !settings_hit(l.settings.x+l.settings.w/2,l.settings.y+l.settings.h/2)) return 3;
  for (auto r:M5.Display.fills) if (!header_inside(v,{r.x,r.y,r.w,r.h})) return 4;
  Control control{};reset(control);
  if (handle_touch(control,l.mute.x+5,l.mute.y+5,100,128)!=Action::opened) return 5;
  capture_volume_background();
  const auto before=M5.Display.fills.size();const int clears=M5.Display.full_clears;
  for (unsigned value: {64,128,192}) draw(control,uint8_t(value),true,50);
  if (M5.Display.full_clears!=clears || M5.Display.last_text!="75%") return 6;
  for (size_t i=before;i<M5.Display.fills.size();++i) {
    auto r=M5.Display.fills[i];HeaderRect rect{r.x,r.y,r.w,r.h};
    if (!header_inside(l.battery,rect) && !header_inside(l.tray,rect)) return 7;
  }
  if (visualizer_hit(l.battery.x+l.battery.w/2,l.battery.y+20)) return 10;
  const int pushes=M5.Display.pushes;
  // A structural viewport change invalidates cached pixels from the old layout.
  if (!set_viewport(v)) return 8;
  restore_volume_background();
  if (M5.Display.pushes!=pushes) return 9;
  return 0;
}

extern "C" __declspec(dllexport) int check_combined_controls(int width,int height,int safe) {
  namespace h=orcsdr::audio_header;namespace k=orcsdr::freq_keypad;
  M5.Display=RecordingDisplay{};M5.Display.w=width;M5.Display.h=height;orcsdr::focus_nav::clear();
  h::HeaderRect v=safe?h::HeaderRect{72,72,width-144,height-144}:h::HeaderRect{0,0,width,height};
  if (!h::set_viewport(v)) return 1;
  h::draw_brand("HOME");h::draw_battery(75);h::draw_home_button();h::draw_mute_button(true);
  h::draw_visualizer_button(false);h::draw_settings_button();
  const int top=h::layout().content_top;
  k::Rect body{v.x,top,v.w,v.y+v.h-top};
  k::draw(top,0,"ENTER FREQUENCY","Frequency","MHz","96.1",&body);
  if (!k::self_check() || k::layout().card.y<top || orcsdr::focus_nav::count()!=17) return 2;
  for (const auto& key:k::layout().keys) if (key.h<44 || key.w<44) return 3;
  return 0;
}

extern "C" __declspec(dllexport) int check_home_layouts() {
  namespace h=orcsdr::audio_header;
  const h::HeaderRect viewports[]={{0,0,1280,720},{72,72,1088,424},{72,72,424,1088},{0,0,800,480}};
  for (auto viewport:viewports) {
    const auto header=h::make_header_layout(viewport);
    const auto l=orcsdr::home::make_layout(viewport,header.content_top);
    if (!header.valid || !l.valid) return 1;
    if (l.legacy) {
      if (l.spectrum.x!=330 || l.spectrum.w!=906 || l.waterfall.y!=301 || l.rail.x!=24) return 2;
      continue;
    }
    const h::HeaderRect regions[]={l.spectrum,l.waterfall,l.frequency,l.contrast,l.palette,l.speed,l.mode,
        l.span,l.tune,l.step,l.filter,l.gain,l.recent,l.all,l.footer};
    for (unsigned i=0;i<sizeof(regions)/sizeof(regions[0]);++i) {
      if (!h::header_inside(viewport,regions[i])) return 3;
      for (unsigned j=i+1;j<sizeof(regions)/sizeof(regions[0]);++j)
        if (h::header_overlap(regions[i],regions[j])) return 4;
    }
    if (l.spectrum.y<header.content_top || l.spectrum.h<50 || l.waterfall.h<50) return 5;
  }
  if (orcsdr::home::make_layout({0,0,200,200},50).valid) return 6;
  return 0;
}
