#include "map_screen.hpp"
#include "map_service.hpp"
#include "map_downloads.hpp"
#include "map_preferences.hpp"
#include "receiver_location.hpp"
#include "setup_flow.hpp"
#include "setup_ui.hpp"
#include "screen_controller.hpp"
#include "focus_nav.hpp"
#include "navigation_service.hpp"
#include "settings_app.hpp"
#include "orcmap/location_picker.hpp"
#include <M5Unified.h>
#include <esp_attr.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace orcsdr::map_screen {
namespace {
// Match Settings: charcoal panels, cyan controls and green primary actions.
constexpr uint16_t kBg=0x0841,kPanel=0x1082,kInk=0xc618,kAccent=0x04ff,kGreen=0x6fe8;
constexpr int kMapX=16,kMapY=72,kMapW=1248,kMapH=568;
constexpr int kLatX=250,kLonX=612,kCoordinateY=10,kCoordinateW=350;
constexpr int kZoomX=1188,kZoomInY=84,kZoomOutY=148,kZoomW=64,kZoomH=52;
constexpr int kPanX=28,kPanY=84,kPanStepX=kMapW/2,kPanStepY=kMapH/2;
constexpr int kUseX=920,kFooterY=650,kUseW=344;
Hooks g_hooks;
NvsStore* g_preferences=nullptr;
setup::Flow g_saved,g_flow;
Mode g_mode=Mode::inventory;
bool g_auto=false,g_dirty=true,g_drag=false,g_has_proposal=false;
bool g_lat_set=false,g_lon_set=false,g_return_settings=false;
bool g_picker_ready=false;
bool g_setup_paused=false;
screens::Id g_return=screens::Id::home;
settings::Section g_section=settings::Section::data_maps;
int g_last_x=0,g_last_y=0,g_edit=0,g_index=0,g_scroll=0;
bool g_location_menu=true,g_details=false;
bool g_download_view=false;
int g_download_index=0,g_download_scroll=0;
bool g_map_touch=false,g_map_moved=false;
bool g_map_visible=false;
bool g_layout_valid=false;
uint32_t g_layout=0;
EXT_RAM_BSS_ATTR maps::downloads::Snapshot g_downloads;
int32_t g_lat=0,g_lon=0;
char g_entry[16]{},g_message[96]{};
orcmap::LocationPicker g_picker;
EXT_RAM_BSS_ATTR maps::MapSnapshot g_packs;
uint32_t g_last_map_revision=0,g_last_tick=0;

bool hit(int x,int y,int bx,int by,int w,int h) {return x>=bx&&x<bx+w&&y>=by&&y<by+h;}
void text(const char* s,int x,int y,int size=2,uint16_t color=kInk) {
  M5.Display.setTextDatum(top_left);M5.Display.setTextSize(size);M5.Display.setTextColor(color,kBg);
  M5.Display.drawString(s,x,y);
}
void button(const char* label,int x,int y,int w=180,int h=46,bool primary=false) {
  const uint16_t accent=primary?kGreen:kAccent;
  focus_nav::note(x,y,w,h);M5.Display.fillRoundRect(x,y,w,h,6,kPanel);
  M5.Display.drawRoundRect(x,y,w,h,6,accent);
  M5.Display.setTextDatum(top_left);M5.Display.setTextSize(3);M5.Display.setTextColor(accent,kPanel);
  M5.Display.drawString(label,x+12,y+14);
}
void wrapped(const char* value,int x,int y,int columns=100,int size=1) {
  const size_t length=std::strlen(value);
  for(size_t offset=0;offset<length;offset+=columns) {
    char part[193]{};std::memcpy(part,value+offset,std::min(static_cast<size_t>(columns),length-offset));
    text(part,x,y,size);y+=8*size+8;
  }
}
bool checkpoint() {
  // A rerun stays in RAM until Finish; Cancel retains the old completion record.
  if(!g_auto)return true;
  if(g_flow.writable&&g_preferences&&g_preferences->putUInt("maps_setup",g_flow.record()))return true;
  std::snprintf(g_message,sizeof(g_message),"Could not save setup progress. Existing settings retained.");return false;
}
bool location_step() {return g_mode==Mode::picker||(g_mode==Mode::setup&&g_flow.step==setup::Step::location);}
bool maps_step() {return g_mode==Mode::inventory||(g_mode==Mode::setup&&g_flow.step==setup::Step::maps);}
bool advance() {
  const auto previous=g_flow;g_flow.next();
  const bool ok=checkpoint();if(!ok)g_flow=previous;
  if(ok)g_download_view=false;
  g_dirty=true;return ok;
}
void picker_at(int32_t latitude,int32_t longitude) {
  g_picker_ready=maps::begin_location_picker(g_picker,kMapW,kMapH,latitude,longitude);
}
void moved_picker() {
  const auto point=g_picker.Selection();g_lat=static_cast<int32_t>(std::llround(point.lat_deg*1e7));
  g_lon=static_cast<int32_t>(std::llround(point.lon_deg*1e7));
  g_lat_set=g_lon_set=g_has_proposal=true;g_dirty=true;
}
void pan(int dx,int dy) {
  if(g_picker_ready&&g_picker.DragByGesture(dx,dy))moved_picker();
}
void begin_edit(int field) {
  g_edit=field;std::snprintf(g_entry,sizeof(g_entry),"%ld",static_cast<long>(field==1?g_lat:g_lon));g_dirty=true;
}
void finish_edit() {
  int32_t value=0;
  if(!setup::parse_e7(g_entry,g_edit==1,&value)) {
    std::snprintf(g_message,sizeof(g_message),"Invalid E7 coordinate (degrees x 10000000).");g_dirty=true;return;
  }
  if(g_edit==1) {g_lat=static_cast<int32_t>(value);g_lat_set=true;}
  else {g_lon=static_cast<int32_t>(value);g_lon_set=true;}
  g_edit=0;g_has_proposal=g_lat_set&&g_lon_set;picker_at(g_lat,g_lon);g_dirty=true;g_message[0]=0;
}
void draw_map() {
  g_last_map_revision=maps::revision();
  if(!g_picker_ready) {
    M5.Display.fillRect(kMapX,kMapY,kMapW,kMapH,kBg);
    text("Map camera unavailable. Enter a valid coordinate or cancel.",kMapX+12,kMapY+12,1);
    return;
  }
  if(std::abs(static_cast<int64_t>(g_lat))>850511287) {
    M5.Display.fillRect(kMapX,kMapY,kMapW,kMapH,kBg);
    text("Polar latitude: no Mercator basemap. The entered E7 coordinate is retained.",kMapX+12,kMapY+12,1);
    return;
  }
  const auto location=receiver_location::snapshot();maps::FrameInfo info;
  const auto state=maps::draw(g_picker.View(),M5.Display,kMapX,kMapY,&info,location.map_pack,maps::preferences::labels(maps::preferences::Consumer::picker));
  if(state==maps::Frame::pending&&g_map_visible) {
    M5.Display.fillRect(450,604,380,32,kBg);
    text("Updating map...",460,608,2);
  } else if(state!=maps::Frame::ready) {
    g_map_visible=false;
    M5.Display.fillRect(kMapX,kMapY,kMapW,kMapH,kBg);
    M5.Display.drawRect(kMapX,kMapY,kMapW,kMapH,0x2945);
    text(state==maps::Frame::pending?"Loading offline map...":"No basemap installed. Pan or enter coordinates to choose a location.",kMapX+12,kMapY+12,1);
  } else {g_map_visible=true;wrapped(info.credit,kMapX+12,kMapY+kMapH-44,100,2);}
  M5.Display.drawLine(kMapX+kMapW/2-14,kMapY+kMapH/2,kMapX+kMapW/2+14,kMapY+kMapH/2,kAccent);
  M5.Display.drawLine(kMapX+kMapW/2,kMapY+kMapH/2-14,kMapX+kMapW/2,kMapY+kMapH/2+14,kAccent);
  // Redraw overlays whenever the asynchronous map replaces its pixels.
  button(maps::preferences::labels(maps::preferences::Consumer::picker)?"LABELS ON":"LABELS OFF",940,84,232,52);
  button("+",kZoomX,kZoomInY,kZoomW,kZoomH);
  button("-",kZoomX,kZoomOutY,kZoomW,kZoomH);
  button("^",kPanX+72,kPanY,kZoomW,kZoomH);
  button("<",kPanX,kPanY+64,kZoomW,kZoomH);
  button(">",kPanX+144,kPanY+64,kZoomW,kZoomH);
  button("v",kPanX+72,kPanY+128,kZoomW,kZoomH);
}
void draw_picker() {
  char value[64];std::snprintf(value,sizeof(value),"LAT E7 %ld",static_cast<long>(g_lat));button(value,kLatX,kCoordinateY,kCoordinateW);
  std::snprintf(value,sizeof(value),"LON E7 %ld",static_cast<long>(g_lon));button(value,kLonX,kCoordinateY,kCoordinateW);
  button("USE LOCATION",kUseX,kFooterY,kUseW,60,true);
  const int hint_x=g_mode==Mode::setup?220:16;
  text(g_has_proposal?"Center = location. USE LOCATION saves.":"Choose a point, then USE LOCATION.",hint_x,652,2);
  text("Pan: arrows/drag/tap. Zoom: +/-.",hint_x,676,2);
  draw_map();
}
void draw_inventory() {
  (void)maps::snapshot(&g_packs);
  text("Choose an offline map",32,92,3);
  text(g_packs.sd?"Maps on this device and SD card":"No SD card - built-in maps only",32,130,2);
  if(!g_packs.loaded)text("Scanning maps...",32,170,3);
  if(g_packs.overflow||g_packs.out_of_memory)text("Some maps could not be listed. Rescan to retry.",32,580,2,TFT_ORANGE);
  g_index=std::clamp(g_index,0,std::max(0,static_cast<int>(g_packs.count)-1));
  g_scroll=std::clamp(g_scroll,0,std::max(0,static_cast<int>(g_packs.count)-4));
  const auto location=receiver_location::snapshot();
  for(int row=0;row<4&&g_scroll+row<g_packs.count;++row) {
    const int index=g_scroll+row,y=174+row*84;const auto& pack=g_packs.packs[index];
    char title[58];std::snprintf(title,sizeof(title),"%.57s",pack.title);
    button(title,32,y,1070,76,index==g_index);
    char line[160];
    const char* detail=pack.kind==maps::Kind::firmware?"World overview - broad coverage, limited detail":
        pack.kind==maps::Kind::legacy?"Older format - install a PMTiles map":pack.region;
    std::snprintf(line,sizeof(line),"%.55s | zoom %u-%u%s%s",detail,pack.min_zoom,pack.max_zoom,
        std::strcmp(location.map_pack,pack.identity)==0?" | SELECTED":"",pack.usable?"":" | UNAVAILABLE");
    text(line,44,y+46,2);
  }
  button("UP",1120,174,128,70);button("DOWN",1120,426,128,70);
  button("RESCAN",32,520,200,56);button("DETAILS",250,520,200,56);
  button("GET MAPS",470,520,310,56,true);
  if(g_packs.count&&g_packs.packs[g_index].usable)button("USE THIS MAP",800,520,300,56,true);
  if(g_details&&g_packs.count) {
    const auto& pack=g_packs.packs[g_index];
    M5.Display.fillRect(24,160,1080,340,kBg);
    text(pack.title,32,170,3);wrapped(pack.region,32,212,80,2);
    wrapped(pack.status,32,256,80,2);wrapped(pack.credit,32,300,80,2);
    wrapped(pack.kind==maps::Kind::legacy?"Legacy ORCMAP1 format (unsupported)":pack.identity,32,364,80,2);
    text("Metadata/size checked; archive hash not verified.",32,454,2);
  }
  if(g_downloads.message[0]) {
    char message[64];std::snprintf(message,sizeof(message),"%.63s",g_downloads.message);
    text(message,32,582,3,g_downloads.phase==maps::downloads::Phase::failed?TFT_ORANGE:kInk);
  } else if(!g_downloads.busy&&!g_packs.overflow&&!g_packs.out_of_memory) {
    text("Manual SD: copy matching .pmtiles + .manifest.json to /orcmaps/.",32,582,2);
    text("Files + checksum: github.com/hardcoreerik/OrcMaps/releases",32,606,2);
  }
}
bool download_busy() {return g_downloads.busy;}
void download_size(uint64_t bytes,char* value,size_t capacity) {
  if(bytes>=1024ULL*1024ULL*1024ULL)
    std::snprintf(value,capacity,"%.1f GB",static_cast<double>(bytes)/(1024.0*1024.0*1024.0));
  else std::snprintf(value,capacity,"%.1f MB",static_cast<double>(bytes)/(1024.0*1024.0));
}
void draw_download_progress();
void draw_downloads() {
  (void)maps::downloads::snapshot(&g_downloads);
  text("Get offline maps",32,92,3);
  const char* hint=g_downloads.busy?"Back to the device: downloads continue. Stop Transfer pauses the download.":
      !g_downloads.configured?"Map source is not configured. Installed maps remain available.":
      !g_downloads.connected?"Offline. Return to Network to connect; saved transfers can still be resumed.":
      !g_downloads.sd_available?"Insert an SD card for downloads. The firmware world map remains available.":
      !g_downloads.catalog_ready?"Check the catalog to see maps available from your configured source.":
      "Select a map. Downloads can be resumed; use Installed to choose your map.";
  text(hint,32,132,2);
  const int count=static_cast<int>(g_downloads.count);
  g_download_index=std::clamp(g_download_index,0,std::max(0,count-1));
  g_download_scroll=std::clamp(g_download_scroll,0,std::max(0,count-4));
  for(int row=0;row<4&&g_download_scroll+row<count;++row) {
    const int index=g_download_scroll+row,y=174+row*84;
    maps::downloads::Entry pack;if(!maps::downloads::entry(index,&pack))continue;
    char title[64],size[24],detail[128];
    std::snprintf(title,sizeof(title),"%.43s%s",pack.title,pack.installed?" [INSTALLED]":pack.recommended?" [RECOMMENDED]":"");
    button(title,32,y,1070,76,index==g_download_index);
    download_size(pack.bytes,size,sizeof(size));
    std::snprintf(detail,sizeof(detail),"%.28s | %.22s | %s | zoom %u-%u",pack.region,pack.product,size,pack.min_zoom,pack.max_zoom);
    text(detail,44,y+46,2);
  }
  if(g_downloads.phase==maps::downloads::Phase::checking)text("Checking catalog...",44,190,3);
  else if(!count)text("No catalog maps to show yet.",44,190,3);
  button("UP",1120,174,128,70);button("DOWN",1120,426,128,70);
  button("INSTALLED",32,520,270,56);
  if(maps::downloads::can_check(g_downloads))
    button("CHECK CATALOG",320,520,310,56);
  else if(maps::downloads::can_reset(g_downloads))
    button("RESET STUCK",320,520,310,56);
  if(download_busy())button("STOP TRANSFER",930,520,320,56);
  else if(maps::downloads::can_install(g_downloads,g_download_index)) {
    maps::downloads::Entry pack;
    if(maps::downloads::entry(g_download_index,&pack)&&!pack.installed)
      button("DOWNLOAD/RESUME",650,520,370,56,true);
  }
  if(maps::downloads::can_retry(g_downloads))
    button("RETRY LAST",1038,520,210,56);
  draw_download_progress();
}
void draw_download_progress() {
  M5.Display.fillRect(32,580,1216,44,kBg);
  const bool verifying=g_downloads.phase==maps::downloads::Phase::verifying;
  char status[120]{};
  if(verifying&&g_downloads.verify_total_bytes) {
    char checked[24],total[24];
    download_size(g_downloads.verified_bytes,checked,sizeof(checked));
    download_size(g_downloads.verify_total_bytes,total,sizeof(total));
    const auto percent=std::min(g_downloads.verified_bytes,g_downloads.verify_total_bytes)*100/g_downloads.verify_total_bytes;
    std::snprintf(status,sizeof(status),"Checking map file: %s / %s (%u%%)",checked,total,static_cast<unsigned>(percent));
  } else if(verifying)std::snprintf(status,sizeof(status),"Checking map file and installing...");
  else if(g_downloads.phase==maps::downloads::Phase::downloading) {
    char received[24],total[24];download_size(g_downloads.completed_bytes,received,sizeof(received));
    download_size(g_downloads.total_bytes,total,sizeof(total));
    std::snprintf(status,sizeof(status),"Downloading: %s / %s",received,total);
  } else if(g_downloads.phase==maps::downloads::Phase::complete)
    std::snprintf(status,sizeof(status),"Installed. Open INSTALLED, select the map, then USE THIS MAP.");
  else std::snprintf(status,sizeof(status),"%.95s",g_downloads.message);
  text(status,32,584,2,g_downloads.phase==maps::downloads::Phase::failed?TFT_ORANGE:kInk);
  if(verifying||g_downloads.phase==maps::downloads::Phase::downloading) {
    M5.Display.drawRect(32,612,1216,10,kAccent);
    if(verifying&&!g_downloads.verify_total_bytes)M5.Display.fillRect(34+static_cast<int>((millis()/40)%1010),614,200,6,kAccent);
    else if(verifying?g_downloads.verify_total_bytes:g_downloads.total_bytes) {
      const auto used=verifying?g_downloads.verified_bytes:g_downloads.completed_bytes;
      const auto total=verifying?g_downloads.verify_total_bytes:g_downloads.total_bytes;
      const double fraction=std::min(1.0,static_cast<double>(used)/total);
      M5.Display.fillRect(34,614,static_cast<int>(1212*fraction),6,kGreen);
    }
  }
}
void draw_edit() {
  M5.Display.fillRect(0,90,1280,540,kBg);
  text(g_edit==1?"LATITUDE E7 (-900000000 to 900000000)":"LONGITUDE E7 (-1800000000 to 1800000000)",40,110);
  text(g_entry,40,160,3,kAccent);
  constexpr char keys[]="123456789-0<";
  for(int i=0;i<12;++i) {char label[2]{keys[i],0};button(label,400+(i%3)*120,220+(i/3)*80,100,60);}
  button("SAVE FIELD",820,300,250);button("CANCEL FIELD",820,400,250);
}
void close_screen() {
  if(g_mode==Mode::setup)setup_ui::reset();
  g_edit=0;maps::cancel();screens::begin_transition(g_return,millis());screens::finish_transition();
  if(g_return_settings)navigation::open_settings(g_section);
  else if(g_hooks.restore)g_hooks.restore(g_return);
  if(g_setup_paused) {g_setup_paused=false;if(g_hooks.setup_reception)g_hooks.setup_reception(false);}
}
bool finish() {
  const auto previous=g_flow;g_flow.done=true;
  if(!g_flow.writable||!g_preferences||!g_preferences->putUInt("maps_setup",g_flow.record())) {
    g_flow=previous;std::snprintf(g_message,sizeof(g_message),"Setup record could not be saved (unknown schema or storage error).");g_dirty=true;return false;
  }
  g_saved=g_flow;g_saved.automatic=false;g_auto=false;
  close_screen();
  return true;
}
void cancel() {setup_ui::reset();close_screen();}
} // namespace
void configure(const Hooks& hooks) {g_hooks=hooks;}
void load(NvsStore& preferences) {
  g_preferences=&preferences;
  const bool legacy=preferences.isKey("journal")||preferences.isKey("workflow")||
      preferences.isKey("loc_point")||
      preferences.isKey("adsb_loc_set")||preferences.isKey("adsb_lat_e7")||
      preferences.isKey("map_pack")||preferences.isKey("wifi0_ssid")||
      preferences.isKey("wifi_ssid")||preferences.isKey("set_bright")||preferences.isKey("pair_key");
  g_saved=setup::Flow::restore(preferences.isKey("maps_setup"),preferences.getUInt("maps_setup",0),legacy);
  if(g_saved.automatic) {
    // Mark the blank install before legacy boot defaults are written.
    g_auto=true;g_flow=g_saved;(void)checkpoint();g_auto=false;
  }
}
bool first_run_pending() {return g_saved.automatic;}
bool open(Mode mode,bool automatic) {
  const bool download_view=mode==Mode::downloads;
  if(download_view)mode=Mode::inventory;
  if(mode==Mode::setup&&!g_setup_paused&&g_hooks.setup_reception) {
    if(!g_hooks.setup_reception(true))return false;
    g_setup_paused=true;
  } else if(mode!=Mode::setup&&g_setup_paused) {
    g_setup_paused=false;if(g_hooks.setup_reception)g_hooks.setup_reception(false);
  }
  g_map_visible=false;
  const bool nested=screens::is_active(screens::Id::maps)||
      (settings::active()&&screens::status().return_to==screens::Id::maps);
  if(!nested) {g_return_settings=settings::active();g_section=settings::section();}
  // Close the original Settings session first, restoring its graphics state.
  // Optional Network opens a new session that can safely return to this screen.
  if(settings::active())navigation::close_settings();
  if(!nested)g_return=screens::status().active;
  screens::begin_transition(screens::Id::maps,millis());
  g_mode=mode;g_auto=automatic;g_flow=automatic?g_saved:setup::Flow{};
  g_flow.writable=g_saved.writable;g_saved.automatic=false;
  setup_ui::reset();g_edit=0;g_message[0]=0;g_dirty=true;g_index=0;g_scroll=0;g_drag=false;g_map_touch=false;g_map_moved=false;g_location_menu=true;g_details=false;
  g_download_view=download_view;g_download_index=0;g_download_scroll=0;g_layout_valid=false;
  const auto location=receiver_location::snapshot();g_lat=location.latitude_e7;g_lon=location.longitude_e7;
  g_lat_set=g_lon_set=g_has_proposal=location.configured;
  picker_at(g_lat,g_lon);maps::cancel();draw();screens::finish_transition();
  return true;
}
State state() {
  return {screens::owns(screens::Id::maps),g_has_proposal,g_edit!=0,g_mode,
      static_cast<uint8_t>(g_flow.step),g_picker_ready?g_picker.Zoom():uint8_t{0},g_lat,g_lon,g_download_view};
}
void refresh() {if(screens::is_active(screens::Id::maps)){g_dirty=true;}}
bool control(Control action,int32_t first,int32_t second,const char* identity) {
  if(!screens::owns(screens::Id::maps))return false;
  if(action==Control::cancel){cancel();return true;}
  if(action==Control::back&&g_mode==Mode::setup&&setup_ui::back()){g_dirty=true;return true;}
  if(g_edit||setup_ui::input_active())return false;
  if(action==Control::next||action==Control::back||action==Control::quick_start||action==Control::finish) {
    if(g_mode!=Mode::setup)return false;
    if(action==Control::finish)return g_flow.step==setup::Step::complete&&finish();
    if(action==Control::next){if(g_flow.step==setup::Step::complete)return false;return advance();}
    const auto previous=g_flow;
    if(action==Control::back) {
      if(location_step()&&!g_location_menu){g_location_menu=true;g_dirty=true;return true;}
      if(g_flow.step==setup::Step::welcome)return false;
      g_flow.back();
    } else {if(g_flow.step!=setup::Step::welcome)return false;g_flow.quick_start();}
    const bool ok=checkpoint();if(!ok)g_flow=previous;g_dirty=true;return ok;
  }
  if(action==Control::select_pack) {
    if(!maps_step()||!identity||!g_hooks.pack||!maps::snapshot(&g_packs))return false;
    for(size_t i=0;i<g_packs.count;++i) {
      const auto& pack=g_packs.packs[i];
      if(pack.usable&&!std::strcmp(pack.identity,identity)&&g_hooks.pack(pack.identity)) {
        if(g_mode==Mode::setup)return advance();
        cancel();return true;
      }
    }
    return false;
  }
  if(!location_step())return false;
  if(action==Control::set_proposal) {
    if(!receiver_location::valid(true,first,second))return false;
    g_lat=first;g_lon=second;g_lat_set=g_lon_set=g_has_proposal=true;
    g_location_menu=false;picker_at(g_lat,g_lon);g_dirty=true;return true;
  }
  if(g_mode==Mode::setup&&g_location_menu)return false;
  if(action==Control::save) {
    if((!g_has_proposal&&!g_picker_ready)||!g_hooks.location)return false;
    button("SAVING...",kUseX,kFooterY,kUseW,60,true);
    if(!g_hooks.location(g_lat,g_lon)) {
      std::snprintf(g_message,sizeof(g_message),"Location could not be saved. Please retry.");g_dirty=true;return false;
    }
    if(g_mode==Mode::setup)return advance();
    cancel();return true;
  }
  if(!g_picker_ready)return false;
  if(action==Control::pan) {
    if(first<-1280||first>1280||second<-720||second>720||(!first&&!second))return false;
    pan(-first,-second);return true;
  }
  bool changed=false;
  if(action==Control::zoom_in)changed=g_picker.ZoomIn();
  else if(action==Control::zoom_out)changed=g_picker.ZoomOut();
  if(changed){g_dirty=true;}
  return changed;
}
void draw() {
  if(!screens::is_active(screens::Id::maps))return;
  M5.Display.setFont(nullptr);
  const uint32_t layout=static_cast<uint32_t>(g_mode) |
      (static_cast<uint32_t>(g_flow.step)<<4) | (static_cast<uint32_t>(g_edit)<<8) |
      (static_cast<uint32_t>(g_location_menu)<<12) | (static_cast<uint32_t>(g_download_view)<<13) |
      (static_cast<uint32_t>(setup_ui::input_active())<<14);
  const bool structural=!g_layout_valid||layout!=g_layout;
  g_layout=layout;g_layout_valid=true;
  const bool preserve=g_map_visible&&location_step()&&!g_edit&&!structural;

  if(preserve) {
    M5.Display.fillRect(0,0,1280,kMapY,kBg);
    M5.Display.fillRect(0,kMapY+kMapH,1280,720-kMapY-kMapH,kBg);
  } else if(structural) {M5.Display.fillScreen(kBg);g_map_visible=false;}
  else {
    // Status, inventory and setup polling replace content, not static chrome.
    M5.Display.fillRect(0,100,1280,532,kBg);
  }
  focus_nav::clear();
  if(g_mode==Mode::setup&&g_hooks.brand)g_hooks.brand();
  if(g_mode==Mode::setup&&g_setup_paused)text("Reception paused during setup.",430,52,2);
  if(location_step()&&!(g_mode==Mode::setup&&g_location_menu))text(g_mode==Mode::setup?"SETUP: LOCATION":"LOCATION",16,24,2,kAccent);
  else if(g_mode!=Mode::setup||!g_hooks.brand)text(g_mode==Mode::setup?"ORCSDR SETUP":"LOCAL MAPS",32,28,3,kAccent);
  if(g_mode==Mode::setup){const char* labels[]={"1 / 5 WELCOME","2 / 5 WI-FI","3 / 5 LOCATION","4 / 5 MAPS","5 / 5 READY"};text(labels[static_cast<int>(g_flow.step)],430,24,3,kAccent);}
  button(maps_step()&&g_download_view?"BACK":"CANCEL",1050,20,195);
  if(location_step()&&g_mode==Mode::setup&&g_location_menu)setup_ui::draw(setup_ui::Page::location);
  else if(location_step())draw_picker();
  else if(maps_step()) {if(g_download_view)draw_downloads();else draw_inventory();}
  else if(g_flow.step==setup::Step::welcome) {
    text("Welcome to OrcSDR",32,150,3);text("Connect to Wi-Fi, set your location,",32,220,3);
    text("then choose your offline maps.",32,260,3);
    text("You can skip steps and return later.",32,320,3);button("QUICK START",32,390,260,46,true);
  } else if(g_flow.step==setup::Step::network) {
    setup_ui::draw(setup_ui::Page::network);
  } else {text("Setup complete",32,160,5);text("Your location and map choices are saved.",32,240,3);}
  if(g_mode==Mode::setup&&setup_ui::input_active()){g_dirty=false;return;}
  if(g_mode==Mode::setup) {
    if(g_flow.step!=setup::Step::welcome)button("BACK",32,632,180,80);
    if(location_step()&&!g_location_menu)button("SKIP",700,kFooterY,196);
    else button(g_flow.step==setup::Step::complete?"FINISH":"CONTINUE",920,632,310,80,true);
  }
  if(g_edit)draw_edit();
  if(g_message[0])text(g_message,32,location_step()&&!g_edit?704:610,2,TFT_ORANGE);
  g_dirty=false;
}
void service_navigation() {
  if(g_setup_paused&&!screens::is_active(screens::Id::maps)) {
    setup_ui::reset();
    g_setup_paused=false;if(g_hooks.setup_reception)g_hooks.setup_reception(false);
  }
}
void tick() {
  service_navigation();
  if(!screens::owns(screens::Id::maps))return;
  if(g_dirty)draw();
  const uint32_t now=millis();
  if(now-g_last_tick<250)return;
  g_last_tick=now;
  if(g_mode==Mode::setup&&setup_ui::poll())g_dirty=true;
  if(location_step()&&!(g_mode==Mode::setup&&g_location_menu)&&!g_edit&&maps::revision()!=g_last_map_revision) {
    draw_map();
  }
  if(maps_step()) {
    const auto previous=g_packs.revision;
    if(maps::snapshot(&g_packs)&&previous!=g_packs.revision)g_dirty=true;
    const auto previous_download=g_downloads;
    if(maps::downloads::snapshot(&g_downloads)) {
      const auto& before=previous_download;const auto& after=g_downloads;
      const bool layout_changed=before.phase!=after.phase||before.busy!=after.busy||
          before.pending!=after.pending||before.configured!=after.configured||
          before.connected!=after.connected||before.catalog_ready!=after.catalog_ready||
          before.sd_available!=after.sd_available||before.count!=after.count||
          std::strcmp(before.message,after.message);
      if(layout_changed)g_dirty=true;
      else if(!g_dirty&&g_download_view&&(before.completed_bytes!=after.completed_bytes||
          before.total_bytes!=after.total_bytes||before.verified_bytes!=after.verified_bytes||
          before.verify_total_bytes!=after.verify_total_bytes||before.verify_pass!=after.verify_pass||
          (after.phase==maps::downloads::Phase::verifying&&!after.verify_total_bytes)))
        draw_download_progress();
    }
  }
}
void touch(int x,int y,bool pressed,bool new_press) {
  if(!screens::owns(screens::Id::maps))return;
  if(!pressed) {
    if(g_map_touch&&!g_map_moved&&hit(x,y,kMapX,kMapY,kMapW,kMapH)) {
      g_picker.RecenterAtScreen(g_last_x-kMapX,g_last_y-kMapY);moved_picker();
    }
    g_drag=false;g_map_touch=false;return;
  }
  if(new_press)g_map_touch=false;
  if(maps_step()&&hit(x,y,32,174,1070,336)) {
    if(new_press) {g_drag=true;g_last_y=y;}
    else if(g_drag&&std::abs(y-g_last_y)>=60) {
      int& scroll=g_download_view?g_download_scroll:g_scroll;
      const int count=g_download_view?g_downloads.count:g_packs.count;
      scroll=std::clamp(scroll+(y<g_last_y?1:-1),0,std::max(0,count-4));
      g_last_y=y;g_details=false;g_dirty=true;return;
    }
  }
  if(g_mode==Mode::setup&&new_press&&
      (setup_ui::input_active()||((g_flow.step==setup::Step::network||
       (location_step()&&g_location_menu))&&y>=100&&y<632))) {
    const auto result=setup_ui::touch(g_flow.step==setup::Step::network?setup_ui::Page::network:setup_ui::Page::location,x,y);
    if(result==setup_ui::Result::picker){g_location_menu=false;const auto current=receiver_location::snapshot();g_lat=current.latitude_e7;g_lon=current.longitude_e7;picker_at(g_lat,g_lon);g_has_proposal=true;g_lat_set=g_lon_set=true;}
    g_dirty=true;return;
  }
  if(g_edit) {
    if(!new_press)return;
    if(hit(x,y,820,300,250,46))finish_edit();
    else if(hit(x,y,820,400,250,46)) {g_edit=0;g_dirty=true;}
    else {
      constexpr char keys[]="123456789-0<";
      for(int i=0;i<12;++i)if(hit(x,y,400+(i%3)*120,220+(i/3)*80,100,60)) {
        keyboard_input::Key key;if(keys[i]=='<')key.special=keyboard_input::Special::backspace;else key.ch=keys[i];
        (void)keyboard_input::apply_edit(g_entry,sizeof(g_entry),11,key);g_dirty=true;
      }
    }
    return;
  }
  if(location_step()&&!(g_mode==Mode::setup&&g_location_menu)&&new_press) {
    g_drag=false;
    if(hit(x,y,kLatX,kCoordinateY,kCoordinateW,46)) {begin_edit(1);return;}
    if(hit(x,y,kLonX,kCoordinateY,kCoordinateW,46)) {begin_edit(2);return;}
    // Overlay controls take precedence over panning the map underneath.
    if(hit(x,y,940,84,232,52)) {
      using maps::preferences::Consumer;
      if(!maps::preferences::set_labels(Consumer::picker,!maps::preferences::labels(Consumer::picker)))
        std::snprintf(g_message,sizeof(g_message),"Label preference could not be saved. Please retry.");
      else g_message[0]=0;
      g_dirty=true;return;
    }
    if(hit(x,y,kZoomX,kZoomInY,kZoomW,kZoomH)) {(void)control(Control::zoom_in);return;}
    if(hit(x,y,kZoomX,kZoomOutY,kZoomW,kZoomH)) {(void)control(Control::zoom_out);return;}
    if(hit(x,y,kPanX+72,kPanY,kZoomW,kZoomH)) {(void)control(Control::pan,0,-kPanStepY);return;}
    if(hit(x,y,kPanX,kPanY+64,kZoomW,kZoomH)) {(void)control(Control::pan,-kPanStepX,0);return;}
    if(hit(x,y,kPanX+144,kPanY+64,kZoomW,kZoomH)) {(void)control(Control::pan,kPanStepX,0);return;}
    if(hit(x,y,kPanX+72,kPanY+128,kZoomW,kZoomH)) {(void)control(Control::pan,0,kPanStepY);return;}
    if(hit(x,y,kUseX,kFooterY,kUseW,60)) {
      (void)control(Control::save);
      return;
    }
  }
  if(location_step()&&!(g_mode==Mode::setup&&g_location_menu)&&hit(x,y,kMapX,kMapY,kMapW,kMapH)) {
    if(!g_picker_ready)return;
    // Decide tap versus drag before changing the camera; touch-down must not
    // jump to the finger and queue a render that the first drag supersedes.
    if(new_press) {g_drag=true;g_map_touch=true;g_map_moved=false;g_last_x=x;g_last_y=y;}
    else if(g_drag&&(g_map_moved||std::abs(x-g_last_x)>=6||std::abs(y-g_last_y)>=6)&&
            (x!=g_last_x||y!=g_last_y)) {
      g_map_moved=true;g_picker.DragByGesture(x-g_last_x,y-g_last_y);
      g_last_x=x;g_last_y=y;moved_picker();
    }
    return;
  }
  if(!new_press)return;
  if(hit(x,y,1050,20,195,46)) {cancel();return;}
  if(g_mode==Mode::setup) {
    if(hit(x,y,32,632,180,80)) {(void)control(Control::back);return;}
    if((location_step()&&hit(x,y,700,kFooterY,196,46))||
       ((!location_step()||g_location_menu)&&hit(x,y,920,632,310,80))) {(void)control(g_flow.step==setup::Step::complete?Control::finish:Control::next);return;}
    if(g_flow.step==setup::Step::welcome&&hit(x,y,32,390,260,46)) {(void)control(Control::quick_start);return;}
  }
  if(maps_step()) {
    if(g_download_view) {
      if(hit(x,y,32,520,270,56)) {g_download_view=false;g_details=false;g_message[0]=0;g_dirty=true;return;}
      const int old_scroll=g_download_scroll,old_index=g_download_index;
      bool acted=false;
      if(hit(x,y,1120,174,128,70))g_download_scroll=std::max(0,g_download_scroll-1);
      else if(hit(x,y,1120,426,128,70))g_download_scroll=std::min(std::max(0,static_cast<int>(g_downloads.count)-4),g_download_scroll+1);
      else if(hit(x,y,32,174,1070,336)) {
        const int row=(y-174)/84;
        maps::downloads::Entry pack;
        if((y-174)%84<76&&g_download_scroll+row<g_downloads.count&&
            maps::downloads::entry(g_download_scroll+row,&pack))g_download_index=g_download_scroll+row;
      } else if(download_busy()) {
        if(hit(x,y,930,520,320,56)){maps::downloads::cancel();acted=true;}
      } else {
        if(hit(x,y,320,520,310,56)&&maps::downloads::can_check(g_downloads)){(void)maps::downloads::check_catalog();acted=true;}
        else if(hit(x,y,320,520,310,56)&&maps::downloads::can_reset(g_downloads)){(void)maps::downloads::reset_pending();acted=true;}
        else if(hit(x,y,1038,520,210,56)&&maps::downloads::can_retry(g_downloads))
          {(void)maps::downloads::retry();acted=true;}
        else if(hit(x,y,650,520,370,56)&&maps::downloads::can_install(g_downloads,g_download_index)) {
          maps::downloads::Entry pack;
          if(maps::downloads::entry(g_download_index,&pack)&&!pack.installed) {
            (void)maps::downloads::request_install(g_download_index);acted=true;
          }
        }
      }
      if(acted||old_scroll!=g_download_scroll||old_index!=g_download_index) {
        (void)maps::downloads::snapshot(&g_downloads);g_dirty=true;
      }
      return;
    }
    if(hit(x,y,32,174,1070,336)) {const int row=(y-174)/84;if(g_scroll+row<g_packs.count)g_index=g_scroll+row;g_details=false;g_dirty=true;}
    else if(hit(x,y,1120,174,128,70)) {g_scroll=std::max(0,g_scroll-1);g_dirty=true;}
    else if(hit(x,y,1120,426,128,70)) {g_scroll=std::min(std::max(0,static_cast<int>(g_packs.count)-4),g_scroll+1);g_dirty=true;}
    else if(hit(x,y,32,520,200,56)) {maps::rescan();g_packs.loaded=false;g_dirty=true;}
    else if(hit(x,y,250,520,200,56)) {g_details=!g_details;g_dirty=true;}
    else if(hit(x,y,470,520,310,56)) {g_download_view=true;g_details=false;g_message[0]=0;g_dirty=true;}
    else if(hit(x,y,800,520,300,56)&&g_packs.count&&g_hooks.pack) {
      g_index=std::clamp(g_index,0,static_cast<int>(g_packs.count)-1);
      char identity[256];std::snprintf(identity,sizeof(identity),"%s",g_packs.packs[g_index].identity);
      (void)control(Control::select_pack,0,0,identity);
    }
  }
}
void key(const keyboard_input::Key& key) {
  using keyboard_input::Special;
  if(g_mode==Mode::setup&&setup_ui::input_active()) {setup_ui::key(g_flow.step==setup::Step::network?setup_ui::Page::network:setup_ui::Page::location,key);g_dirty=true;return;}
  if(g_mode==Mode::setup&&key.special==Special::escape&&setup_ui::back()){g_dirty=true;return;}
  if(g_edit) {
    const auto action=keyboard_input::apply_edit(g_entry,sizeof(g_entry),11,key);
    if(action==keyboard_input::EditAction::accept)finish_edit();
    else if(action==keyboard_input::EditAction::cancel) {g_edit=0;g_dirty=true;}
    else if(action==keyboard_input::EditAction::edited)g_dirty=true;
    return;
  }
  if(key.special==Special::escape) {cancel();return;}
  if(location_step()&&g_picker_ready&&(key.ch=='+'||key.ch=='-')) {
    if(key.ch=='+')g_picker.ZoomIn();else g_picker.ZoomOut();g_dirty=true;return;
  }
  if(location_step()&&g_picker_ready&&key.ctrl) {
    int dx=0,dy=0;
    if(key.special==Special::left)dx=40;else if(key.special==Special::right)dx=-40;
    else if(key.special==Special::up)dy=40;else if(key.special==Special::down)dy=-40;
    if(dx||dy) {pan(dx,dy);return;}
  }
  focus_nav::Direction direction;
  bool move=true;
  if(key.special==Special::left)direction=focus_nav::Direction::left;
  else if(key.special==Special::right)direction=focus_nav::Direction::right;
  else if(key.special==Special::up)direction=focus_nav::Direction::up;
  else if(key.special==Special::down||key.special==Special::tab)direction=focus_nav::Direction::down;
  else move=false;
  if(move&&focus_nav::move(direction)) {
    focus_nav::Rect rect;if(focus_nav::focused(&rect))M5.Display.drawRect(rect.x-2,rect.y-2,rect.w+4,rect.h+4,TFT_YELLOW);
  } else if(key.special==Special::enter) {
    focus_nav::Rect rect;if(focus_nav::focused(&rect))touch(rect.x+rect.w/2,rect.y+rect.h/2,true,true);
  }
}
} // namespace orcsdr::map_screen
