// Real picker/Settings UI and camera; only display, device storage and navigation hooks are faked.
#include "map_screen.hpp"
#include "map_serial.hpp"
#include "map_service.hpp"
#include "map_preferences.hpp"
#include "map_downloads.hpp"
#include "location_store.hpp"
#include "receiver_location.hpp"
#include "settings_app.hpp"
#include "navigation_service.hpp"
#include "dashboard_audio_control.hpp"
#include "text_editor.hpp"
#include "setup_ui.hpp"
#include <M5Unified.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
namespace {
std::map<std::string,std::vector<unsigned char>> nvs_data;
bool write_ok=true;
std::string serial_output;
int rescans=0,invalidations=0,download_checks=0,download_installs=0,download_cancels=0,download_resets=0;
orcsdr::maps::downloads::Snapshot download_status;
bool download_installed=false,expect_reset_feedback=true;
bool legacy_inventory=false;
uint32_t inventory_revision=1;
orcsdr::setup_ui::Snapshot setup_status;
void serial_command(const std::string& command,bool authenticated=true) {
  serial_output.clear();
  assert(orcsdr::maps::serial_control::handle(command.c_str(),authenticated,
      +[](const char* text){serial_output+=text;}));
}
bool serial_ok(){return serial_output=="RTL_MAP_OK\n";}
bool pause_ok=true,setup_paused=false;
int pause_calls=0,resume_calls=0;
bool setup_reception(bool pause) {
  if(pause) {++pause_calls;if(!pause_ok)return false;setup_paused=true;}
  else {++resume_calls;setup_paused=false;}
  return true;
}
orcsdr::NvsStore preferences;
orcmap::Viewport requested;
bool requested_labels=true;
int saves=0;
std::string chosen_pack;
orcsdr::maps::Frame map_result=orcsdr::maps::Frame::pending;
bool select_pack(const char* identity) {chosen_pack=identity;return true;}
bool visible(const char* value) {
  return std::find(M5.Display.strings.begin(),M5.Display.strings.end(),value)!=M5.Display.strings.end();
}
orcsdr::settings::State settings_state() {
  orcsdr::settings::State state;
  const auto location=orcsdr::receiver_location::snapshot();
  state.location_configured=location.configured;
  state.latitude_e7=location.latitude_e7;state.longitude_e7=location.longitude_e7;
  strlcpy(state.location_label,location.label,sizeof(state.location_label));
  state.radar_range_nm=25;
  return state;
}
bool save(int32_t latitude,int32_t longitude) {
  assert(!M5.Display.strings.empty()&&M5.Display.strings.back()=="SAVING...");
  ++saves;
  if(!orcsdr::location_store::save(preferences,{true,latitude,longitude}))return false;
  orcsdr::receiver_location::set(true,latitude,longitude);return true;
}
void click(int x,int y) {
  orcsdr::map_screen::touch(x,y,true,true);
  orcsdr::map_screen::touch(x,y,false,false);
  orcsdr::map_screen::tick();
}
}
namespace orcsdr {
NvsStore::~NvsStore()=default;
bool NvsStore::begin(const char*,bool){return true;}
bool NvsStore::put_bool(const char* key,bool value){return put_bytes(key,&value,1);}
bool NvsStore::is_key(const char* key) const {return nvs_data.count(key)!=0;}
size_t NvsStore::bytes_length(const char* key) const {return is_key(key)?nvs_data.at(key).size():0;}
size_t NvsStore::get_bytes(const char* key,void* out,size_t size) const {
  if(bytes_length(key)!=size)return 0;std::memcpy(out,nvs_data.at(key).data(),size);return size;
}
bool NvsStore::put_bytes(const char* key,const void* value,size_t size) {
  if(!write_ok)return false;const auto* bytes=static_cast<const unsigned char*>(value);
  nvs_data[key]={bytes,bytes+size};return true;
}
uint32_t NvsStore::get_u32(const char* key,uint32_t fallback) const {uint32_t value;return get_bytes(key,&value,4)?value:fallback;}
int32_t NvsStore::get_i32(const char* key,int32_t fallback) const {int32_t value;return get_bytes(key,&value,4)?value:fallback;}
bool NvsStore::get_bool(const char* key,bool fallback) const {bool value;return get_bytes(key,&value,1)?value:fallback;}
bool NvsStore::put_u32(const char* key,uint32_t value) {return put_bytes(key,&value,4);}
namespace navigation {
void open_settings(settings::Section section) {
  screens::begin_transition(screens::Id::settings,millis(),true);
  settings::enter(settings_state(),section);screens::finish_transition();
}
void close_settings() {settings::leave();screens::close_settings(millis());screens::finish_transition();}
}
namespace maps {
void cancel() {}
uint32_t revision() {return 0;}
bool snapshot(MapSnapshot* out) {
  *out=MapSnapshot{};out->loaded=true;out->sd=true;out->count=6;out->revision=inventory_revision;
  for(int i=0;i<6;++i) {auto& pack=out->packs[i];std::snprintf(pack.title,sizeof(pack.title),"Map %d",i);std::snprintf(pack.identity,sizeof(pack.identity),"pack-%d",i);pack.usable=true;}
  if(legacy_inventory) {
    auto& pack=out->packs[0];pack.kind=Kind::legacy;pack.usable=false;
    std::snprintf(pack.title,sizeof(pack.title),"Older map file (unsupported)");
    std::snprintf(pack.identity,sizeof(pack.identity),"lane_county_map");
  }
  return true;
}
bool running() {return true;}
void rescan() {++rescans;}
void invalidate() {++invalidations;}
void report(void (*write)(const char*)) {write("MAP_REPORT\n");}
Frame draw(const orcmap::Viewport& view,lgfx::LovyanGFX&,int,int,FrameInfo*,const char*,bool labels) {
  requested_labels=labels;requested=view;return map_result;
}
}
namespace maps::downloads {
bool snapshot(Snapshot* out){*out=download_status;return true;}
bool entry(size_t index,Entry* out){if(index>=download_status.count)return false;*out={};out->installed=download_installed;return true;}
bool check_catalog(){++download_checks;return true;}
bool request_install(size_t){++download_installs;return true;}
bool retry(){return false;}
bool reset_pending(){++download_resets;if(expect_reset_feedback)assert(visible("CLEARING..."));return true;}
void cancel(){++download_cancels;}
}
namespace audio_header {
void draw_brand(const char*) {} void draw_battery(int32_t) {}
void draw_mute_button(bool) {} void draw_visualizer_button(bool) {} void draw_settings_button() {}
}

}
void screen_tests() {
  // Real screen backgrounds are uint16_t, not the stub's int TFT constants.
  const int initial_map_clears=M5.Display.map_clears;
  M5.Display.drawString("stale",32,80);
  M5.Display.fillRect(16,72,1248,568,uint16_t{0x0841});
  assert(M5.Display.map_clears==initial_map_clears+1);
  assert(std::find(M5.Display.strings.begin(),M5.Display.strings.end(),"stale")==M5.Display.strings.end());
  using namespace orcsdr;
  receiver_location::set(true,440000000,-1230000000);
  navigation::open_settings(settings::Section::location_adsb);
  assert(visible("CHOOSE LOCATION ON MAP")&&!visible("ADS-B RADAR RANGE"));
  assert(visible("LOCATION")&&visible("SAVED LOCATION")&&!visible("PROFILE LABEL"));
  auto named=settings_state();strlcpy(named.location_label,"Springfield, Oregon",sizeof(named.location_label));
  M5.Display.strings.clear();settings::update(named);
  assert(visible("Springfield, Oregon")&&!visible("SAVED LOCATION"));
  auto unset=named;unset.location_configured=false;
  M5.Display.strings.clear();settings::update(unset);
  assert(visible("NOT SET")&&!visible("Springfield, Oregon"));
  M5.Display.strings.clear();settings::update(settings_state());
  assert(visible("SAVED LOCATION"));
  assert(settings::handle_touch(500,380).kind==settings::ActionKind::open_location_picker);
  auto external=settings_state();external.latitude_e7=450000000;
  const std::string long_pack(200,'x');
  std::snprintf(external.map_pack,sizeof(external.map_pack),"%s",long_pack.c_str());
  M5.Display.strings.clear();settings::update(external);
  assert(!visible(long_pack.c_str()));
  assert(visible((std::string(39,'x')+"...").c_str()));
  assert(std::string(settings::state().map_pack)==long_pack);
  assert(visible("45.0000000")); // Previously changed state silently without repainting.
  map_screen::configure({nullptr,save,nullptr});
  map_screen::open(map_screen::Mode::picker);
  assert(requested.zoom==3&&requested.width_px==1248&&requested.height_px==568);
  assert(requested_labels&&visible("LABELS ON"));
  const auto camera_before_toggle=requested;
  click(1000,110);
  assert(!requested_labels&&visible("LABELS OFF"));
  assert(requested.center_lat_deg==camera_before_toggle.center_lat_deg&&requested.center_lon_deg==camera_before_toggle.center_lon_deg&&requested.zoom==camera_before_toggle.zoom);
  maps::preferences::load();assert(!maps::preferences::labels(maps::preferences::Consumer::picker));
  click(1000,110);assert(requested_labels);
  const auto original=requested;
  map_result=maps::Frame::ready;map_screen::draw();
  const int clears=M5.Display.screen_clears,map_clears=M5.Display.map_clears;
  map_result=maps::Frame::pending;
  click(205,170);
  assert(M5.Display.screen_clears==clears&&M5.Display.map_clears==map_clears);
  assert(visible("Updating map..."));
  map_screen::open(map_screen::Mode::picker);
  map_screen::touch(800,400,true,true);map_screen::tick();
  assert(requested.center_lon_deg==original.center_lon_deg);
  map_screen::touch(820,400,true,false);map_screen::tick();
  assert(requested.center_lon_deg<original.center_lon_deg);
  const auto dragged=requested;
  map_screen::touch(820,400,false,false);map_screen::tick();
  assert(requested.center_lon_deg==dragged.center_lon_deg);
  map_screen::open(map_screen::Mode::picker);
  click(800,400);assert(requested.center_lon_deg>original.center_lon_deg);
  map_screen::open(map_screen::Mode::picker);
  map_screen::touch(800,400,true,true);
  map_screen::touch(802,402,true,false);map_screen::tick();
  assert(requested.center_lon_deg==original.center_lon_deg); // Small jitter is not a drag.
  map_screen::touch(802,402,false,false);map_screen::tick();
  assert(requested.center_lon_deg>original.center_lon_deg);
  map_screen::open(map_screen::Mode::picker);
  map_screen::touch(800,400,true,true);
  map_screen::touch(800,700,false,false);map_screen::tick();
  assert(requested.center_lon_deg==original.center_lon_deg); // Release outside cancels a tap.
  click(205,170); // right half-screen
  assert(requested.center_lon_deg!=original.center_lon_deg);
  const auto moved=requested;
  click(1220,110);assert(requested.zoom==4);
  assert(requested.center_lat_deg==moved.center_lat_deg&&requested.center_lon_deg==moved.center_lon_deg);
  click(1220,175);assert(requested.zoom==3);
  click(130,110);assert(requested.center_lat_deg>moved.center_lat_deg);
  click(130,240);assert(std::abs(requested.center_lat_deg-moved.center_lat_deg)<1e-7);
  click(55,170);assert(std::abs(requested.center_lon_deg-original.center_lon_deg)<1e-7);
  click(205,170);
  const auto selected=requested;
  write_ok=false;click(1100,675);
  assert(saves==1&&screens::owns(screens::Id::maps));
  assert(receiver_location::snapshot().longitude_e7==-1230000000);
  assert(visible("Location could not be saved. Please retry."));
  assert(visible("USE LOCATION")&&!visible("SAVING...")); // Failed write restores a retry action.
  write_ok=true;click(1100,675);
  assert(saves==2&&screens::owns(screens::Id::settings));
  const auto saved=location_store::load(preferences);
  assert(saved.configured&&saved.longitude==std::llround(selected.center_lon_deg*1e7));
  assert(settings::state().longitude_e7==saved.longitude&&saved.longitude!=-1230000000);
  assert(visible("SAVED LOCATION"));
  char decimal[32];std::snprintf(decimal,sizeof(decimal),"%.7f",saved.longitude/1e7);
  assert(visible(decimal));
  map_screen::open(map_screen::Mode::picker);click(205,170);click(1130,40);
  assert(saves==2&&settings::state().longitude_e7==saved.longitude);
  receiver_location::set(false,0,0);
  map_screen::open(map_screen::Mode::picker);click(1260,707);
  assert(saves==3&&screens::owns(screens::Id::settings)); // Center can be saved without first moving the map.
  receiver_location::set(true,900000000,0);
  map_screen::open(map_screen::Mode::picker);click(1260,707);
  assert(saves==4&&receiver_location::snapshot().latitude_e7==900000000);
  receiver_location::set(true,440000000,-1230000000);
  map_screen::open(map_screen::Mode::setup);
  click(1000,670);click(1000,670);
  assert(visible("Set receiver location"));
  assert(visible("USE NETWORK LOCATION")&&visible("SEARCH ZIP / ADDRESS"));
  click(200,420);assert(visible("USE LOCATION"));
  click(1130,40);
  map_screen::configure({nullptr,save,select_pack});
  map_screen::open(map_screen::Mode::inventory);
  assert(visible("Map 0")&&visible("Map 3")&&!visible("Map 4"));
  assert(visible("Manual SD: copy matching .pmtiles + .manifest.json to /orcmaps/."));
  assert(visible("Files + checksum: github.com/hardcoreerik/OrcMaps/releases"));
  legacy_inventory=true;
  map_screen::open(map_screen::Mode::inventory);
  M5.Display.strings.clear();click(330,548);
  assert(visible("Older map file (unsupported)"));
  assert(visible("Legacy ORCMAP1 format (unsupported)")&&!visible("lane_county_map"));
  serial_command("RTL_MAP LIST",false);
  assert(serial_output.find("id=lane_county_map")!=std::string::npos);
  legacy_inventory=false;map_screen::open(map_screen::Mode::inventory);
  serial_command("RTL_MAP HELP",false);
  assert(serial_output.find("matching .pmtiles + .manifest.json in /orcmaps/")!=std::string::npos);
  assert(serial_output.find("https://github.com/hardcoreerik/OrcMaps/releases")!=std::string::npos);
  click(600,548);assert(visible("Get offline maps")&&visible("Map source is not configured. Installed maps remain available."));
  // Transfer bytes and verification animation must never clear the whole page.
  download_status.configured=true;download_status.connected=true;
  download_status.catalog_ready=true;download_status.sd_available=true;
  download_status.count=1;download_status.busy=true;
  download_status.phase=maps::downloads::Phase::downloading;
  download_status.total_bytes=100000000;++download_status.revision;
  test_millis+=300;map_screen::tick();map_screen::tick();
  const int transfer_clears=M5.Display.screen_clears;
  assert(visible("BACK")&&!visible("CANCEL"));
  assert(visible("Back to the device: downloads continue. Stop Transfer pauses the download."));
  for(int i=0;i<8;++i) {
    download_status.completed_bytes+=1000000;++download_status.revision;
    test_millis+=300;map_screen::tick();map_screen::tick();
  }
  assert(M5.Display.screen_clears==transfer_clears);
  assert(visible("Downloading: 7.6 MB / 95.4 MB"));
  const int before_messages=M5.Display.screen_clears;
  for(int i=0;i<3;++i) {
    std::snprintf(download_status.message,sizeof(download_status.message),"Transfer update %d",i);
    ++download_status.revision;
    test_millis+=300;map_screen::tick();map_screen::tick();
    assert(M5.Display.screen_clears==before_messages);
    assert(visible("Downloading: 7.6 MB / 95.4 MB"));
    assert(!visible("Transfer update 0")); // Busy progress replaces the background message.
  }
  const int no_op_regions=M5.Display.region_clears;
  click(1250,600); // No control here: no repaint.
  click(1150,190); // Already at top: no full-screen repaint.
  assert(M5.Display.screen_clears==before_messages);
  assert(M5.Display.region_clears==no_op_regions);
  download_status.phase=maps::downloads::Phase::verifying;++download_status.revision;
  test_millis+=300;map_screen::tick();map_screen::tick();
  const int verifying_clears=M5.Display.screen_clears;
  for(int i=0;i<4;++i){test_millis+=300;map_screen::tick();map_screen::tick();}
  assert(M5.Display.screen_clears==verifying_clears);
  assert(visible("Checking map file and installing..."));
  download_status.verify_total_bytes=100ull*1024*1024;download_status.verify_pass=1;
  for(unsigned i=1;i<=4;++i) {
    download_status.verified_bytes=i*25ull*1024*1024;++download_status.revision;
    test_millis+=300;map_screen::tick();map_screen::tick();
    assert(M5.Display.screen_clears==verifying_clears);
  }
  assert(visible("Checking map file: 100.0 MB / 100.0 MB (100%)"));
  download_status.verify_pass=2;download_status.verified_bytes=25ull*1024*1024;++download_status.revision;
  test_millis+=300;map_screen::tick();map_screen::tick();
  assert(visible("Checking map file: 25.0 MB / 100.0 MB (25%)"));
  assert(M5.Display.screen_clears==verifying_clears);
  serial_command("RTL_MAP OPEN DOWNLOADS",false);
  assert(serial_output=="RTL_MAP_ERROR auth_required\n");
  const int before_download_entry=M5.Display.screen_clears;
  serial_command("RTL_MAP OPEN DOWNLOADS");assert(serial_ok());
  assert(M5.Display.screen_clears==before_download_entry+1);
  assert(map_screen::state().downloads&&visible("Get offline maps"));
  download_status.pending=true;download_status.busy=false;download_status.phase=maps::downloads::Phase::failed;
  test_millis+=300;map_screen::tick();map_screen::tick();
  assert(visible("Use original SD. RESET STUCK forgets the saved install."));
  const int reset_clears=M5.Display.screen_clears;click(400,548);assert(download_resets==1);
  assert(M5.Display.screen_clears==reset_clears);
  serial_command("RTL_MAP DOWNLOAD RESET",false);
  assert(serial_output=="RTL_MAP_ERROR auth_required\n"&&download_resets==1);
  expect_reset_feedback=false;serial_command("RTL_MAP DOWNLOAD RESET");
  assert(serial_ok()&&download_resets==2);expect_reset_feedback=true;
  download_status.pending=false;download_status.phase=maps::downloads::Phase::idle;
  serial_command("RTL_MAP STATUS",false);
  assert(serial_output.find("downloads=1")!=std::string::npos);
  std::snprintf(download_status.message,sizeof(download_status.message),"Not enough SD space. Free space or use a larger card.");
  serial_command("RTL_MAP DOWNLOAD LIST",false);
  assert(serial_output.find("verified=26214400 verify_total=104857600 verify_pass=2")!=std::string::npos);
  assert(serial_output.find("Not enough SD space. Free space or use a larger card.")!=std::string::npos);
  click(100,548);assert(visible("Choose an offline map"));
  const int inventory_clears=M5.Display.screen_clears;
  ++inventory_revision;test_millis+=300;map_screen::tick();map_screen::tick();
  assert(M5.Display.screen_clears==inventory_clears&&visible("Map 0"));
  for(int i=0;i<4;++i) {
    ++download_status.revision;download_status.completed_bytes+=1000000;
    test_millis+=300;map_screen::tick();map_screen::tick();
  }
  assert(M5.Display.screen_clears==inventory_clears);
  download_status={};++download_status.revision;
  test_millis+=300;map_screen::tick();map_screen::tick();
  const int guidance_clears=M5.Display.screen_clears;
  for(int i=0;i<4;++i) {
    ++download_status.revision;download_status.completed_bytes+=1000000;
    test_millis+=300;map_screen::tick();map_screen::tick();
  }
  assert(M5.Display.screen_clears==guidance_clears);
  assert(visible("Manual SD: copy matching .pmtiles + .manifest.json to /orcmaps/."));
  assert(visible("Files + checksum: github.com/hardcoreerik/OrcMaps/releases"));

  map_screen::touch(300,450,true,true);map_screen::touch(300,300,true,false);map_screen::touch(300,300,false,false);map_screen::tick();
  assert(visible("Map 4")&&!visible("Map 0"));
  click(300,450);click(900,540);assert(chosen_pack=="pack-4");
  navigation::open_settings(settings::Section::location_adsb);
  assert(settings::begin_location_method(0).kind==settings::ActionKind::location_ip_lookup);
  map_screen::load(preferences);
  map_screen::configure({nullptr,save,select_pack,nullptr,setup_reception});
  map_screen::open(map_screen::Mode::setup);
  assert(setup_paused&&pause_calls==1);
  assert(visible("Reception paused during setup."));
  setup_ui::configure({+[](setup_ui::Snapshot* out){*out=setup_status;},nullptr,nullptr,nullptr,nullptr});
  click(1000,670);
  const int network_clears=M5.Display.screen_clears;
  for(int i=0;i<3;++i) {
    std::snprintf(setup_status.message,sizeof(setup_status.message),"Network update %d",i);
    test_millis+=300;map_screen::tick();map_screen::tick();
    assert(M5.Display.screen_clears==network_clears&&visible(setup_status.message));
  }
  click(1000,670);
  assert(setup_paused&&resume_calls==0); // Wi-Fi/location steps retain the pause.
  click(1130,40);
  assert(!setup_paused&&resume_calls==1);
  map_screen::open(map_screen::Mode::setup);
  click(100,410); // Quick Start goes to Ready.
  write_ok=false;click(1000,670);
  assert(setup_paused&&resume_calls==1); // Failed Finish must retain the pause.
  write_ok=true;
  click(1000,670);
  assert(!setup_paused&&resume_calls==2);
  map_screen::open(map_screen::Mode::setup);
  setup_ui::configure({+[](setup_ui::Snapshot* out){
    out->count=1;strlcpy(out->networks[0].ssid,"Bench",33);out->networks[0].secure=true;
  },nullptr,nullptr,nullptr,nullptr});
  setup_ui::touch(setup_ui::Page::network,100,260);
  assert(setup_ui::input_active()&&text_editor::active());
  keyboard_input::Key secret;secret.ch='x';setup_ui::key(setup_ui::Page::network,secret);
  screens::begin_transition(screens::Id::home,millis());screens::finish_transition();
  map_screen::tick();
  assert(!text_editor::active()&&std::string(text_editor::value()).empty());
  // A later tick cannot close a newer editor owned by another screen.
  text_editor::begin("OTHER OWNER","keep",20,false,"SAVE");map_screen::tick();
  assert(text_editor::active()&&std::string(text_editor::value())=="keep");text_editor::close();
  assert(!setup_paused&&resume_calls==3); // External Home must release too.
  pause_ok=false;
  map_screen::open(map_screen::Mode::setup);
  assert(!screens::owns(screens::Id::maps)&&!setup_paused);
  pause_ok=true;
  map_screen::open(map_screen::Mode::picker);
  assert(!setup_paused&&pause_calls==4); // Ordinary map use never pauses reception.
  // Named serial controls exercise the same real UI, persistence and camera.
  const auto before_serial=receiver_location::snapshot();
  const auto opened=map_screen::state();
  serial_command("RTL_MAP OPEN SETUP",false);
  assert(serial_output=="RTL_MAP_ERROR auth_required\n"&&map_screen::state().mode==opened.mode);
  serial_command("RTL_MAP STATUS",false);assert(serial_output.find("RTL_MAP_SAVED configured=1")!=std::string::npos);
  assert(serial_output.find("latitude_e7=")==std::string::npos);
  assert(serial_output.find("longitude_e7=")==std::string::npos);
  serial_command("RTL_MAP STATUS");
  assert(serial_output.find("latitude_e7="+std::to_string(before_serial.latitude_e7))!=std::string::npos);
  serial_command("RTL_MAP "+std::string(400,'x'));assert(!serial_ok());
  serial_command("RTL_MAP PICKER SET 900000001 0");assert(!serial_ok());
  assert(receiver_location::snapshot().latitude_e7==before_serial.latitude_e7);
  serial_command("RTL_MAP PICKER SET 100000000 200000000");assert(serial_ok());
  assert(map_screen::state().latitude_e7==100000000&&receiver_location::snapshot().latitude_e7==before_serial.latitude_e7);
  serial_command("RTL_MAP PICKER PAN 1290 0");assert(!serial_ok());
  serial_command("RTL_MAP PICKER ZOOM IN trailing");assert(!serial_ok());
  serial_command("RTL_MAP PICKER ZOOM IN");assert(serial_ok());
  serial_command("RTL_MAP PICKER PAN 40 0");assert(serial_ok()&&map_screen::state().longitude_e7>200000000);
  write_ok=false;serial_command("RTL_MAP PICKER SAVE");assert(!serial_ok()&&map_screen::state().active);
  write_ok=true;serial_command("RTL_MAP CANCEL");assert(serial_ok());
  assert(receiver_location::snapshot().latitude_e7==before_serial.latitude_e7);
  serial_command("RTL_MAP PICKER SAVE");assert(!serial_ok()); // inactive owner
  serial_command("RTL_MAP OPEN PICKER");assert(serial_ok());
  serial_command("RTL_MAP PICKER SET 100000000 200000000");assert(serial_ok());
  serial_command("RTL_MAP PICKER SAVE");assert(serial_ok());
  assert(location_store::load(preferences).latitude==100000000&&!map_screen::state().active);
  const bool lora_labels=maps::preferences::labels(maps::preferences::Consumer::lora);
  serial_command("RTL_MAP LABELS ADSB OFF",false);assert(!serial_ok()&&invalidations==0);
  write_ok=false;serial_command("RTL_MAP LABELS ADSB OFF");
  assert(!serial_ok()&&invalidations==0&&maps::preferences::labels(maps::preferences::Consumer::adsb));write_ok=true;
  serial_command("RTL_MAP LABELS ADSB OFF");assert(serial_ok()&&invalidations==1);
  assert(!maps::preferences::labels(maps::preferences::Consumer::adsb)&&maps::preferences::labels(maps::preferences::Consumer::lora)==lora_labels);
  serial_command("RTL_MAP LABELS ADSB ON");assert(serial_ok());
  serial_command("RTL_MAP LABELS OTHER OFF");assert(!serial_ok());
  serial_command("RTL_MAP LABELS ADSB OFF extra");assert(!serial_ok());
  serial_command("RTL_MAP LIST",false);assert(serial_output.find("id=pack-5")!=std::string::npos);
  serial_command("RTL_MAP OPEN LIST");assert(serial_ok());
  serial_command("RTL_MAP SELECT no-such-pack");assert(!serial_ok());
  serial_command("RTL_MAP SELECT pack-4");assert(serial_ok()&&chosen_pack=="pack-4");
  const int prior_pauses=pause_calls,prior_resumes=resume_calls;
  map_screen::configure({nullptr,save,select_pack,nullptr,setup_reception});
  pause_ok=false;serial_command("RTL_MAP OPEN SETUP");assert(!serial_ok());pause_ok=true;
  serial_command("RTL_MAP OPEN SETUP");assert(serial_ok()&&setup_paused&&pause_calls==prior_pauses+2);
  serial_command("RTL_MAP SETUP FINISH");assert(!serial_ok()&&setup_paused);
  serial_command("RTL_MAP SETUP NEXT");assert(serial_ok()&&map_screen::state().step==1);
  serial_command("RTL_MAP SETUP BACK");assert(serial_ok()&&map_screen::state().step==0);
  serial_command("RTL_MAP SETUP QUICK");assert(serial_ok()&&map_screen::state().step==4);
  write_ok=false;serial_command("RTL_MAP SETUP FINISH");assert(!serial_ok()&&setup_paused);write_ok=true;
  serial_command("RTL_MAP SETUP FINISH");assert(serial_ok()&&!setup_paused&&resume_calls==prior_resumes+1);
  const auto completed_setup=nvs_data["maps_setup"];
  nvs_data.erase("maps_setup");map_screen::load(preferences);
  assert(map_screen::open(map_screen::Mode::setup,true));
  write_ok=false;serial_command("RTL_MAP SETUP NEXT");
  assert(!serial_ok()&&map_screen::state().step==0&&setup_paused);
  write_ok=true;serial_command("RTL_MAP CANCEL");assert(serial_ok()&&!setup_paused);
  nvs_data["maps_setup"]=completed_setup;map_screen::load(preferences);
  serial_command("RTL_MAP DOWNLOAD CHECK");assert(!serial_ok()&&download_checks==0);
  download_status.configured=true;download_status.connected=true;
  serial_command("RTL_MAP DOWNLOAD CHECK");assert(serial_ok()&&download_checks==1);
  download_status.catalog_ready=download_status.sd_available=true;download_status.count=1;
  serial_command("RTL_MAP DOWNLOAD INSTALL 1");assert(!serial_ok()&&download_installs==0);
  serial_command("RTL_MAP DOWNLOAD INSTALL 0",false);assert(!serial_ok()&&download_installs==0);
  download_installed=true;serial_command("RTL_MAP DOWNLOAD INSTALL 0");assert(!serial_ok());download_installed=false;
  serial_command("RTL_MAP DOWNLOAD INSTALL 0");assert(serial_ok()&&download_installs==1);
  download_status.busy=true;serial_command("RTL_MAP DOWNLOAD CANCEL");assert(serial_ok()&&download_cancels==1);
  serial_command("RTL_MAP OPEN DOWNLOADS");assert(serial_ok());
  const int stops_before_back=download_cancels;
  click(1140,40);
  assert(!map_screen::state().active&&download_cancels==stops_before_back&&download_status.busy);
  download_status={};
  settings::enter(settings_state(),settings::Section::system);
  const int clock_clears=M5.Display.screen_clears;
  assert(settings::begin_clock_edit());
  assert(visible("YEAR")&&visible("SYNC NTP")&&visible("SAVE"));
  assert(M5.Display.screen_clears==clock_clears);
  settings::leave();
  assert(!settings::begin_clock_edit());
  std::puts("MAP_SERIAL_PASS authentication, parser, shared save/pan/zoom, setup pause/finish and installer gates");
  std::puts("MAP_SCREEN_TEST_PASS (changed-point save/return, failure, cancel, pan, zoom, Settings redraw)");
}
