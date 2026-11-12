#include "map_serial.hpp"
#include "map_screen.hpp"
#include "map_service.hpp"
#include "map_preferences.hpp"
#include "map_downloads.hpp"
#include "receiver_location.hpp"
#include "setup_flow.hpp"
#include <esp_attr.h>
#include <cstdio>
#include <cstring>

namespace orcsdr::maps::serial_control {
namespace {
EXT_RAM_BSS_ATTR MapSnapshot g_serial_inventory;
bool equal(const char* a,const char* b){return std::strcmp(a,b)==0;}
bool consumer(const char* name,preferences::Consumer* out) {
  const char* names[]={"PICKER","LORA","ADSB","FT8"};
  for(unsigned i=0;i<4;++i)if(equal(name,names[i])){*out=static_cast<preferences::Consumer>(i);return true;}
  return false;
}
}
bool handle(const char* command,bool authenticated,void (*write)(const char*)) {
  if(!command||std::strncmp(command,"RTL_MAP ",8))return false;
  if(!write)return true;
  if(std::strlen(command)>383){write("RTL_MAP_ERROR invalid_command\n");return true;}
  char op[16]{},arg[256]{},value[32]{},extra[32]{},trailing=0;
  const int count=std::sscanf(command+8,"%15s %255s %31s %31s %c",op,arg,value,extra,&trailing);
  if(count==1&&equal(op,"HELP")) {
    write("RTL_MAP HELP|STATUS|LIST|RESCAN|OPEN <PICKER|LIST|SETUP|DOWNLOADS>|CANCEL\n");
    write("RTL_MAP PICKER SET <latitude_e7> <longitude_e7>|PAN <east_px> <south_px>|ZOOM <IN|OUT>|SAVE\n");
    write("RTL_MAP SETUP NEXT|SKIP|BACK|QUICK|FINISH; RTL_MAP SELECT <pack_id>\n");
    write("RTL_MAP LABELS <PICKER|LORA|ADSB|FT8> <ON|OFF>\n");
    write("RTL_MAP DOWNLOAD LIST|CHECK|INSTALL <index>|RETRY|RESET|CANCEL\n");
    write("Manual SD: matching .pmtiles + .manifest.json in /orcmaps/; RESCAN, then SELECT.\n");
    write("Files and checksums: https://github.com/hardcoreerik/OrcMaps/releases\n");return true;
  }
  if(count==1&&equal(op,"STATUS")) {
    const auto view=map_screen::state();const auto saved=receiver_location::snapshot();
    char line[256];
    if(!authenticated) {
      std::snprintf(line,sizeof(line),"RTL_MAP_VIEW active=%u mode=%u step=%u proposal=%u editing=%u zoom=%u downloads=%u\n",
          view.active,static_cast<unsigned>(view.mode),view.step,view.proposal,view.editing,view.zoom,view.downloads);write(line);
      std::snprintf(line,sizeof(line),"RTL_MAP_SAVED configured=%u\n",saved.configured);write(line);
      report(write);return true;
    }
    std::snprintf(line,sizeof(line),"RTL_MAP_VIEW active=%u mode=%u step=%u proposal=%u editing=%u latitude_e7=%ld longitude_e7=%ld zoom=%u downloads=%u\n",
        view.active,static_cast<unsigned>(view.mode),view.step,view.proposal,view.editing,
        static_cast<long>(view.latitude_e7),static_cast<long>(view.longitude_e7),view.zoom,view.downloads);write(line);
    std::snprintf(line,sizeof(line),"RTL_MAP_SAVED configured=%u latitude_e7=%ld longitude_e7=%ld\n",
        saved.configured,static_cast<long>(saved.latitude_e7),static_cast<long>(saved.longitude_e7));write(line);
    report(write);return true;
  }
  if(count==1&&equal(op,"LIST")) {
    if(!snapshot(&g_serial_inventory)){write("RTL_MAP_ERROR busy\n");return true;}
    char line[512];
    std::snprintf(line,sizeof(line),"RTL_MAP_LIST count=%u loaded=%u sd=%u world=%u\n",
        g_serial_inventory.count,g_serial_inventory.loaded,g_serial_inventory.sd,g_serial_inventory.world);write(line);
    for(unsigned i=0;i<g_serial_inventory.count;++i) {
      const auto& pack=g_serial_inventory.packs[i];
      std::snprintf(line,sizeof(line),"RTL_MAP_PACK index=%u usable=%u id=%s title=\"%s\" zoom=%u-%u status=\"%s\"\n",
          i,pack.usable,pack.identity,pack.title,pack.min_zoom,pack.max_zoom,pack.status);write(line);
    }
    return true;
  }
  if(count==2&&equal(op,"DOWNLOAD")&&equal(arg,"LIST")) {
    downloads::Snapshot status;
    if(!downloads::snapshot(&status)){write("RTL_MAP_ERROR busy\n");return true;}
    char line[320];
    std::snprintf(line,sizeof(line),"RTL_MAP_DOWNLOAD phase=%u busy=%u configured=%u ready=%u count=%u done=%llu total=%llu verified=%llu verify_total=%llu verify_pass=%lu\n",
        static_cast<unsigned>(status.phase),status.busy,status.configured,status.catalog_ready,status.count,
        static_cast<unsigned long long>(status.completed_bytes),static_cast<unsigned long long>(status.total_bytes),
        static_cast<unsigned long long>(status.verified_bytes),static_cast<unsigned long long>(status.verify_total_bytes),
        static_cast<unsigned long>(status.verify_pass));write(line);
    for(unsigned i=0;i<status.count;++i) {
      downloads::Entry entry;if(!downloads::entry(i,&entry))continue;
      std::snprintf(line,sizeof(line),"RTL_MAP_DOWNLOAD_ENTRY index=%u installed=%u recommended=%u product=%s bytes=%llu title=\"%s\"\n",
          i,entry.installed,entry.recommended,entry.product,static_cast<unsigned long long>(entry.bytes),entry.title);write(line);
    }
    return true;
  }
  if(!authenticated){write("RTL_MAP_ERROR auth_required\n");return true;}
  using C=map_screen::Control;
  bool valid=true,ok=false;
  if(count==2&&equal(op,"OPEN")) {
    if(equal(arg,"PICKER"))ok=map_screen::open(map_screen::Mode::picker);
    else if(equal(arg,"LIST"))ok=map_screen::open(map_screen::Mode::inventory);
    else if(equal(arg,"SETUP"))ok=map_screen::open(map_screen::Mode::setup);
    else if(equal(arg,"DOWNLOADS"))ok=map_screen::open(map_screen::Mode::downloads);
    else valid=false;
  } else if(count==1&&equal(op,"CANCEL"))ok=map_screen::control(C::cancel);
  else if(count==1&&equal(op,"RESCAN")){rescan();map_screen::refresh();ok=true;}
  else if(count==2&&equal(op,"SELECT"))ok=map_screen::control(C::select_pack,0,0,arg);
  else if(count==3&&equal(op,"LABELS")) {
    preferences::Consumer target;
    if(!consumer(arg,&target)||(!equal(value,"ON")&&!equal(value,"OFF")))valid=false;
    else {ok=preferences::set_labels(target,equal(value,"ON"));if(ok){invalidate();map_screen::refresh();}}
  } else if(equal(op,"PICKER")) {
    int32_t first=0,second=0;
    if(count==4&&equal(arg,"SET")) {
      valid=setup::parse_e7(value,true,&first)&&setup::parse_e7(extra,false,&second);
      if(valid)ok=map_screen::control(C::set_proposal,first,second);
    } else if(count==4&&equal(arg,"PAN")) {
      valid=setup::parse_e7(value,false,&first)&&setup::parse_e7(extra,false,&second)&&
          first>=-1280&&first<=1280&&second>=-720&&second<=720&&(first||second);
      if(valid)ok=map_screen::control(C::pan,first,second);
    } else if(count==3&&equal(arg,"ZOOM")&&(equal(value,"IN")||equal(value,"OUT")))
      ok=map_screen::control(equal(value,"IN")?C::zoom_in:C::zoom_out);
    else if(count==2&&equal(arg,"SAVE"))ok=map_screen::control(C::save);
    else valid=false;
  } else if(count==2&&equal(op,"SETUP")) {
    if(equal(arg,"NEXT")||equal(arg,"SKIP"))ok=map_screen::control(C::next);
    else if(equal(arg,"BACK"))ok=map_screen::control(C::back);
    else if(equal(arg,"QUICK"))ok=map_screen::control(C::quick_start);
    else if(equal(arg,"FINISH"))ok=map_screen::control(C::finish);
    else valid=false;
  } else if(equal(op,"DOWNLOAD")) {
    downloads::Snapshot status;
    if(!downloads::snapshot(&status)){write("RTL_MAP_ERROR busy\n");return true;}
    if(count==2&&equal(arg,"CHECK")){if(downloads::can_check(status))ok=downloads::check_catalog();}
    else if(count==2&&equal(arg,"CANCEL")){if(status.busy||status.pending){downloads::cancel();ok=true;}}
    else if(count==2&&equal(arg,"RETRY")){if(downloads::can_retry(status))ok=downloads::retry();}
    else if(count==2&&equal(arg,"RESET")){if(downloads::can_reset(status))ok=downloads::reset_pending();}
    else if(count==3&&equal(arg,"INSTALL")) {
      int32_t index=0;valid=setup::parse_e7(value,false,&index)&&index>=0;
      downloads::Entry entry;
      if(valid&&downloads::can_install(status,index)&&downloads::entry(index,&entry)&&!entry.installed)
        ok=downloads::request_install(index);
    } else valid=false;
  } else valid=false;
  write(!valid?"RTL_MAP_ERROR invalid_command\n":ok?"RTL_MAP_OK\n":"RTL_MAP_ERROR rejected\n");
  return true;
}
}
