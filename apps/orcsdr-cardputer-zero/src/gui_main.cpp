#include "compact_renderer.hpp"
#include "fm_session.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
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
using orc::zero::ControlModel;
using orc::zero::Key;
using orc::zero::Connection;
using orc::zero::FmSession;
using orc::zero::FmTelemetry;
using orc::zero::CompactRenderer;
struct Options { bool demo=false,null_audio=false;uint32_t device=0,freq=96100000;
 std::string display="sdl",audio="default",screenshot,view="main",input;unsigned frames=0; };
unsigned parse_unsigned(const std::string& s){size_t used=0;auto v=std::stoul(s,&used);if(used!=s.size()||v>UINT32_MAX)throw std::invalid_argument("bad integer");return static_cast<unsigned>(v);}
Options parse(int argc,char** argv){
 Options a;
 for(int i=1;i<argc;++i){const std::string x=argv[i];
  if(x=="--demo"){a.demo=true;continue;}if(x=="--null-audio"){a.null_audio=true;continue;}
  if(x=="--help") {std::cout<<"OrcSDR Cardputer Zero UI\n"
   <<"  --demo [--screenshot FILE] [--view main|weak|disconnected|entry|menu|gain|audio|muted|noaudio|help|long]\n"
   <<"  --display sdl|fb --device N --freq HZ --audio DEVICE [--input /dev/input/eventN]\n"
   <<"  --null-audio (silent diagnostic) --frames N (finite simulator test)\n"
   <<"Keys: Z/C tune, S/D volume, F entry, M mute, G gain, T step, Tab menu, Esc cancel, Q exit\n";std::exit(0);}
  if(++i>=argc)throw std::invalid_argument("missing option value for "+x);
  const std::string y=argv[i];
  if(x=="--device")a.device=parse_unsigned(y);
  else if(x=="--freq")a.freq=parse_unsigned(y);
  else if(x=="--audio")a.audio=y;
  else if(x=="--display")a.display=y;
  else if(x=="--screenshot")a.screenshot=y;
  else if(x=="--view")a.view=y;
  else if(x=="--input")a.input=y;
  else if(x=="--frames")a.frames=parse_unsigned(y);
  else throw std::invalid_argument("unknown option "+x);
 }
 if(a.freq<orc::zero::kFmMinHz||a.freq>orc::zero::kFmMaxHz)throw std::invalid_argument("WFM supports 87.5-108 MHz");
 if(a.display!="sdl"&&a.display!="fb")throw std::invalid_argument("display must be sdl or fb");
 return a;
}
void demo_state(ControlModel& m,const std::string& view){
 auto& s=m.s;s.simulated=true;s.connection=Connection::ready;
 s.applied_hz=101700000;s.requested_hz=s.applied_hz;s.applied_rate=300000;
 s.audio_ready=true;s.receiver_name="DEMO RTL-SDR";
 for(int i=0;i<256;++i){const double x=(i-150)/30.;
  const double p=210*std::exp(-x*x*2.3)+15+13*std::sin(i*.24);
  s.spectrum[size_t(i)]=uint8_t(std::clamp(p,0.,255.));}
 for(int j=0;j<36;++j)for(int i=0;i<256;++i)
  s.waterfall[size_t(j)][size_t(i)]=uint8_t(std::clamp(int(s.spectrum[size_t(i)])+(j*31+i*3)%25-12,0,255));
 s.waterfall_rows=36;
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
 if(s.connection==Connection::error&&u.page!=orc::zero::Page::error){
  u.page=orc::zero::Page::error;u.error=s.last_error;
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
 m.handle(key,digit);
 if(session&&key==Key::reconnect&&m.s.connection!=Connection::ready){
  const auto desired=m.s.requested_hz;
  // Reconnect is deliberately explicit. Do not spin hot on unplugged hardware.
  session->start(0,desired);session->set_volume(unsigned(m.s.volume));session->set_mute(m.s.muted);m.s.connection=Connection::connecting;
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
  case SDLK_UP:key=Key::up;break;case SDLK_DOWN:key=Key::down;break;
  case SDLK_RETURN:key=Key::enter;break;
  case SDLK_ESCAPE:key=Key::escape;break;
  case SDLK_BACKSPACE:key=Key::backspace;break;
  case SDLK_PERIOD:key=Key::point;break;
  case SDLK_TAB:key=Key::menu;break;
  case SDLK_f:key=Key::frequency;break;
  case SDLK_m:key=Key::mute;break;
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
 while(!model.exit_requested){
  SDL_Event e{};
  while(SDL_PollEvent(&e)){
   if(e.type==SDL_QUIT){model.exit_requested=true;break;}
   if(e.type==SDL_KEYDOWN){Key key{};char digit=0;
     if(translate_sdl(e.key.keysym.sym,key,digit))action(model,session,key,digit);}
  }
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
  case KEY_UP:return Key::up;case KEY_DOWN:return Key::down;
  case KEY_ENTER:return Key::enter;case KEY_ESC:return Key::escape;
  case KEY_BACKSPACE:return Key::backspace;case KEY_DOT:return Key::point;
  case KEY_TAB:return Key::menu;case KEY_F:return Key::frequency;
  case KEY_M:return Key::mute;case KEY_S:case KEY_MINUS:return Key::volume_down;
  case KEY_D:case KEY_EQUAL:return Key::volume_up;case KEY_G:return Key::gain;
  case KEY_T:return Key::step;case KEY_R:return Key::reconnect;
  case KEY_H:return Key::help;case KEY_Q:return Key::quit;
  default:return Key::escape; // caller must ignore unsupported scan codes
 }
}
int choose_input(const std::string& explicit_path){
 if(!explicit_path.empty())return ::open(explicit_path.c_str(),O_RDONLY|O_NONBLOCK|O_CLOEXEC);
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
int run_fb(const Options& a,ControlModel& model,FmSession* session){
 const int fd=::open("/dev/fb0",O_RDWR|O_CLOEXEC);
 if(fd<0)throw std::runtime_error("Cannot open /dev/fb0; check launcher/display permissions");
 fb_var_screeninfo v{};fb_fix_screeninfo f{};
 if(ioctl(fd,FBIOGET_VSCREENINFO,&v)<0||ioctl(fd,FBIOGET_FSCREENINFO,&f)<0){::close(fd);throw std::runtime_error("Framebuffer ioctl failed");}
 if(v.xres<320||v.yres<170||(v.bits_per_pixel!=16&&v.bits_per_pixel!=32)){::close(fd);throw std::runtime_error("Framebuffer must be >=320x170 at RGB565 or XRGB8888");}
 const auto bytes=static_cast<size_t>(f.smem_len);
 auto* mapped=static_cast<std::uint8_t*>(mmap(nullptr,bytes,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0));
 if(mapped==MAP_FAILED){::close(fd);throw std::runtime_error("Framebuffer mmap failed");}
 const int keys=choose_input(a.input);
 if(keys<0)std::cerr<<"No matching evdev keyboard. Use --input /dev/input/eventN and check device permissions.\n";
 CompactRenderer renderer;
 unsigned ticks=0;
 while(!model.exit_requested){
  if(keys>=0){pollfd pfd{keys,POLLIN,0};if(poll(&pfd,1,0)>0&&(pfd.revents&POLLIN)){
    input_event event{};
    while(::read(keys,&event,sizeof(event))==sizeof(event)){
     if(event.type!=EV_KEY||(event.value!=1&&event.value!=2))continue;
     Key key{};char digit=0;
     const unsigned c=event.code;
     if(c!=KEY_LEFT&&c!=KEY_Z&&c!=KEY_RIGHT&&c!=KEY_C&&c!=KEY_UP&&c!=KEY_DOWN&&
      c!=KEY_ENTER&&c!=KEY_ESC&&c!=KEY_BACKSPACE&&c!=KEY_DOT&&c!=KEY_TAB&&c!=KEY_F&&
      c!=KEY_M&&c!=KEY_S&&c!=KEY_MINUS&&c!=KEY_D&&c!=KEY_EQUAL&&c!=KEY_G&&c!=KEY_T&&
      c!=KEY_R&&c!=KEY_H&&c!=KEY_Q&&!(c>=KEY_1&&c<=KEY_9)&&c!=KEY_0)continue;
     key=translate_evdev(c,digit);action(model,session,key,digit);
    }
   }}
  if(session){const auto s=session->snapshot();sync_model(model,s);relay_controls(model,*session,s);}
  renderer.render(model.s);
  for(int y=0;y<170;++y){auto* row=mapped+y*f.line_length;
   if(v.bits_per_pixel==16)std::memcpy(row,renderer.pixels.data()+size_t(y)*320,320*2);
   else for(int x=0;x<320;++x){const auto color=renderer.pixels[size_t(y)*320+x];
    const uint32_t rgb=uint32_t((color>>11)&31)*255/31<<16 |uint32_t((color>>5)&63)*255/63<<8 |uint32_t(color&31)*255/31;
    std::memcpy(row+x*4,&rgb,sizeof(rgb));}}
  if(++ticks==a.frames&&a.frames)break;
  std::this_thread::sleep_for(std::chrono::milliseconds(80));
 }
 if(!a.screenshot.empty())renderer.save_ppm(a.screenshot);
 if(keys>=0)::close(keys);munmap(mapped,bytes);::close(fd);return 0;
}
#endif
}
int main(int argc,char**argv){
 try{
  Options a=parse(argc,argv);ControlModel model;
  model.restore(a.freq,30,false,2);
  FmSession session;FmSession* owner=nullptr;
  if(a.demo)demo_state(model,a.view);
  else{session.start(a.device,a.freq,a.audio,a.null_audio);owner=&session;}
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
  session.stop();return rc;
 }catch(const std::exception& e){std::cerr<<"orc-zero-gui: "<<e.what()<<'\n';return 1;}
}