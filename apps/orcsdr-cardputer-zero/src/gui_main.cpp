#include "compact_renderer.hpp"
#include "dashboard_session.hpp"
#include "fm_session.hpp"
#include "settings.hpp"
#include "bias_tee.hpp"
#include <algorithm>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#ifdef ORC_HAVE_SDL
#include <SDL2/SDL.h>
#endif
#ifdef __linux__
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <poll.h>
#include <unistd.h>
#endif

namespace {
volatile std::sig_atomic_t g_quit_signal=0,g_palette_signal=0,g_dashboard_signal=0;
extern "C" void request_palette(int) { g_palette_signal=1; }
extern "C" void request_dashboard(int) { g_dashboard_signal=1; }
extern "C" void request_signal_exit(int) { g_quit_signal=1; }
using orc::zero::ControlModel;
using orc::zero::Key;
using orc::zero::Connection;
using orc::zero::FmSession;
using orc::zero::FmTelemetry;
using orc::zero::CompactRenderer;
struct Options { bool demo=false,null_audio=false;std::string radio;uint32_t device=0,freq=96100000;bool freq_explicit=false,audio_explicit=false,bias_explicit=false,bias_tee=false;
 std::string display="sdl",audio="default",screenshot,view="main",input;unsigned frames=0,dashboard=0; };
unsigned parse_unsigned(const std::string& s){size_t used=0;auto v=std::stoul(s,&used);if(used!=s.size()||v>UINT32_MAX)throw std::invalid_argument("bad integer");return static_cast<unsigned>(v);}
Options parse(int argc,char** argv){
 Options a;
 for(int i=1;i<argc;++i){const std::string x=argv[i];
  if(x=="--demo"){a.demo=true;continue;}if(x=="--null-audio"){a.null_audio=true;continue;}
  if(x=="--help") {std::cout<<"OrcSDR Cardputer Zero UI\n"
   <<"  --demo [--screenshot FILE] [--view main|weak|disconnected|entry|menu|gain|audio|muted|noaudio|help|long]\n"
   <<"  --display sdl|fb --device N --freq HZ --audio DEVICE [--input /dev/input/eventN]\n"
   <<"  --dashboard fm|adsb|acars|airband|rf-lab|wifi|weather|marine|pocsag|sstv|am|dead-air|shortwave|cb|ft8|p25|lora|satellite (live fbdev)\n"
   <<"  --bias-tee on|off (antenna DC power; overrides saved setting)\n"
   <<"  --null-audio (silent diagnostic) --frames N (finite simulator test)\n"
   <<"Keys: Z/C tune, S/D volume, F entry, M mute, G gain, T step, P palette, Space dashboards, Tab menu, Esc cancel, Q exit\n";std::exit(0);}
  if(++i>=argc)throw std::invalid_argument("missing option value for "+x);
  const std::string y=argv[i];
  if(x=="--device")a.device=parse_unsigned(y);
        else if(x=="--radio")a.radio=y;
  else if(x=="--bias-tee"){if(y!="on"&&y!="off")throw std::invalid_argument("--bias-tee requires on or off");a.bias_explicit=true;a.bias_tee=y=="on";}
  else if(x=="--freq"){a.freq=parse_unsigned(y);a.freq_explicit=true;}
  else if(x=="--audio"){a.audio=y;a.audio_explicit=true;}
  else if(x=="--display")a.display=y;
  else if(x=="--screenshot")a.screenshot=y;
  else if(x=="--dashboard"){
    if(y=="adsb")a.dashboard=1;
    else if(y=="acars")a.dashboard=2;
    else if(y=="airband")a.dashboard=3;
    else if(y=="rf-lab")a.dashboard=5;
    else if(y=="wifi")a.dashboard=6;
    else if(y=="weather")a.dashboard=7;
    else if(y=="marine")a.dashboard=8;
    else if(y=="pocsag")a.dashboard=9;
    else if(y=="sstv")a.dashboard=10;
    else if(y=="am")a.dashboard=11;
    else if(y=="dead-air")a.dashboard=12;
    else if(y=="shortwave")a.dashboard=13;
    else if(y=="cb")a.dashboard=14;
    else if(y=="ft8")a.dashboard=15;
    else if(y=="p25")a.dashboard=16;
    else if(y=="lora")a.dashboard=17;
    else if(y=="satellite")a.dashboard=18;
 else if(y=="dmr")a.dashboard=19;
    else if(y!="fm")throw std::invalid_argument("Unknown dashboard: "+y);
  }
  else if(x=="--view")a.view=y;
  else if(x=="--input")a.input=y;
  else if(x=="--frames")a.frames=parse_unsigned(y);
  else throw std::invalid_argument("unknown option "+x);
 }
 if(a.freq<orc::zero::kFmMinHz||a.freq>orc::zero::kFmMaxHz)throw std::invalid_argument("WFM supports 87.5-108 MHz");
 if(a.dashboard&&(a.demo||a.display!="fb"||(!a.screenshot.empty()&&!a.frames)))
   throw std::invalid_argument("--dashboard requires live fbdev; use --frames for a screenshot");
 if(a.display!="sdl"&&a.display!="fb")throw std::invalid_argument("display must be sdl or fb");
 return a;
}
void demo_state(ControlModel& m,const std::string& view){
 auto& s=m.s;s.simulated=true;s.connection=Connection::ready;
 s.applied_hz=101700000;s.requested_hz=s.applied_hz;s.applied_rate=orc::zero::kDefaultSpanHz;
 s.audio_ready=true;s.receiver_name="DEMO RTL-SDR";
 for(int i=0;i<256;++i){const double x=(i-150)/30.;
  const double p=210*std::exp(-x*x*2.3)+15+13*std::sin(i*.24);
  s.spectrum[size_t(i)]=uint8_t(std::clamp(p,0.,255.));}
 for(int j=0;j<orc::zero::kWaterfallRows;++j)for(int i=0;i<256;++i)
  s.waterfall[size_t(j)][size_t(i)]=uint8_t(std::clamp(int(s.spectrum[size_t(i)])+(j*31+i*3)%25-12,0,255));
 s.waterfall_rows=orc::zero::kWaterfallRows;
 if(view=="weak"){
  s.spectrum.fill(4);for(auto& row:s.waterfall)row.fill(4);
 }else if(view=="disconnected"){
  s.simulated=false;s.connection=Connection::disconnected;s.live_spectrum=false;
 }else if(view=="entry") {s.page=orc::zero::Page::entry;s.entry="103.9";}
 else if(view=="menu")s.page=orc::zero::Page::menu;
 else if(view=="gain"){s.page=orc::zero::Page::gain;s.gain_auto=false;s.gain_tenth_db=90;}
 else if(view=="audio")s.page=orc::zero::Page::audio;
 else if(view=="muted"){s.page=orc::zero::Page::audio;s.muted=true;}
 else if(view=="noaudio"){s.page=orc::zero::Page::audio;s.audio_ready=false;}
 else if(view=="help")s.page=orc::zero::Page::help;
 else if(view=="long"){s.page=orc::zero::Page::error;s.error="RTL-SDR USB PERMISSION DENIED";s.requested_hz=s.applied_hz=108000000;}
}
void sync_model(ControlModel& m,const FmTelemetry& s){
 auto& u=m.s;
 const bool just_ready=s.connection==Connection::ready&&u.connection!=Connection::ready;
 if(just_ready){m.hardware_ready(s.applied_hz,s.applied_rate_sps,s.supported_gains,s.receiver_name);}
 else {u.connection=s.connection;u.applied_hz=s.applied_hz;u.applied_rate=s.applied_rate_sps;
       u.gains=s.supported_gains;u.receiver_name=s.receiver_name;u.generation=s.generation;}
 if(s.connection==Connection::ready){
  // Only reconcile pending frequency after an actual applied readback.
  if(s.requested_hz==s.applied_hz && u.requested_hz!=s.applied_hz)
    u.requested_hz=s.applied_hz;
  u.gain_auto=s.gain_auto;u.gain_tenth_db=s.gain_tenth_db;
  u.live_spectrum=s.has_spectrum;u.spectrum=s.spectrum;
  u.waterfall=s.waterfall;u.waterfall_rows=s.waterfall_rows;
 }else {u.live_spectrum=false;u.waterfall_rows=0;}
 u.known_dropped_samples=s.known_queue_dropped_samples;u.audio_ready=s.audio_ready;
 static std::string last_error_displayed;
 if(s.last_error!=last_error_displayed){
  last_error_displayed=s.last_error;
  if(!s.last_error.empty()){u.page=orc::zero::Page::error;u.error=s.last_error;}
  else if(u.page==orc::zero::Page::error){u.page=orc::zero::Page::receive;u.error.clear();}
 }
}
void relay_controls(ControlModel& m,FmSession& session,const FmTelemetry& observed){
 if(m.s.connection!=Connection::ready)return;
 if(m.s.requested_hz!=observed.requested_hz)session.request_tune(m.s.requested_hz);
 if(m.s.gain_auto!=observed.gain_auto||(!m.s.gain_auto&&m.s.gain_tenth_db!=observed.gain_tenth_db))
  (void)session.request_gain(m.s.gain_auto,m.s.gain_tenth_db);
 session.set_volume(unsigned(m.s.volume));session.set_mute(m.s.muted);
}
void action(ControlModel& m,FmSession* session,Key key,char digit=0){
 const auto before=m.s.page;
 m.handle(key,digit);
 if(key==Key::palette)std::cout<<"Palette: "<<orcsdr::waterfall_style::palette_name(m.s.palette)<<std::endl;
 if(session&&key==Key::reconnect&&before!=orc::zero::Page::entry){
  const auto desired=m.s.requested_hz;
  // Reconnect is deliberately explicit. Do not spin hot on unplugged hardware.
  session->restart(desired);session->set_volume(unsigned(m.s.volume));session->set_mute(m.s.muted);m.s.connection=Connection::connecting;
 }
 if(session&&m.s.connection==Connection::ready)
  relay_controls(m,*session,session->snapshot());
}
#ifdef ORC_HAVE_SDL
bool translate_sdl(SDL_Keycode code,Key& key,char& digit){
 if(code>=SDLK_0&&code<=SDLK_9){key=Key::digit;digit=char('0'+code-SDLK_0);return true;}
 switch(code){
  case SDLK_LEFT:case SDLK_z:key=Key::left;break;
  case SDLK_RIGHT:case SDLK_c:key=Key::right;break;
  case SDLK_UP:case SDLK_f:key=Key::up;break;
  case SDLK_DOWN:case SDLK_x:key=Key::down;break;
  case SDLK_RETURN:key=Key::enter;break;
  case SDLK_ESCAPE:key=Key::escape;break;
  case SDLK_HOME:key=Key::home;break;
  case SDLK_BACKSPACE:key=Key::backspace;break;
  case SDLK_PERIOD:key=Key::point;break;
  case SDLK_TAB:key=Key::dashboards;break;
  case SDLK_b:key=Key::bias_tee;break;
  case SDLK_v:key=Key::frequency;break;
  case SDLK_m:key=Key::mute;break;
  case SDLK_p:key=Key::palette;break;
  case SDLK_SPACE:key=Key::menu;break;
  case SDLK_s:case SDLK_MINUS:key=Key::volume_down;break;
  case SDLK_d:case SDLK_EQUALS:key=Key::volume_up;break;
  case SDLK_g:key=Key::gain;break;case SDLK_t:key=Key::step;break;
  case SDLK_r:key=Key::reconnect;break;
  case SDLK_h:key=Key::help;break;case SDLK_q:key=Key::quit;break;
  default:return false;
 }return true;
}
int run_sdl(Options a,ControlModel& model,FmSession* session){
 if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS)!=0)throw std::runtime_error(SDL_GetError());
 struct SDLGuard{~SDLGuard(){SDL_Quit();}} guard;
 SDL_Window* window=SDL_CreateWindow("OrcSDR Cardputer Zero",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,
                                     960,510,SDL_WINDOW_SHOWN|SDL_WINDOW_RESIZABLE);
 if(!window)throw std::runtime_error(SDL_GetError());
 SDL_Renderer* renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED);
 if(!renderer)renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
 if(!renderer)throw std::runtime_error(SDL_GetError());
 SDL_RenderSetLogicalSize(renderer,320,170);
 SDL_Texture* tex=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGB565,SDL_TEXTUREACCESS_STREAMING,320,170);
 if(!tex)throw std::runtime_error(SDL_GetError());
 CompactRenderer frame;
 unsigned ticks=0;
 while(!model.exit_requested&&!g_quit_signal){
  SDL_Event e{};
  while(SDL_PollEvent(&e)){
   if(e.type==SDL_QUIT){model.exit_requested=true;break;}
   if(e.type==SDL_KEYDOWN){Key key{};char digit=0;
     if(translate_sdl(e.key.keysym.sym,key,digit))action(model,session,key,digit);}
  }
  if(g_palette_signal){g_palette_signal=0;action(model,session,Key::palette);}
  if(g_dashboard_signal){g_dashboard_signal=0;action(model,session,Key::dashboards);}
  if(session){const auto s=session->snapshot();sync_model(model,s);relay_controls(model,*session,s);}
  frame.render(model.s);
  SDL_UpdateTexture(tex,nullptr,frame.pixels.data(),320*2);
  SDL_RenderClear(renderer);SDL_RenderCopy(renderer,tex,nullptr,nullptr);SDL_RenderPresent(renderer);
  if(++ticks==a.frames&&a.frames)break;
  SDL_Delay(80); // 12.5 Hz cap; audio/USB workers are independent.
 }
 if(!a.screenshot.empty())frame.save_ppm(a.screenshot);
 SDL_DestroyTexture(tex);SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);
 return 0;
}
#endif
#ifdef __linux__
Key translate_evdev(unsigned code,char& digit){
 digit=0;
 if(code>=KEY_1&&code<=KEY_9){digit=char('1'+code-KEY_1);return Key::digit;}
 if(code==KEY_0){digit='0';return Key::digit;}
 switch(code){
  case KEY_LEFT:case KEY_Z:return Key::left;case KEY_RIGHT:case KEY_C:return Key::right;
  case KEY_UP:case KEY_F:return Key::up;case KEY_DOWN:case KEY_X:return Key::down;
  case KEY_ENTER:return Key::enter;case KEY_ESC:return Key::escape;case KEY_HOME:return Key::home;
  case KEY_BACKSPACE:return Key::backspace;case KEY_DOT:return Key::point;
  case KEY_TAB:return Key::dashboards;case KEY_V:return Key::frequency;
  case KEY_SPACE:return Key::menu;case KEY_P:return Key::palette;case KEY_M:return Key::mute;case KEY_S:case KEY_MINUS:return Key::volume_down;
  case KEY_D:case KEY_EQUAL:return Key::volume_up;case KEY_G:return Key::gain;
  case KEY_B:return Key::bias_tee;
  case KEY_T:return Key::step;case KEY_R:return Key::reconnect;
  case KEY_H:return Key::help;case KEY_Q:return Key::quit;
  default:return Key::escape; // caller must ignore unsupported scan codes
 }
}
int choose_input(const std::string& explicit_path){
 if(!explicit_path.empty()){
  const int fd=::open(explicit_path.c_str(),O_RDONLY|O_NONBLOCK|O_CLOEXEC);
  // An explicit path that no longer exists (an injector that exited, a renamed
  // device) must not leave the panel silently deaf; fall through to detection.
  if(fd>=0)return fd;
 }
 for(int i=0;i<32;++i){const std::string path="/dev/input/event"+std::to_string(i);
  const int fd=::open(path.c_str(),O_RDONLY|O_NONBLOCK|O_CLOEXEC);if(fd<0)continue;
  unsigned long bits[(KEY_MAX+sizeof(unsigned long)*8)/(sizeof(unsigned long)*8)]{};
  if(ioctl(fd,EVIOCGBIT(EV_KEY,sizeof(bits)),bits)>=0){
   const auto test=[&](unsigned key){return (bits[key/(8*sizeof(unsigned long))]>>(key%(8*sizeof(unsigned long))))&1UL;};
   if(test(KEY_ENTER)&&test(KEY_F)&&test(KEY_M))return fd;
  }
  ::close(fd);
 }return -1;
}
// Battery for the status bar. The gauge is a bq27220 on this board; if it is
// missing the bar simply omits the figure rather than showing a fake level.
static int read_battery_percent(){
  std::ifstream in("/sys/class/power_supply/bq27220-0/capacity");
  int value=-1;
  if(in>>value)return std::clamp(value,0,100);
  return -1;
}
int run_fb(const Options& a,ControlModel& model,FmSession* session){
 const int fd=::open("/dev/fb0",O_RDWR|O_CLOEXEC);
 if(fd<0)throw std::runtime_error("Cannot open /dev/fb0; check launcher/display permissions");
 fb_var_screeninfo v{};fb_fix_screeninfo f{};
 if(ioctl(fd,FBIOGET_VSCREENINFO,&v)<0||ioctl(fd,FBIOGET_FSCREENINFO,&f)<0){::close(fd);throw std::runtime_error("Framebuffer ioctl failed");}
 if(v.xres<320||v.yres<170||(v.bits_per_pixel!=16&&v.bits_per_pixel!=32)){::close(fd);throw std::runtime_error("Framebuffer must be >=320x170 at RGB565 or XRGB8888");}
 const auto bytes=static_cast<size_t>(f.smem_len);
 auto* mapped=static_cast<std::uint8_t*>(mmap(nullptr,bytes,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0));
 if(mapped==MAP_FAILED){::close(fd);throw std::runtime_error("Framebuffer mmap failed");}
 if(!v.red.length||!v.green.length||!v.blue.length||v.red.length>8||v.green.length>8||v.blue.length>8||
    (size_t(v.yoffset)+170)*f.line_length>bytes){
    munmap(mapped,bytes);::close(fd);throw std::runtime_error("Unsupported framebuffer color/stride configuration");
 }
 int keys=choose_input(a.input);
 if(keys<0)std::cerr<<"No matching evdev keyboard. Use --input /dev/input/eventN and check device permissions.\n";
 CompactRenderer renderer, dashboard_error;
 auto dashboards=std::make_unique<orc::zero::DashboardSession>();
    if(a.radio=="pluto")dashboards->set_radio_kind(orc::linux_sdr::RadioSource::Kind::pluto);
 auto select_dashboard=[&](unsigned selection){
   if(!session)return;
   if(selection!=dashboards->mode()){
     if(dashboards->mode())dashboards->stop();else session->stop();
     if(selection)dashboards->start(selection,a.device,a.audio,a.null_audio,model.s.requested_hz);
     else{
       session->start(a.device,model.s.requested_hz,a.audio,a.null_audio);
       session->set_volume(unsigned(model.s.volume));
       session->set_mute(model.s.muted);
     }
   }
   model.s.page=orc::zero::Page::receive;
 };
 auto input=[&](Key key,char digit=0){
   if(key==Key::home){
     // Global shortcut: leave any dashboard and return to the receive page.
     // Only restart the FM session when a dashboard actually owned the radio,
     // so pressing HOME on the receive page does not disturb a running receiver.
     if(dashboards->mode()){
       dashboards->stop();
       if(session){session->start(a.device,model.s.requested_hz,a.audio,a.null_audio);session->set_volume(unsigned(model.s.volume));session->set_mute(model.s.muted);}
     }
     model.s.page=orc::zero::Page::receive;return;
   }
   if(model.s.page==orc::zero::Page::dashboards){
     auto& selection=model.s.dashboard_selection;
     if(key==Key::up)selection=(selection+19)%20;
     else if(key==Key::down)selection=(selection+1)%20;
     else if(key==Key::enter){
       if(selection==4)model.s.page=orc::zero::Page::settings;
       else{select_dashboard(unsigned(selection));model.s.page=orc::zero::Page::receive;}
     }
     else if(key==Key::escape||key==Key::dashboards)model.s.page=orc::zero::Page::receive;
     else if(key==Key::quit)model.exit_requested=true;
     return;
   }
   if(model.s.page==orc::zero::Page::settings){
     if(key==Key::bias_tee){
       model.s.bias_tee=!model.s.bias_tee;model.dirty_settings=true;
       orc::linux_sdr::bias_tee.request(model.s.bias_tee);return;
     }
     if(key==Key::escape){model.s.page=orc::zero::Page::receive;return;}
     if(key!=Key::dashboards){
       if(key==Key::volume_up||key==Key::volume_down||key==Key::mute||key==Key::palette||key==Key::step||key==Key::quit)
         action(model,dashboards->mode()?nullptr:session,key,digit);
       return;
     }
   }
   if(key==Key::dashboards){
     model.s.dashboard_selection=int(dashboards->mode());
     model.s.page=orc::zero::Page::dashboards;
   }else if(dashboards->mode()&&key!=Key::quit&&key!=Key::palette){
     if(!dashboards->input(key,digit)&&key==Key::escape){
       model.s.dashboard_selection=int(dashboards->mode());
       model.s.page=orc::zero::Page::dashboards;
     }
   }else action(model,dashboards->mode()?nullptr:session,key,digit);
 };
 if(a.dashboard)select_dashboard(a.dashboard);
 // RGB565 to device-pixel, precomputed once for all 65536 colours. Converting
 // per pixel on every pass cost more than drawing and capped the panel at a few
 // frames a second, which is what made the waterfall step instead of flow.
 std::vector<std::uint32_t> lut(65536);
 for(std::uint32_t c=0;c<65536u;++c){
  const unsigned r=((c>>11)&31u)*255u/31u;
  const unsigned g=((c>>5)&63u)*255u/63u;
  const unsigned b=(c&31u)*255u/31u;
  const auto quant=[](unsigned value,unsigned bits){return value*((1u<<bits)-1u)/255u;};
  lut[c]=(quant(r,v.red.length)<<v.red.offset)|(quant(g,v.green.length)<<v.green.offset)|
    (quant(b,v.blue.length)<<v.blue.offset);
 }
 const bool fb_stats=std::getenv("ORC_FB_STATS")!=nullptr;
 std::uint32_t frames_drawn=0;
 auto stats_from=std::chrono::steady_clock::now();
 unsigned ticks=0;
 while(!model.exit_requested&&!g_quit_signal){
  if(keys>=0){pollfd pfd{keys,POLLIN,0};const int ready=::poll(&pfd,1,0);
    if(ready>0&&(pfd.revents&(POLLHUP|POLLERR|POLLNVAL))){::close(keys);keys=choose_input("");}
    else if(ready>0&&(pfd.revents&POLLIN)){
    input_event event{};
    while(::read(keys,&event,sizeof(event))==sizeof(event)){
     if(event.type!=EV_KEY||(event.value!=1&&event.value!=2))continue;
     Key key{};char digit=0;
     const unsigned c=event.code;
     if(c!=KEY_LEFT&&c!=KEY_Z&&c!=KEY_RIGHT&&c!=KEY_C&&c!=KEY_UP&&c!=KEY_DOWN&&
      c!=KEY_ENTER&&c!=KEY_ESC&&c!=KEY_BACKSPACE&&c!=KEY_DOT&&c!=KEY_TAB&&c!=KEY_F&&c!=KEY_X&&c!=KEY_V&&
      c!=KEY_HOME&&
      c!=KEY_SPACE&&c!=KEY_P&&c!=KEY_M&&c!=KEY_S&&c!=KEY_MINUS&&c!=KEY_D&&c!=KEY_EQUAL&&c!=KEY_G&&c!=KEY_T&&
      c!=KEY_B&&c!=KEY_R&&c!=KEY_H&&c!=KEY_Q&&!(c>=KEY_1&&c<=KEY_9)&&c!=KEY_0)continue;
     key=translate_evdev(c,digit);input(key,digit);
    }
   }}
  if(g_palette_signal){g_palette_signal=0;input(Key::palette);}
  if(g_dashboard_signal){g_dashboard_signal=0;input(Key::dashboards);}
  model.s.bias_tee_state=orc::linux_sdr::bias_tee.state();
  if(session&&!dashboards->mode()){const auto s=session->snapshot();sync_model(model,s);relay_controls(model,*session,s);}
  const bool show_dashboard=dashboards->mode()&&model.s.page==orc::zero::Page::receive;
  if(!show_dashboard)renderer.render(model.s);
  const auto& pixels=show_dashboard?dashboards->render(model.s.palette,dashboard_error):renderer.pixels;
  // Per-pixel conversion through a table. The multiply-and-divide per pixel was
  // costing more than the drawing and capping the panel at a few frames a second,
  // which is what made the waterfall look like it stepped rather than flowed.
  const int pixel_bytes=int(v.bits_per_pixel/8);
  for(int y=0;y<170;++y){
   const auto offset=(size_t(y)+v.yoffset)*f.line_length+size_t(v.xoffset)*(v.bits_per_pixel/8);
   if(offset+320*(v.bits_per_pixel/8)>bytes)continue;
   auto* row=mapped+offset;
   for(int x=0;x<320;++x){
    const std::uint32_t encoded=lut[pixels[size_t(y)*320+x]];
    auto* target=row+x*pixel_bytes;
    if(std::memcmp(target,&encoded,pixel_bytes)!=0)
      std::memcpy(target,&encoded,pixel_bytes);
   }
  }
  if((ticks%40)==0)model.s.battery_percent=read_battery_percent();
  ++frames_drawn;

  if(fb_stats){
   const auto now=std::chrono::steady_clock::now();
   const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(now-stats_from).count();
   if(elapsed>=5000){
    std::clog<<"fb "<<(frames_drawn*1000.0/double(elapsed))<<" fps"<<std::endl;
    frames_drawn=0;stats_from=now;
   }
  }
  if(++ticks==a.frames&&a.frames)break;
  std::this_thread::sleep_for(std::chrono::milliseconds(33));
 }
 if(!a.screenshot.empty()){
   if(dashboards->mode())renderer.pixels=dashboards->render(model.s.palette,dashboard_error);
   if(!renderer.save_ppm(a.screenshot))throw std::runtime_error("Could not write PPM screenshot");
 }
 if(keys>=0) ::close(keys);
 munmap(mapped,bytes);
 ::close(fd);
 return 0;
}
#endif
}
int main(int argc,char**argv){
 try{
  std::signal(SIGINT,request_signal_exit);
  std::signal(SIGTERM,request_signal_exit);
  #ifdef SIGUSR1
  std::signal(SIGUSR1,request_palette);std::signal(SIGUSR2,request_dashboard);
#endif
  Options a=parse(argc,argv);ControlModel model;
  orc::zero::UserSettings saved{};
  if(!a.demo&&orc::zero::load_settings(orc::zero::settings_path(),saved)){
    if(!a.freq_explicit)a.freq=saved.frequency_hz;
    if(!a.audio_explicit)a.audio=saved.audio_device;
    if(!a.bias_explicit)a.bias_tee=saved.bias_tee;
  }
  model.s.bias_tee=a.demo?false:a.bias_tee;
  orc::linux_sdr::bias_tee.request(model.s.bias_tee);
  model.s.palette=a.demo?0:saved.palette;
  model.restore(a.freq,a.demo?30:int(saved.volume),a.demo?false:saved.mute,a.demo?2:int(saved.step_index));
  FmSession session;FmSession* owner=nullptr;
  if(a.demo)demo_state(model,a.view);
  else{
    if(!a.dashboard)session.start(a.device,a.freq,a.audio,a.null_audio);
    session.set_volume(unsigned(model.s.volume));
    session.set_mute(model.s.muted);
    owner=&session;
  }
  if(!a.screenshot.empty()&&a.frames==0){
   CompactRenderer r;r.render(model.s);
   if(!r.save_ppm(a.screenshot))throw std::runtime_error("Could not write PPM screenshot");
   session.stop();std::cout<<"Saved actual 320x170 renderer pixels: "<<a.screenshot<<'\n';return 0;
  }
  int rc=0;
  if(a.display=="fb"){
#ifdef __linux__
    rc=run_fb(a,model,owner);
#else
    throw std::runtime_error("Linux framebuffer not available on this host");
#endif
  }else{
#ifdef ORC_HAVE_SDL
    rc=run_sdl(a,model,owner);
#else
    throw std::runtime_error("SDL2 development headers not built; install libsdl2-dev");
#endif
  }
  if(!a.demo){
    orc::zero::UserSettings settings{};
    orc::zero::load_settings(orc::zero::settings_path(),settings);
    settings.frequency_hz=(model.s.applied_hz>=orc::zero::kFmMinHz&&model.s.applied_hz<=orc::zero::kFmMaxHz)?model.s.applied_hz:a.freq;
    settings.palette=model.s.palette;settings.bias_tee=model.s.bias_tee;
    settings.volume=unsigned(model.s.volume);settings.mute=model.s.muted;
    settings.step_index=unsigned(model.step_index);
    settings.audio_device=a.audio;
    settings.gain_auto=model.s.gain_auto;settings.gain_tenth_db=model.s.gain_tenth_db;
    if(model.dirty_settings && !orc::zero::save_settings(orc::zero::settings_path(),settings))
      std::cerr<<"Could not save per-user OrcSDR settings; continuing clean shutdown.\n";
  }
  session.stop();return rc;
 }catch(const std::exception& e){std::cerr<<"orc-zero-gui: "<<e.what()<<'\n';return 1;}
}
