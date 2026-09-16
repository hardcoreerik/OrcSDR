#include "adsb_dashboard.hpp"
#include "dashboard_registry.hpp"
#include "screen_controller.hpp"
#include "radio_session.hpp"
#include "offline_map.hpp"
#include <M5Unified.h>
#include <cassert>
#include <cstdlib>
#include <fstream>

// Board services remain outside the renderer under test.
namespace orcsdr::badge {
extern const uint8_t raw_start[] asm("_binary_orc_badge_104_rgb565_start") = {0};
extern const uint8_t raw_end[] asm("_binary_orc_badge_104_rgb565_end") = {0};
}
namespace orcsdr::offline_map {
bool available(){return false;}
void draw_base(lgfx::LovyanGFX&,const View&,uint16_t,uint16_t,uint16_t,uint16_t){}
}
namespace orcsdr::audio_header {
void draw_brand(const char* subtitle){M5.Display.labels.push_back(subtitle);}
void draw_battery(int32_t){}
void draw_home_button(){}void draw_mute_button(bool){}void draw_visualizer_button(bool){}void draw_settings_button(){}
}
bool label(const char* s){return std::find(M5.Display.labels.begin(),M5.Display.labels.end(),s)!=M5.Display.labels.end();}
void save(const char* name){if(const char* path=std::getenv("ORCSDR_UI_OUTPUT")){std::ofstream f(std::string(path)+"/"+name+".svg");f<<"<svg xmlns='http://www.w3.org/2000/svg' width='1280' height='720' viewBox='0 0 1280 720'>"<<M5.Display.svg<<"</svg>";}}
int main(){
 using namespace orcsdr;
 assert(screens::self_check());assert(dashboards::self_check());assert(adsb::self_check());
 radio::Session session;auto old=session.acquire(radio::Owner::adsb,radio::Band::adsb,1090000000,2048000);
 auto next=session.acquire(radio::Owner::flarm,radio::Band::flarm,868300000,960000);
 assert(!session.retuned(old,1090000000)&&session.owns(next));
 adsb::Settings settings;settings.flarm=true;
 settings.gain_supported=settings.gain_auto_supported=true;
 adsb::enter(settings);adsb::Snapshot snapshot;snapshot.revision=1;
 snapshot.receiver_running=true;adsb::set_live_snapshot(snapshot);adsb::draw();
 assert(label("NEED UTC TIME"));assert(label("FLARM 868"));save("flarm-waiting");
 settings.location_configured=true;settings.latitude_e7=460500000;settings.longitude_e7=145000000;
 adsb::enter(settings);snapshot.time_ready=true;snapshot.visible_count=snapshot.aircraft_count=1;
 auto& a=snapshot.aircraft[0];a.icao=0x123456;a.address_type=2;a.protocol_generation=7;a.channel=1;
 strlcpy(a.callsign,"123456",sizeof(a.callsign));strlcpy(a.type,"Glider",sizeof(a.type));a.has_callsign=true;
 a.has_altitude=a.has_position=a.has_speed=a.has_heading=a.has_vertical_rate=true;
 a.latitude=46.12f;a.longitude=14.6f;a.altitude_ft=4101;a.speed_kts=78;a.heading_deg=90;a.vertical_rate_fpm=-472;
 snapshot.total_messages=25;snapshot.flarm_v6=10;snapshot.flarm_v7=15;snapshot.message_rate=2;
 snapshot.effective_sps=960000;++snapshot.revision;adsb::set_live_snapshot(snapshot);adsb::draw();save("flarm-radar");
 for(int tab=1;tab<5;++tab){
  adsb::handle_touch(tab*256+128,680);
  for(const auto& text:M5.Display.labels){assert(text.find("FAA")==std::string::npos);if(text.find("MODE-S")!=std::string::npos)std::fprintf(stderr,"Unexpected tab%d label: %s\n",tab,text.c_str());assert(text.find("MODE-S")==std::string::npos);}
  if(tab==1)save("flarm-list");
  if(tab==2){assert(label("AIR V7"));assert(label("868.4 MHz"));assert(label("WGS84 ELLIPSOID"));save("flarm-target");}
  if(tab==3)save("flarm-stats");if(tab==4)save("flarm-settings");
 }
 // FLARM channel information must not activate the ADS-B gain controls.
 assert(adsb::handle_touch(80,450)==adsb::Action::none);
 assert(adsb::handle_touch(300,450)==adsb::Action::none);
 const auto saved_view = adsb::view();
 adsb::leave();adsb::resume(settings);adsb::draw();assert(adsb::view()==saved_view);
 snapshot.aircraft_count=snapshot.visible_count=0;++snapshot.session_id;++snapshot.revision;
 adsb::set_live_snapshot(snapshot);adsb::handle_touch(128,680);adsb::draw();assert(!label("123456"));
 // A protocol switch cannot reuse the previous receiver's aircraft snapshot.
 settings.flarm=false;adsb::enter(settings);
 assert(label("ADS-B 1090"));assert(!label("123456"));
 adsb::handle_touch(1152,680);
 assert(adsb::handle_touch(80,450)==adsb::Action::gain_auto);
 assert(adsb::handle_touch(300,450)==adsb::Action::gain_tenth_db);
 assert(adsb::self_check());
 std::puts("Aviation: FLARM/ADS-B renderer, protocol labels, mode reset, Settings return and tuner ownership passed");
}
