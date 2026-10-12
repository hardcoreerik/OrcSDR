#include "sstv_receiver.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <sys/stat.h>
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

// Native SSTV: one DSP producer (48k PCM -> 12k ring), one UI-task consumer.
// The critical decoder keeps two 800-byte luminance scanlines and scalar
// timing state.  There is no per-frame PCM buffer, FFT allocation or UI call.
// Timing and VIS tables: sstv_modes.cpp (slowrx/Dayton).
namespace orcsdr::sstv {
namespace {
constexpr uint32_t kRate=12000, kRingSize=4096, kMask=kRingSize-1;
constexpr uint16_t kMaxWidth=800;
constexpr size_t kGallery=4;
std::atomic<uint32_t> g_read{0},g_write{0},g_drops{0};
std::atomic<bool> g_on{false};
int16_t g_queue[kRingSize]{};
int32_t g_decim_sum=0;
uint8_t g_decim_n=0;
Mode g_requested=Mode::auto_vis;
const ModeSpec* g_spec=nullptr;
Metrics g_stats{};
Image g_live{};
Image g_saved[kGallery]{};
size_t g_saved_count=0;
uint32_t g_next_serial=1,g_frequency=145800000,g_revision=0,g_file_id=0;
// 1.6 KiB workspace for PD pairs; for other families, scan[0] is luminance.
uint8_t g_scan[2][kMaxWidth]{};
int32_t g_last_pixel[2]={-1,-1};
// Band-limited crossing estimator.  A light one-pole HP/LP pair rejects out
// of-band noise before fractional zero-cross interpolation.  No windowed FFT.
float g_in_prev=0,g_hp=0,g_lp=0,g_prev=0;
float g_last_edge=0,g_period=0,g_freq=0,g_level=0;
uint32_t g_ticks=0,g_sync_run=0;
double g_line_origin=0;
uint16_t g_line_index=0;
bool g_line_sync=false;
enum class VisStage:uint8_t { lead1, break1200, lead2, start1200, bits };
VisStage g_vis_stage=VisStage::lead1;
uint32_t g_phase_samples=0,g_phase_age=0,g_vis_start=0;
uint8_t g_vis_byte=0,g_vis_parity=0,g_vis_field=0;
bool g_vis_stop_good=false;

void* frame_alloc(size_t bytes) {
#ifdef ESP_PLATFORM
  return heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
  return std::malloc(bytes);
#endif
}
void frame_free(const uint16_t* p) {
  if(!p) return;
#ifdef ESP_PLATFORM
  heap_caps_free(const_cast<uint16_t*>(p));
#else
  std::free(const_cast<uint16_t*>(p));
#endif
}
void reset_vis() {
  g_vis_stage=VisStage::lead1;
  g_phase_samples=g_phase_age=0;
  g_vis_byte=g_vis_parity=g_vis_field=0;
}
void reset_picture() {
  g_spec=nullptr;
  g_stats.receiving=false;
  g_stats.row=g_stats.rows=g_stats.width=0;
  g_line_index=0;
  g_sync_run=0;
  g_line_sync=false;
  g_last_pixel[0]=g_last_pixel[1]=-1;
}
uint16_t gray(uint8_t v) {
  return static_cast<uint16_t>(((v&248)<<8)|((v&252)<<3)|(v>>3));
}
uint8_t tone_luma(float hz) {
  return static_cast<uint8_t>(std::clamp(static_cast<int>((hz-1500.f)*255.f/800.f+.5f),0,255));
}
void begin_picture(const ModeSpec* spec,uint8_t vis) {
  if(!spec) return;
  frame_free(g_live.pixels);
  g_live={};
  const size_t n=static_cast<size_t>(spec->width)*spec->height;
  auto* frame=static_cast<uint16_t*>(frame_alloc(n*sizeof(uint16_t)));
  if(!frame) {
    std::snprintf(g_stats.status,sizeof(g_stats.status),"INSUFFICIENT PSRAM");
    reset_picture();
    return;
  }
  std::memset(frame,0,n*sizeof(uint16_t));
  g_live.pixels=frame;
  g_live.width=spec->width;
  g_live.height=spec->height;
  g_live.frequency_hz=g_frequency;
  g_live.mode=spec->mode;
  g_live.partial=true;
  g_spec=spec;
  g_line_index=0;
  g_line_origin=static_cast<double>(g_ticks); // immediately after VIS stop bit
  g_line_sync=false; g_sync_run=0;
  g_last_pixel[0]=g_last_pixel[1]=-1;
  g_stats.width=spec->width;g_stats.rows=spec->height;
  g_stats.row=0;g_stats.vis=vis;g_stats.received_mode=spec->mode;
  g_stats.receiving=true;
  ++g_revision;
  std::snprintf(g_stats.status,sizeof(g_stats.status),"RECEIVING %s / MONO",spec->name);
}
void finish_picture() {
  g_stats.receiving=false;
  if(!g_live.pixels) return;
  g_live.serial=g_next_serial++;
  g_live.partial=false;
  frame_free(g_saved[kGallery-1].pixels);
  for(size_t n=kGallery-1;n>0;--n)g_saved[n]=g_saved[n-1];
  g_saved[0]=g_live;
  g_saved_count=std::min(kGallery,g_saved_count+1);
  g_live={};
  g_stats.frames++;
  reset_picture();
  ++g_revision;
  std::snprintf(g_stats.status,sizeof(g_stats.status),"FRAME SAVED IN RAM / MONO");
}
void commit_line() {
  if(!g_spec||!g_live.pixels)return;
  if(!g_line_sync)g_stats.sync_missed_lines++;
  const uint16_t row=static_cast<uint16_t>(g_line_index*(g_spec->family==3?2:1));
  auto* dst=const_cast<uint16_t*>(g_live.pixels);
  const int count=g_spec->family==3?2:1;
  for(int channel=0;channel<count;++channel) {
    if(row+channel>=g_live.height)break;
    const size_t off=static_cast<size_t>(row+channel)*g_live.width;
    for(uint16_t x=0;x<g_live.width;++x)
      dst[off+x]=gray(g_scan[channel][x]);
  }
  g_stats.row=std::min<uint16_t>(g_live.height,static_cast<uint16_t>(row+count));
  g_live.decoded_rows=g_stats.row;
  ++g_revision; // UI can paint newly committed rows without any frame wait
  ++g_line_index;
  g_line_origin+=static_cast<double>(g_spec->line_us)*kRate/1000000.;
  g_line_sync=false;
  g_last_pixel[0]=g_last_pixel[1]=-1;
  std::memset(g_scan,0,sizeof(g_scan));
  if(g_stats.row>=g_live.height)finish_picture();
}
double sync_offset_samples(const ModeSpec& m) {
  if(m.family!=2)return 0.;
  // Scottie has mid-line sync: first G then B, then sync, then R.
  const double chan=static_cast<double>(m.pixel_ns)*m.width/1.e9;
  return (m.porch_us*1.e-6+2.*chan+m.sep_us*1.e-6)*kRate;
}
void sync_edge(uint32_t sync_len) {
  if(!g_spec||g_line_sync)return;
  const double need=static_cast<double>(g_spec->sync_us)*kRate/1.e6;
  if(sync_len<need*.55 || sync_len>need*1.55)return;
  const double edge=static_cast<double>(g_ticks-sync_len);
  const double expected=g_line_origin+sync_offset_samples(*g_spec);
  const double error=edge-expected;
  // Avoid locking onto arbitrary 1200-Hz noise far from the expected line.
  const double tolerance=std::max(120.,need*1.2); // 10 ms minimum
  if(std::fabs(error)>tolerance)return;
  g_line_sync=true;
  g_stats.sync_locked_lines++;
  g_stats.line_error_ms=static_cast<float>(error*1000./kRate);
  // 25% PLL phase correction; line-to-line runout stays bounded.
  g_line_origin+=std::clamp(error,-tolerance,tolerance)*.25;
}
void sample_luma(float hz) {
  if(!g_spec || !g_live.pixels)return;
  const auto& m=*g_spec;
  const double pos=static_cast<double>(g_ticks)-g_line_origin;
  const double line=static_cast<double>(m.line_us)*kRate/1000000.;
  if(pos>=line) {
    commit_line();
    if(!g_spec)return;
  }
  const double shifted=static_cast<double>(g_ticks)-g_line_origin;
  const double pix_s=static_cast<double>(m.pixel_ns)*kRate/1.e9;
  const double first=(m.family==2 ? m.porch_us : m.sync_us+m.porch_us)*kRate/1.e6;
  const double second=first+3.*pix_s*m.width;
  // PD family Y0,Cr,Cb,Y1; no need to buffer Cr/Cb for monotone receive.
  for(int channel=0;channel<(m.family==3?2:1);++channel) {
    const double start=channel==0?first:second;
    const double x=(shifted-start)/pix_s;
    if(x<0 || x>=m.width)continue;
    const int pixel=static_cast<int>(x);
    if(pixel!=g_last_pixel[channel]) {
      g_scan[channel][pixel]=tone_luma(hz);
      g_last_pixel[channel]=pixel;
    } else {
      // A two-tap moving mean is sufficient for 2-6 audio samples / pixel.
      const unsigned now=tone_luma(hz);
      g_scan[channel][pixel]=static_cast<uint8_t>((g_scan[channel][pixel]+now+1)/2);
    }
  }
}
void vis_tick(float f) {
  const bool lead=f>=1740.f&&f<=2070.f;
  const bool sync=f>=1030.f&&f<=1400.f;
  if(g_vis_stage==VisStage::bits) {
    const uint32_t age=g_ticks-g_vis_start;
    // Sample 30ms bits at their centers (starting 45ms after start bit).
    while(g_vis_field<9 && age>=540u+360u*g_vis_field) {
      if(g_vis_field<7) {
        const bool one=f<1200.f;
        g_vis_byte|=static_cast<uint8_t>(one)<<g_vis_field;
        g_vis_parity+=one?1:0;
      } else if(g_vis_field==7)g_vis_parity+=f<1200.f?1:0;
      else g_vis_stop_good=sync;
      ++g_vis_field;
    }
    if(age>=3600u) {
      const bool valid=((g_vis_parity&1u)==0)&&g_vis_stop_good;
      const uint8_t vis=g_vis_byte;
      reset_vis();
      if(valid) {
        g_stats.good_vis++;
        const ModeSpec* spec=vis_spec(vis);
        if(spec) begin_picture(g_requested==Mode::auto_vis?spec:mode_spec(g_requested),vis);
        else std::snprintf(g_stats.status,sizeof(g_stats.status),"UNKNOWN VIS %u",vis);
      } else {
        g_stats.bad_vis++;
        std::snprintf(g_stats.status,sizeof(g_stats.status),"VIS PARITY/SYNC ERROR");
      }
    }
    return;
  }
  switch(g_vis_stage) {
    case VisStage::lead1:
      if(lead) {
        if(++g_phase_samples>=2400u) { g_vis_stage=VisStage::break1200;g_phase_samples=0;g_phase_age=0; }
      } else g_phase_samples=0;
      break;
    case VisStage::break1200:
      if(++g_phase_age>1200u) {reset_vis();break;}
      if(sync) ++g_phase_samples;
      else if(lead&&g_phase_samples>=24u&&g_phase_samples<=280u) {
        g_vis_stage=VisStage::lead2;g_phase_samples=1;g_phase_age=0;
      }
      break;
    case VisStage::lead2:
      if(lead){++g_phase_samples;g_phase_age=0;}
      else if(sync&&g_phase_samples>=2300u) {
        g_vis_stage=VisStage::start1200;g_phase_samples=1;g_phase_age=0;
      } else if(++g_phase_age>250u)reset_vis();
      break;
    case VisStage::start1200:
      if(sync) {
        if(++g_phase_samples>=180u) {
          g_vis_start=g_ticks-g_phase_samples+1;
          g_vis_field=g_vis_byte=g_vis_parity=0;g_vis_stop_good=false;
          g_vis_stage=VisStage::bits;
          std::snprintf(g_stats.status,sizeof(g_stats.status),"VIS HEADER");
        }
      } else if(++g_phase_age>60u)reset_vis();
      break;
    case VisStage::bits:break;
  }
}
void accept_sample(int16_t pcm) {
  ++g_ticks;
  // One pole high-pass (~120 Hz) followed by LP (~2.6 kHz).  Damping is
  // deliberately mild; preserves 1100-2300-Hz SSTV tone transitions.
  const float x=static_cast<float>(pcm);
  g_hp=.94f*(g_hp+x-g_in_prev);g_in_prev=x;
  g_lp+=.78f*(g_hp-g_lp);
  g_level=std::max(std::fabs(g_lp)/32768.f,g_level*.9995f);
  if(g_prev<=0.f&&g_lp>0.f&&g_level>.005f) {
    const float frac= -g_prev/(g_lp-g_prev);
    const float cross=static_cast<float>(g_ticks-1)+frac;
    const float period=cross-g_last_edge;
    if(g_last_edge>0.f&&period>4.4f&&period<13.f) {
      if(g_period==0.f)g_period=period;
      else g_period+=.25f*(period-g_period);
      g_freq=kRate/g_period;
    }
    g_last_edge=cross;
  }
  g_prev=g_lp;
  if(g_stats.receiving) {
    const bool is_sync=g_freq>1030.f&&g_freq<1400.f;
    if(is_sync)++g_sync_run;
    else if(g_sync_run){sync_edge(g_sync_run);g_sync_run=0;}
    sample_luma(g_freq);
  } else vis_tick(g_freq);
}
void put16(std::FILE* f,uint16_t v){std::fputc(v&255,f);std::fputc(v>>8,f);}
void put32(std::FILE* f,uint32_t v){put16(f,v&65535);put16(f,v>>16);}
} // namespace
void start(uint32_t freq) {
  g_on.store(false,std::memory_order_release);
  reset_vis();reset_picture();
  g_frequency=freq;g_stats.active=true;
  g_hp=g_lp=g_in_prev=g_prev=g_last_edge=g_period=g_freq=g_level=0.f;
  g_ticks=0;
  g_read.store(g_write.load(std::memory_order_acquire),std::memory_order_release);
  std::snprintf(g_stats.status,sizeof(g_stats.status),"WAITING FOR VIS");
  g_on.store(true,std::memory_order_release);
}
void stop(){
  g_on.store(false,std::memory_order_release);
  reset_vis();reset_picture();g_stats.active=false;
  g_read.store(g_write.load(std::memory_order_acquire),std::memory_order_release);
}
bool active(){return g_on.load(std::memory_order_acquire);}
void set_mode(Mode m){g_requested=mode_spec(m)?m:Mode::auto_vis;}
Mode selected_mode(){return g_requested;}
void set_frequency(uint32_t hz){g_frequency=hz;reset_picture();reset_vis();}
void offer_audio_48k(const int16_t* src,size_t n){
  if(!active()||!src)return;
  uint32_t w=g_write.load(std::memory_order_relaxed);
  for(size_t i=0;i<n;++i){
    g_decim_sum+=src[i];
    if(++g_decim_n<4)continue;
    g_decim_n=0;
    const int16_t sample=static_cast<int16_t>(g_decim_sum/4);
    g_decim_sum=0;
    if(w-g_read.load(std::memory_order_acquire)>=kRingSize){
      g_drops.fetch_add(1,std::memory_order_relaxed);continue;
    }
    g_queue[w&kMask]=sample;++w;
  }
  g_write.store(w,std::memory_order_release);
}
void service(size_t budget){
  if(!active())return;
  uint32_t r=g_read.load(std::memory_order_relaxed);
  const uint32_t end=g_write.load(std::memory_order_acquire);
  size_t n=0;
  while(r!=end&&n<budget){accept_sample(g_queue[r&kMask]);++r;++n;}
  g_read.store(r,std::memory_order_release);
  g_stats.samples+=static_cast<uint32_t>(n);
  g_stats.tone_hz=g_freq;g_stats.audio_peak=g_level;
  g_stats.signal=g_level>.01f;
  g_stats.audio_overruns=g_drops.load(std::memory_order_relaxed);
  g_stats.vis_phase=static_cast<uint8_t>(g_vis_stage);
  g_stats.vis_phase_samples=g_phase_samples;
}
Metrics metrics(){return g_stats;}
Image live_image(){return g_live;}
Image archived_image(size_t i){return i<g_saved_count?g_saved[i]:Image{};}
size_t archive_count(){return g_saved_count;}
void clear_gallery(){
  for(auto& a:g_saved){frame_free(a.pixels);a={};}
  g_saved_count=0;
}
uint32_t image_revision(){return g_revision;}
bool save_bmp(size_t index,char* out,size_t out_len,uint32_t utc) {
  if(!out||out_len<90)return false;
  Image im=index==SIZE_MAX?g_live:archived_image(index);
  if(!im.pixels||im.decoded_rows==0)return false;
  if(::mkdir("/sd/orcsdr",0775)!=0&&errno!=EEXIST)return false;
  if(::mkdir("/sd/orcsdr/sstv",0775)!=0&&errno!=EEXIST)return false;
  const uint32_t id=++g_file_id;
  const bool partial=im.partial||im.decoded_rows<im.height;
  char final[144],temporary[156];
  std::snprintf(final,sizeof(final),"/sd/orcsdr/sstv/rx_%lu_%lu_%lu.bmp",
    static_cast<unsigned long>(utc),static_cast<unsigned long>(g_frequency),
    static_cast<unsigned long>(id));
  std::snprintf(temporary,sizeof(temporary),"%s.part",final);
  std::FILE* fp=std::fopen(temporary,"wb");
  if(!fp)return false;
  const uint32_t stride=(im.width*3u+3u)&~3u;
  const uint32_t size=stride*im.height;
  std::fputc('B',fp);std::fputc('M',fp);
  put32(fp,size+54);put32(fp,0);put32(fp,54);
  put32(fp,40);put32(fp,im.width);put32(fp,im.height);
  put16(fp,1);put16(fp,24);put32(fp,0);put32(fp,size);
  put32(fp,2835);put32(fp,2835);put32(fp,0);put32(fp,0);
  for(int y=im.height-1;y>=0;--y){
    for(int x=0;x<im.width;++x){
      const uint16_t c=im.pixels[static_cast<size_t>(y)*im.width+x];
      std::fputc((c&31)<<3,fp);
      std::fputc(((c>>5)&63)<<2,fp);
      std::fputc(((c>>11)&31)<<3,fp);
    }
    for(uint32_t k=im.width*3u;k<stride;++k)std::fputc(0,fp);
  }
  const bool written=!std::ferror(fp);
  const bool closed=std::fclose(fp)==0;
  if(!written||!closed||std::rename(temporary,final)!=0){
    std::remove(temporary);return false;
  }
  // Append-only index: archive metadata survives reboot and SD scans can
  // recover BMPs if power was lost between rename and CSV flush.
  std::FILE* csv=std::fopen("/sd/orcsdr/sstv/index.csv","ab");
  if(!csv){std::remove(final);return false;}
  const int result=std::fprintf(csv,"%lu,%lu,%lu,%s,%u,%u,%u,%u,%u,%s\n",
    static_cast<unsigned long>(id),static_cast<unsigned long>(utc),
    static_cast<unsigned long>(im.frequency_hz),mode_name(im.mode),
    static_cast<unsigned>(im.width),static_cast<unsigned>(im.height),
    static_cast<unsigned>(im.decoded_rows),partial?1u:0u,
    static_cast<unsigned>(g_stats.sync_missed_lines),
    final+std::strlen("/sd/orcsdr/sstv/"));
  const bool logged=result>0&&!std::ferror(csv);
  const bool csv_closed=std::fclose(csv)==0;
  if(!logged||!csv_closed)return false; // BMP retained for recovery
  std::snprintf(out,out_len,"%s",final);
  return true;
}
bool self_check(){
  const ModeSpec* pd=vis_spec(95);
  if(!pd||pd->mode!=Mode::pd120||pd->width!=640||pd->height!=496||
      pd->line_us!=508480u||pd->sync_us!=20000u)return false;
  if((kRingSize&(kRingSize-1u))!=0)return false;
  for(unsigned code=1;code<128;++code)
    if(const ModeSpec* spec=vis_spec(static_cast<uint8_t>(code))){
      if(!spec->width||spec->width>kMaxWidth||spec->height>616||spec->pixel_ns==0)return false;
    }
  return tone_luma(1500.f)==0&&tone_luma(2300.f)==255;
}
} // namespace orcsdr::sstv
