#include "sstv_receiver.hpp"
#include "sstv_auto_detect.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#include <esp_timer.h>
#endif

// Native SSTV: one DSP producer (48k PCM -> 12k ring), one UI-task consumer.
// The critical decoder keeps two 800-byte luminance scanlines and scalar
// timing state.  There is no per-frame PCM buffer, FFT allocation or UI call.
// Timing and VIS tables: sstv_modes.cpp (slowrx/Dayton).
namespace orcsdr::sstv {
namespace {
constexpr uint32_t kRate=12000, kRingSize=4096, kMask=kRingSize-1;
constexpr uint16_t kMaxWidth=800;
// The zero-crossing tone estimator needs a few periods to settle on 1200 Hz, so a sync pulse is
// recognised later than the autocorrelation estimator recognises pixel tones. Measured with the
// ORC test pattern (sstv_pattern_test): every image came out shifted left by 3.0-3.2 ms in both
// Robot 36 and PD120, i.e. a fixed time lag, not a number of pixels. The pulse start is moved
// back by this many 12 kHz samples before it positions the raster. The best value was 37 for
// Robot 36 and 39 for PD120 (their pixels are 3.3 and 2.3 samples wide); 38 served both, and modes with a
// different sync length have their own calibrated figure in sync_latency() below.
constexpr double kSyncStartLatencySamples=38.0;
// Chroma noise cleanup (see clean_chroma): taps 0 = none, 3 = 1-2-1 smoothing; core = chroma offset below which colour is removed (ramps to full strength at 2x).
constexpr int kChromaTaps=3,kChromaCore=12;
// Where a sync pulse really started, in samples before the point where the tone estimator first saw it. The
// estimator's delay depends on the pulse and on what precedes it, so each mode has its own figure, found by
// sweeping the value on the host against the independent colour encoder (tests/sstv_colour_test.cpp) and
// taking the best picture correlation. Real receivers add their own small delay; confirm with a recording.
double sync_latency(const ModeSpec& m){
#ifdef ORCSDR_SSTV_TESTING
  // Host calibration sweeps only (SSTV_LATENCY_SWEEP=<samples>); never compiled into the firmware.
  static const char* sweep=std::getenv("SSTV_LATENCY_SWEEP");
  if(sweep)return std::atof(sweep);
#endif
  switch(m.mode) {
    case Mode::martin_m1:return 36.;
    case Mode::martin_m2:return 34.;
    case Mode::scottie_s1:case Mode::scottie_s2:return 40.;
    case Mode::scottie_dx:return 47.;
    case Mode::pd50:case Mode::pd120:return 40.;
    case Mode::pd160:case Mode::pd180:case Mode::pd240:case Mode::pd290:return 42.;
    case Mode::pd90:return 44.;
    default:return kSyncStartLatencySamples;   // Robot 36 / 72
  }
}
constexpr size_t kGallery=4;
std::atomic<uint32_t> g_read{0},g_write{0},g_drops{0},g_offered{0};
// Drop bursts are recorded in-band so the decoder can keep its sample clock
// true: a marker says "N samples were lost immediately before ring slot I".
// Producer writes head, consumer writes tail (single producer, single consumer).
constexpr uint32_t kGapMarks=8;
uint32_t g_gap_index[kGapMarks]{};
uint32_t g_gap_count[kGapMarks]{};
std::atomic<uint32_t> g_gap_head{0},g_gap_tail{0};
uint32_t g_pending_gap=0;   // producer-only: dropped since the last stored sample
std::atomic<bool> g_on{false};
// The 8 KiB audio ring lives in PSRAM, allocated by start(): internal RAM is nearly full on the Tab5
// (a static here, together with the other SSTV buffers, stopped the firmware from booting).
int16_t* g_queue=nullptr;
int32_t g_decim_sum=0;
uint8_t g_decim_n=0;
Mode g_requested=Mode::auto_vis;
AutoStrategy g_auto_strategy=AutoStrategy::vis_sync;
const ModeSpec* g_spec=nullptr;
Metrics g_stats{};
Image g_live{};
Image g_saved[kGallery]{};
size_t g_saved_count=0;
uint32_t g_next_serial=1,g_frequency=145800000,g_revision=0;
// Every mode is described by up to four scan segments per transmitted line (see build_layout). The per-line
// pixel values live in a PSRAM workspace of eight rows: slots 0-3 hold the segments of the line being received,
// slots 6-7 hold the cleaned-up chroma while a row is painted;
// slots 3-5 are also the Robot 36 carry-over (previous luma, latest R-Y, latest B-Y).
enum class Chan:uint8_t{y,ry,by,r,g,b,chroma};   // chroma: Robot 36, R-Y on even lines and B-Y on odd lines
struct Seg{double start_s,pix_s;uint32_t start_us,dwell_ns;uint16_t px;Chan ch;uint8_t row;};
struct Layout{
  Seg seg[4]{};uint8_t count=0;
  bool yuv=false,interlaced=false;   // yuv: Y/R-Y/B-Y colour; interlaced: Robot 36 chroma sharing
  double sep_start_s=0,sep_end_s=0;  // Robot 36 separator window that tells R-Y from B-Y
};
Layout g_layout{};
uint8_t* g_work=nullptr;
uint8_t* scan(int slot){return g_work+static_cast<size_t>(slot)*kMaxWidth;}
float g_sep_sum=0;uint16_t g_sep_n=0;
int32_t g_last_pixel[4]={-1,-1,-1,-1};
// Band-limited crossing estimator.  A light one-pole HP/LP pair rejects out
// of-band noise before fractional zero-cross interpolation.  No windowed FFT.
float g_in_prev=0,g_hp=0,g_lp=0,g_prev=0;
float g_period=0,g_freq=0,g_level=0,g_env=0;
uint32_t g_last_edge_tick=0;
float g_last_edge_frac=0;
bool g_have_edge=false;
// Separate high-temporal-resolution luminance estimator: keep the original
// crossing-based VIS/sync path unchanged while measuring pixels every sample.
float g_ac_y2=0,g_ac_p=0,g_ac_q=0,g_luma_freq=0;
uint16_t g_ac_freq_lut[513]{};
bool g_ac_lut_ready=false;
// 64-bit sample clock: a 32-bit counter at 12 kHz wraps after ~99.4 h, which
// would invalidate the double raster origin (g_line_origin) mid-image on an
// unattended receiver. Edge/pulse differences that must stay wrap-safe use
// explicit uint32_t subtraction instead.
uint64_t g_ticks=0;
uint32_t g_sync_run=0;
// Sync-tone measurement: the estimator is slow to settle, so only the middle of a pulse (after the first 40 samples
// and before the last 10) is averaged. Short pulses (Martin) have no settled part and are not measured.
constexpr uint32_t kSyncPlateaus=24u;
constexpr uint32_t kMinToneSamples=48u;   // 4 ms at 12 kHz: shorter than any real sync pulse's settled part
float g_plat[kSyncPlateaus]{};uint32_t g_plat_n=0;uint32_t g_sync_pulses=0;uint64_t g_sync_tone_tick=0;
void (*g_observer)(const int16_t*,size_t)=nullptr;
uint64_t g_last_sync_start=0;
bool g_have_sync_start=false;
double g_line_origin=0;
uint16_t g_line_index=0;
// Per-reception counters (one image); the lifetime counters live in g_stats.
uint32_t (*g_utc_source)()=nullptr;
uint64_t g_last_end_tick=0;
bool g_have_end=false;
uint64_t g_last_service_us=0;
uint32_t g_next_session=1,g_session_id=0;
uint16_t g_session_locked=0,g_session_missed=0;
uint8_t g_session_vis=0;
bool g_line_sync=false;
uint8_t g_consecutive_missed=0;
// Lines in a row that may pass without a sync pulse before the picture is given up (sync lost). At the edge of sensitivity misses
// come in bursts; the line timing is held by the clock between pulses, so a few more lines can safely be predicted. Chosen on 22
// recordings of Robot 36 at about -113 dBm: 4 split 15 of them into pieces and made 6 junk images; 8 or more gave one image each.
#ifndef SSTV_MAX_MISSED
#define SSTV_MAX_MISSED 8
#endif
constexpr uint8_t kMaxConsecutiveMissed=SSTV_MAX_MISSED;
enum class VisStage:uint8_t { lead1, break1200, lead2, start1200, bits };
VisStage g_vis_stage=VisStage::lead1;
uint32_t g_phase_samples=0,g_phase_age=0;
uint64_t g_vis_start=0;
uint8_t g_vis_byte=0,g_vis_parity=0,g_vis_field=0;
uint16_t g_vis_votes[9]{};
uint16_t g_vis_ones[9]{};

// Image frames are reference counted. The gallery and the live image each own one
// reference; a background writer (SD archive) takes its own with pin_archived().
// A pinned frame therefore stays valid after gallery eviction, clear_gallery() or
// the receiver moving on, and is freed by whoever releases the last reference.
struct Frame {
  std::atomic<uint32_t> refs;
  uint32_t bytes;
};
std::atomic<uint32_t> g_frames_alive{0},g_frame_bytes{0};
#ifdef ORCSDR_SSTV_TESTING
unsigned g_test_fail_frames=0;   // host tests: make the next N frame allocations fail, as a fragmented PSRAM would
#endif
Frame* frame_create(size_t pixel_count) {
#ifdef ORCSDR_SSTV_TESTING
  if(g_test_fail_frames) { --g_test_fail_frames; return nullptr; }
#endif
  const size_t bytes=sizeof(Frame)+pixel_count*sizeof(uint16_t);
#ifdef ESP_PLATFORM
  void* raw=heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
  void* raw=std::malloc(bytes);
#endif
  if(!raw)return nullptr;
  auto* frame=static_cast<Frame*>(raw);
  frame->refs.store(1,std::memory_order_relaxed);
  frame->bytes=static_cast<uint32_t>(bytes);
  g_frames_alive.fetch_add(1,std::memory_order_relaxed);
  g_frame_bytes.fetch_add(frame->bytes,std::memory_order_relaxed);
  return frame;
}
uint16_t* frame_pixels(Frame* frame){return reinterpret_cast<uint16_t*>(frame+1);}
void frame_retain(const void* handle) {
  if(handle)static_cast<Frame*>(const_cast<void*>(handle))->refs.fetch_add(1,std::memory_order_relaxed);
}
void frame_release(const void* handle) {
  if(!handle)return;
  auto* frame=static_cast<Frame*>(const_cast<void*>(handle));
  if(frame->refs.fetch_sub(1,std::memory_order_acq_rel)!=1)return;
  g_frames_alive.fetch_sub(1,std::memory_order_relaxed);
  g_frame_bytes.fetch_sub(frame->bytes,std::memory_order_relaxed);
#ifdef ESP_PLATFORM
  heap_caps_free(frame);
#else
  std::free(frame);
#endif
}
void reset_vis() {
  g_vis_stage=VisStage::lead1;
  g_phase_samples=g_phase_age=0;
  g_vis_byte=g_vis_parity=g_vis_field=0;
  std::memset(g_vis_votes,0,sizeof(g_vis_votes));
  std::memset(g_vis_ones,0,sizeof(g_vis_ones));
}
void clear_pixel_marks();
void reset_picture() {
  g_spec=nullptr;
  g_stats.receiving=false;
  g_stats.row=g_stats.rows=g_stats.width=0;
  g_line_index=0;
  g_sync_run=0;
  g_line_sync=false;
  g_consecutive_missed=0;
  clear_pixel_marks();
}
void clear_pixel_marks(){for(int i=0;i<4;++i)g_last_pixel[i]=-1;g_sep_sum=0;g_sep_n=0;}
uint16_t rgb565(int r,int g,int b){
  r=std::clamp(r,0,255);g=std::clamp(g,0,255);b=std::clamp(b,0,255);
  return static_cast<uint16_t>(((r&248)<<8)|((g&252)<<3)|(b>>3));
}
// Y / R-Y / B-Y bytes (tone 1500..2300 Hz mapped to 0..255) to RGB, as in slowrx's YUV path.
uint16_t yuv565(int y,int cr,int cb){
  return rgb565((100*y+140*cr-17850)/100,(100*y-71*cr-33*cb+13260)/100,(100*y+178*cb-22695)/100);
}
uint8_t tone_luma(float hz) {
  return static_cast<uint8_t>(std::clamp(static_cast<int>((hz-1500.f)*255.f/800.f+.5f),0,255));
}
Layout build_layout(const ModeSpec& m){
  Layout L;
  const uint32_t w=m.width;
  const uint32_t chan_us=static_cast<uint32_t>(static_cast<uint64_t>(m.pixel_ns)*w/1000u);
  auto add=[&](uint32_t start_us,uint16_t px,Chan ch,uint8_t row=0){
    Seg& g=L.seg[L.count++];
    g.start_us=start_us;g.dwell_ns=m.pixel_ns;g.px=px;g.ch=ch;g.row=row;
    g.start_s=static_cast<double>(start_us)*kRate/1.e6;
    g.pix_s=static_cast<double>(m.pixel_ns)*kRate/1.e9;
  };
  switch(m.family){
    case 0: {  // Robot: sync, porch, then Y; chroma follows a 4.5 ms separator and a 1.5 ms porch
      const uint32_t first=m.sync_us+m.porch_us;
      L.yuv=true;
      add(first,static_cast<uint16_t>(w),Chan::y);
      if(m.mode==Mode::robot36) {
        const uint32_t sep=first+chan_us;
        L.interlaced=true;
        L.sep_start_s=static_cast<double>(sep+500)*kRate/1.e6;      // middle of the 4.5 ms separator
        L.sep_end_s=static_cast<double>(sep+4000)*kRate/1.e6;
        add(sep+6000,static_cast<uint16_t>(w/2),Chan::chroma);
      } else {                                                       // Robot 72: Y, R-Y, B-Y on every line
        const uint32_t c1=first+chan_us+6000;
        add(c1,static_cast<uint16_t>(w/2),Chan::ry);
        add(c1+chan_us/2+6000,static_cast<uint16_t>(w/2),Chan::by);
      }
    } break;
    case 1: {  // Martin: sync, porch, then G, B, R each followed by a separator
      const uint32_t g=m.sync_us+m.porch_us,b=g+chan_us+m.sep_us,r=b+chan_us+m.sep_us;
      add(g,static_cast<uint16_t>(w),Chan::g);add(b,static_cast<uint16_t>(w),Chan::b);add(r,static_cast<uint16_t>(w),Chan::r);
    } break;
    case 2: {  // Scottie: separator, G, separator, B, then the sync pulse in mid-line, porch, R
      const uint32_t g=m.porch_us,b=g+chan_us+m.sep_us,r=b+chan_us+m.sync_us+m.porch_us;
      add(g,static_cast<uint16_t>(w),Chan::g);add(b,static_cast<uint16_t>(w),Chan::b);add(r,static_cast<uint16_t>(w),Chan::r);
    } break;
    default: { // PD: sync, porch, then Y (even row), R-Y, B-Y, Y (odd row); chroma is full width and shared
      L.yuv=true;
      const uint32_t first=m.sync_us+m.porch_us;
      add(first,static_cast<uint16_t>(w),Chan::y,0);
      add(first+chan_us,static_cast<uint16_t>(w),Chan::ry);
      add(first+2*chan_us,static_cast<uint16_t>(w),Chan::by);
      add(first+3*chan_us,static_cast<uint16_t>(w),Chan::y,1);
    } break;
  }
  return L;
}
// Chroma is noisier than luma (half or less of the bandwidth, and each picture element only sees a few audio
// samples), and on a weak signal it shows as coloured speckle on gray areas. A 1-2-1 smoothing across the line
// followed by a ramp around neutral that removes weak chroma and leaves real colours (large offsets) untouched removes most of it.
#ifdef ORCSDR_SSTV_TESTING
int g_chroma_taps=-1,g_chroma_core=-1;
void chroma_env(){
  if(g_chroma_taps>=0)return;
  const char* t=std::getenv("SSTV_CHROMA_TAPS");const char* c=std::getenv("SSTV_CHROMA_CORE");
  g_chroma_taps=t?std::atoi(t):kChromaTaps;g_chroma_core=c?std::atoi(c):kChromaCore;
}
#endif
void clean_chroma(const uint8_t* in,uint8_t* out,uint16_t n){
#ifdef ORCSDR_SSTV_TESTING
  chroma_env();const int taps=g_chroma_taps,core=g_chroma_core;
#else
  const int taps=kChromaTaps,core=kChromaCore;
#endif
  for(uint16_t i=0;i<n;++i) {
    int v=in[i];
    if(taps>=3) {
      const int a=in[i>0?i-1:i],b=in[i+1<n?i+1:i];
      v=(a+2*in[i]+b+2)/4;
    }
    if(core>0) {                       
      const int d=v-128,m=std::abs(d),top=2*core;     // zero below core, full strength from 2*core up
      if(m<top)v=128+(m<=core?0:d*(m-core)/(top-core));
    }
    out[i]=static_cast<uint8_t>(std::clamp(v,0,255));
  }
}
void paint_yuv(uint16_t row,const uint8_t* y,const uint8_t* cr,const uint8_t* cb,uint16_t cw){
  if(!g_live.pixels||row>=g_live.height||cw==0)return;
  auto* dst=const_cast<uint16_t*>(g_live.pixels)+static_cast<size_t>(row)*g_live.width;
  const uint32_t w=g_live.width;
  clean_chroma(cr,scan(6),cw);clean_chroma(cb,scan(7),cw);
  const uint8_t* scr=scan(6);const uint8_t* scb=scan(7);
  for(uint32_t x=0;x<w;++x) {
    const uint32_t cx=std::min<uint32_t>(x*cw/w,cw-1u);
    dst[x]=yuv565(y[x],scr[cx],scb[cx]);
  }
}
void paint_rgb(uint16_t row,const uint8_t* r,const uint8_t* g,const uint8_t* b){
  if(!g_live.pixels||row>=g_live.height)return;
  auto* dst=const_cast<uint16_t*>(g_live.pixels)+static_cast<size_t>(row)*g_live.width;
  for(uint16_t x=0;x<g_live.width;++x)dst[x]=rgb565(r[x],g[x],b[x]);
}
void clear_line_scans(){
  // Luma and RGB gaps stay black; chroma gaps stay neutral so a lost stretch fades to gray, not to colour.
  // Only the slots this mode scans into; Robot 36 keeps slot 3 (the previous line's luma) across lines.
  for(uint8_t i=0;i<g_layout.count;++i)std::memset(scan(i),0,kMaxWidth);
  if(g_layout.yuv)
    for(uint8_t i=0;i<g_layout.count;++i)
      if(g_layout.seg[i].ch==Chan::ry||g_layout.seg[i].ch==Chan::by||g_layout.seg[i].ch==Chan::chroma)
        std::memset(scan(i),128,kMaxWidth);
}
void finish_picture(bool incomplete,EndReason reason);
void begin_picture(const ModeSpec* spec,uint8_t vis,
                   Identification source=Identification::vis,
                   uint8_t confidence=100,bool unknown_start=false) {
  if(!spec) return;
  if(g_live.pixels && g_live.decoded_rows)
    finish_picture(true,EndReason::new_vis);
  frame_release(g_live.frame);
  g_live={};
  const size_t n=static_cast<size_t>(spec->width)*spec->height;
  Frame* frame=frame_create(n);
  // A valid header must not be lost to a full or fragmented PSRAM: give up the oldest gallery pictures one at a time
  // (the gallery is a convenience, the reception is the point) and try again. A picture the UI or archive still holds stays
  // alive until it lets go, so this only frees what nothing else needs.
  while(!frame && g_saved_count>0) {
    frame_release(g_saved[g_saved_count-1].frame);
    g_saved[g_saved_count-1]={};
    --g_saved_count;
    frame=frame_create(n);
  }
  if(!frame) {
    ++g_stats.frame_alloc_failures;
    std::snprintf(g_stats.status,sizeof(g_stats.status),"INSUFFICIENT PSRAM");
    reset_picture();
    return;
  }
  std::memset(frame_pixels(frame),0,n*sizeof(uint16_t));
  g_live.pixels=frame_pixels(frame);
  g_live.frame=frame;
  g_live.width=spec->width;
  g_live.height=spec->height;
  g_live.frequency_hz=g_frequency;
  g_live.mode=spec->mode;
  g_live.partial=true;
  g_live.start_unknown=unknown_start;
  g_live.identification=source;
  g_live.identification_score=confidence;
  g_session_id=g_next_session++;
  g_session_locked=g_session_missed=0;
  g_session_vis=source==Identification::vis?vis:0;
  g_live.session_id=g_session_id;
  g_live.vis_code=g_session_vis;
  g_spec=spec;
  g_layout=build_layout(*spec);
  std::memset(scan(3),0,kMaxWidth);std::memset(scan(4),128,kMaxWidth);std::memset(scan(5),128,kMaxWidth);
  clear_line_scans();
  g_line_index=0;
  // Scottie sends an additional 9 ms (1200 Hz) sync *after* VIS.
  // Its first RGB scan starts after this pre-sync, not at VIS stop;
  // see F4HWN test/modes.py: "pre=9" for Scottie S1/S2/DX.
  g_line_origin=static_cast<double>(g_ticks) +
       (spec->family==2 ? 9.0*kRate/1000.0 : 0.0);
  g_line_sync=false; g_sync_run=0;g_consecutive_missed=0;
  auto_detect::reset();
  clear_pixel_marks();
  g_stats.width=spec->width;g_stats.rows=spec->height;
  g_stats.row=0;g_stats.vis=vis;g_stats.received_mode=spec->mode;
  g_stats.receiving=true;
  g_stats.identification=source;
  g_stats.identification_score=confidence;
  g_stats.start_unknown=unknown_start;
  ++g_revision;
  std::snprintf(g_stats.status,sizeof(g_stats.status),"RECEIVING %s / COLOUR",spec->name);
}
void finish_picture(bool incomplete,EndReason reason) {
  g_stats.receiving=false;
  if(!g_live.pixels)return;
  if(g_live.decoded_rows==0) {
    frame_release(g_live.frame);g_live={};reset_picture();return;
  }
  g_live.serial=g_next_serial++;
  g_live.partial=incomplete||g_live.start_unknown;
  g_live.end_reason=reason;
  g_live.sync_locked=g_session_locked;
  g_live.sync_missed=g_session_missed;
  g_live.utc_epoch=g_utc_source?g_utc_source():0u;
  g_last_end_tick=g_ticks;g_have_end=true;
  frame_release(g_saved[kGallery-1].frame);
  for(size_t n=kGallery-1;n>0;--n)g_saved[n]=g_saved[n-1];
  g_saved[0]=g_live;
  g_saved_count=std::min(kGallery,g_saved_count+1);
  g_live={};
  if(g_saved[0].partial)g_stats.partial_frames++;
  else g_stats.frames++;
  reset_picture();
  ++g_revision;
  std::snprintf(g_stats.status,sizeof(g_stats.status),
    g_saved[0].partial?"PARTIAL IMAGE SAVED / AUTO SEARCH":"FRAME SAVED IN RAM / COLOUR");
}
void commit_line() {
  if(!g_spec||!g_live.pixels)return;
  if(!g_line_sync) {
    g_stats.sync_missed_lines++;
    if(g_session_missed<0xffffu)++g_session_missed;
    if(++g_consecutive_missed>=kMaxConsecutiveMissed) {
      // The lines committed since the last sync pulse were only predictions; the signal is gone, so they are not kept.
      const uint16_t per_line=g_spec->family==3?2:1;
      const uint16_t tail=static_cast<uint16_t>((g_consecutive_missed-1)*per_line);
      g_live.decoded_rows=g_live.decoded_rows>tail?static_cast<uint16_t>(g_live.decoded_rows-tail):0;   // 0 rows: discarded
      finish_picture(true,EndReason::sync_lost);
      return;
    }
  } else g_consecutive_missed=0;
  const int count=g_spec->family==3?2:1;
  const uint16_t row=static_cast<uint16_t>(g_line_index*count);
  const uint16_t w=g_live.width;
  switch(g_spec->family) {
    case 0:
      if(g_layout.interlaced) {
        // Robot 36 sends R-Y on one line and B-Y on the next; each pair of lines shares both. The separator
        // tone says which this line carries (about 1500 Hz R-Y, 2300 Hz B-Y); line parity is the fallback.
        const bool is_ry=g_sep_n?(g_sep_sum/g_sep_n<1900.f):(g_line_index%2==0);
        std::memcpy(scan(is_ry?4:5),scan(1),kMaxWidth);
        paint_yuv(row,scan(0),scan(4),scan(5),w/2);
        // A B-Y line completes the pair: repaint the R-Y line above it with the matching chroma.
        if(!is_ry&&g_line_index>0)paint_yuv(static_cast<uint16_t>(row-1),scan(3),scan(4),scan(5),w/2);
        std::memcpy(scan(3),scan(0),kMaxWidth);
      } else paint_yuv(row,scan(0),scan(1),scan(2),w/2);
      break;
    case 3:
      paint_yuv(row,scan(0),scan(1),scan(2),w);
      paint_yuv(static_cast<uint16_t>(row+1),scan(3),scan(1),scan(2),w);
      break;
    default:  // Martin and Scottie send G, B, R
      paint_rgb(row,scan(2),scan(0),scan(1));
      break;
  }
  g_stats.row=std::min<uint16_t>(g_live.height,static_cast<uint16_t>(row+count));
  g_live.decoded_rows=g_stats.row;
  ++g_revision; // UI can paint newly committed rows without any frame wait
  ++g_line_index;
  g_line_origin+=static_cast<double>(g_spec->line_us)*kRate/1000000.;
  g_line_sync=false;
  clear_pixel_marks();
  clear_line_scans();
  if(g_stats.row>=g_live.height) {
    if(g_consecutive_missed>0) {
      // The picture ran out while the last lines were only predicted (no sync pulse): those rows are not received data,
      // so it is an interrupted reception, not a complete one.
      const uint16_t per_line=g_spec->family==3?2:1;
      const uint16_t tail=static_cast<uint16_t>(g_consecutive_missed*per_line);
      g_live.decoded_rows=g_live.decoded_rows>tail?static_cast<uint16_t>(g_live.decoded_rows-tail):0;
      finish_picture(true,EndReason::sync_lost);
    } else {
      finish_picture(g_live.start_unknown,EndReason::complete);
    }
  }
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
  // The tone estimator is slow to enter the 1200 Hz window (about 3 ms), so a short pulse measures much shorter
  // than it is: Martin's 4.9 ms sync reads about 34 samples against a nominal 58. The floor is relaxed for short
  // pulses only; the predicted-position check below is what rejects stray 1200 Hz noise.
  const double shortest=std::max(need*.25,need*.55-20.);
  if(sync_len<shortest || sync_len>need*1.55)return;
  const double edge=static_cast<double>(g_ticks-sync_len)-sync_latency(*g_spec);
  const double expected=g_line_origin+sync_offset_samples(*g_spec);
  const double error=edge-expected;
  // Avoid locking onto arbitrary 1200-Hz noise far from the expected line.
  const double tolerance=std::max(120.,need*1.2); // 10 ms minimum
  if(std::fabs(error)>tolerance)return;
  g_line_sync=true;
  g_stats.sync_locked_lines++;
  if(g_session_locked<0xffffu)++g_session_locked;
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
  const uint8_t value=tone_luma(hz);
  for(uint8_t i=0;i<g_layout.count;++i) {
    const Seg& sg=g_layout.seg[i];
    const double x=(shifted-sg.start_s)/sg.pix_s;
    if(x<0 || x>=sg.px)continue;
    const int pixel=static_cast<int>(x);
    uint8_t* row=scan(i);
    if(pixel!=g_last_pixel[i]) {
      row[pixel]=value;
      g_last_pixel[i]=pixel;
    } else {
      // A two-tap moving mean is sufficient for 2-6 audio samples / pixel.
      row[pixel]=static_cast<uint8_t>((row[pixel]+value+1)/2);
    }
  }
  if(g_layout.interlaced&&shifted>=g_layout.sep_start_s&&shifted<g_layout.sep_end_s) {
    g_sep_sum+=hz;
    if(g_sep_n<0xffffu)++g_sep_n;
  }
}
void vis_tick(float f) {
  const bool lead=f>=1740.f&&f<=2070.f;
  const bool sync=f>=1030.f&&f<=1400.f;
  if(g_vis_stage==VisStage::bits) {
    const uint32_t age=static_cast<uint32_t>(g_ticks-g_vis_start);
    // Majority vote over the middle 20ms of each 30ms VIS symbol. Unlike
    // the former single-point sampler, a single tone glitch cannot flip
    // a bit. No dynamic memory and only small integer vote counters.
    if(age>=480u) {
      const uint32_t v=age-480u;
      const unsigned field=v/360u;
      if(field<9u && v%360u<240u) {
        if(field==8u) {
          ++g_vis_votes[field];
          if(f>=1030.f&&f<=1400.f)++g_vis_ones[field];
        } else if(f>=1000.f&&f<=1440.f) {
          ++g_vis_votes[field];
          if(f<1200.f)++g_vis_ones[field];
        }
      }
    }
    if(age>=3600u) {
      bool valid=true;
      uint8_t code=0,parity=0;
      for(unsigned i=0;i<8;++i) {
        const uint16_t n=g_vis_votes[i],ones=g_vis_ones[i];
        if(n<70u || (ones*100u>35u*n && ones*100u<65u*n)) {
          valid=false;break;
        }
        const bool bit=ones*2u>=n;
        if(i<7u)code|=static_cast<uint8_t>(bit)<<i;
        parity+=bit?1u:0u;
      }
      const uint16_t stop_n=g_vis_votes[8];
      valid=valid && stop_n>=70u && g_vis_ones[8]*10u>=stop_n*7u
            && (parity&1u)==0u;
      reset_vis();
      // Flat gray stretches of a picture look like a VIS leader, and in a three-channel mode a header-shaped
      // run of pixel data can even pass parity. While every line is still syncing, the picture is intact and
      // cannot be interrupted; a genuinely new transmission breaks the line syncs before its header completes.
      if(valid&&g_stats.receiving&&g_consecutive_missed==0)valid=false;
      if(valid) {
        g_stats.good_vis++;
        const ModeSpec* spec=vis_spec(code);
        if(spec && (g_requested==Mode::auto_vis || g_requested==spec->mode))
          begin_picture(spec,code,Identification::vis,100,false);
        else if(spec)
          std::snprintf(g_stats.status,sizeof(g_stats.status),
                        "VIS / MANUAL MODE MISMATCH");
        else
          std::snprintf(g_stats.status,sizeof(g_stats.status),"UNKNOWN VIS %u",code);
      } else {
        // Gray stretches inside a picture (neutral chroma is 1900 Hz, like a VIS leader) start header
        // attempts that always fail; counting them would show a healthy reception as 31 bad headers.
        if(!g_stats.receiving)g_stats.bad_vis++;
        if(!g_stats.receiving)
          std::snprintf(g_stats.status,sizeof(g_stats.status),
                        "VIS PARITY/SYNC ERROR");
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
      if(++g_phase_age>2400u) {reset_vis();break;}
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
          g_vis_field=g_vis_byte=g_vis_parity=0;
          g_vis_stage=VisStage::bits;
          std::snprintf(g_stats.status,sizeof(g_stats.status),"VIS HEADER");
        }
      } else if(++g_phase_age>60u)reset_vis();
      break;
    case VisStage::bits:break;
  }
}
// Time the producer could not deliver (ring overrun) still elapsed on the air.
// Advance the sample clock by the dropped amount so the raster timeline and the
// line-sync window stay true, and restart anything that spans the gap.
void skip_gap(uint32_t count) {
  g_ticks+=count;
  ++g_stats.audio_gaps;
  g_stats.audio_gap_samples+=count;
  g_have_edge=false;
  g_sync_run=0;
  reset_vis();
}
// The frequency estimator settles geometrically after the transition into a sync pulse, in steps every few samples, and
// swings back as the pulse ends. Take the plateaus before that final swing and extrapolate their geometric decay
// (Aitken) to the tone the pulse really carries. False if the pulse gave too little to measure.
bool sync_tone_estimate(float& tone) {
  if(g_plat_n==0)return false;
  uint32_t m=g_plat_n-1;
  while(m>0&&g_plat[m]>g_plat[m-1]+0.5f)--m;   // drop the closing swing upward
  const float x2=g_plat[m];
  if(m==0){
    if(g_plat_n>1)return false;                // only a rising series: the first sample after the crossing, not a tone
    tone=x2;return true;                       // steady from the start: already the tone
  }
  if(m==1){
    if(std::fabs(x2-g_plat[0])>3.f)return false;
    tone=x2;return true;
  }
  const float d0=g_plat[m-1]-g_plat[m-2],d1=x2-g_plat[m-1];
  if(std::fabs(d1)<=3.f){tone=x2;return true;}
  const float ratio=d0!=0.f?d1/d0:0.f;
  if(ratio<=0.2f||ratio>=0.95f)return false;   // not a clean geometric settle
  tone=x2+d1*ratio/(1.f-ratio);
  return tone>900.f&&tone<1500.f;
}

// Adaptive noise band-limit for weak signals. SSTV tones sit between 1100 and 2300 Hz, so a clean demodulated signal has almost no
// energy above 4.6 kHz (pixel detail reaches at most about 4.4 kHz) while FM noise has plenty (about 1.5% of the power at
// -113 dBm, under 0.02% on a strong signal). The
// front end measures that ratio and fades from the original wide path (clean signal: unchanged, fine pixel detail intact) to a
// second-order 1000-2600 Hz band-pass as noise grows. Evaluated on 11 recordings of Robot 36 at -113 dBm: row correlation with
// the test pattern 0.28 -> 0.44. Define SSTV_NO_ADAPTIVE_BP to switch it off for A/B tests.
#ifndef SSTV_NO_ADAPTIVE_BP
#define SSTV_ADAPTIVE_BP 1
#endif
#ifdef SSTV_ADAPTIVE_BP
struct Biquad {
  float b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
  float run(float x) {
    const float y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2;
    x2=x1;x1=x;y2=y1;y1=y;
    return y;
  }
};
#ifndef SSTV_BP_HP_HZ
#define SSTV_BP_HP_HZ 1000.f
#endif
#ifndef SSTV_BP_LP_HZ
#define SSTV_BP_LP_HZ 2600.f
#endif
#ifndef SSTV_BP_Q
#define SSTV_BP_Q .70710678f
#endif
#ifndef SSTV_NOISE_CUT_HZ
#define SSTV_NOISE_CUT_HZ 4600.f
#endif
Biquad g_bq_hp,g_bq_lp,g_bq_noise;
float g_e_hi=0,g_e_all=0,g_blend=0;
uint32_t g_blend_tick=0;
// Audio EQ cookbook (Butterworth when Q is 1/sqrt 2) at the 12 kHz decoder rate.
Biquad make_biquad(bool high_pass,float corner_hz,float q) {
  const float w=2.f*3.14159265358979323846f*corner_hz/kRate,c=std::cos(w),alpha=std::sin(w)/(2.f*q);
  const float a0=1.f+alpha;
  Biquad bq{};
  const float edge=high_pass?(1.f+c)/2.f:(1.f-c)/2.f;
  bq.b0=edge/a0;bq.b1=(high_pass?-2.f*edge:2.f*edge)/a0;bq.b2=edge/a0;
  bq.a1=-2.f*c/a0;bq.a2=(1.f-alpha)/a0;
  return bq;
}
// One sample through the adaptive stage: the band-limited signal is blended in as the out-of-band noise fraction rises.
float adaptive_front(float raw) {
  const float band=g_bq_lp.run(g_bq_hp.run(raw));
  const float hi=g_bq_noise.run(raw);
  g_e_hi+=(hi*hi-g_e_hi)*(1.f/800.f);
  g_e_all+=(raw*raw-g_e_all)*(1.f/800.f);
  if((++g_blend_tick&15u)==0) {
    float target=0.f;
    if(g_e_all>1.0e4f) {   // ignore near-silence
      const float ratio=g_e_hi/g_e_all;
      // 0 at a 0.3% ratio, 1 at 1% (noise measured at about 1.5%); the picture's own pixel detail stays below that.
      target=std::clamp((std::log10(std::max(ratio,1e-6f))+2.52f)*1.92f,0.f,1.f);
    }
    g_blend+=0.05f*(target-g_blend);   // about 15 ms per step of 16 samples: smooth, not jumpy
  }
  return raw+g_blend*(band-raw);
}
#endif
void accept_sample(int16_t pcm) {
  ++g_ticks;
  // One pole high-pass (~120 Hz) followed by LP (~2.6 kHz).  Damping is
  // deliberately mild; preserves 1100-2300-Hz SSTV tone transitions.
#ifdef SSTV_ADAPTIVE_BP
  const float x=adaptive_front(static_cast<float>(pcm));
#else
  const float x=static_cast<float>(pcm);
#endif
  g_hp=.94f*(g_hp+x-g_in_prev);g_in_prev=x;
  g_lp+=.78f*(g_hp-g_lp);
  g_level=std::max(std::fabs(g_lp)/32768.f,g_level*.9995f);
  // Fast envelope (sub-ms attack, ~3 ms release at 12 kHz). When the audio
  // really goes quiet the tone estimate must fall to zero; otherwise the last
  // tone (for example a 1200 Hz sync) would be held through silence, stretching
  // pulse widths and letting a stale 1900 Hz leader keep VIS timing alive.
  {
    const float a=std::fabs(g_lp)/32768.f;
    g_env+=(a>g_env?.5f:.15f)*(a-g_env);
    if(g_env<.002f){g_freq=0.f;g_period=0.f;g_have_edge=false;}
  }
  if(g_prev<=0.f&&g_lp>0.f&&g_level>.005f) {
    const float frac= -g_prev/(g_lp-g_prev);
    // Use an integer sample clock + fractional crossing; floating-point
    // absolute timestamps lose sub-sample precision after ~23 minutes.
    const uint32_t tick=static_cast<uint32_t>(g_ticks-1u);
    const float period=static_cast<float>(tick-g_last_edge_tick)
                       +frac-g_last_edge_frac;
    if(g_have_edge&&period>4.4f&&period<13.f) {
      if(g_period==0.f)g_period=period;
      else g_period+=.25f*(period-g_period);
      g_freq=kRate/g_period;
    }
    g_last_edge_tick=tick;
    g_last_edge_frac=frac;
    g_have_edge=true;
  }

  // Normalized adjacent-sample autocorrelation: for a sinusoid at f Hz,
  // P/Q = 2*cos(2*pi*f/kRate). This provides a new tone estimate on every
  // 12k sample instead of waiting ~5-10 samples for each zero crossing.
  // F4HWN's open-source SSTV receiver uses the same DSP identity; the
  // coefficient is tuned here against Pluto-received Robot36 recordings.
  const float acp=g_prev*(g_lp+g_ac_y2);
  const float acq=g_prev*g_prev;
  g_ac_p+=.6f*(acp-g_ac_p);
  g_ac_q+=.6f*(acq-g_ac_q);
  g_ac_y2=g_prev;
  if(g_ac_q>500.f && g_level>.005f) {
    const float ratio=std::clamp(.5f*g_ac_p/g_ac_q,0.f,1.f)*512.f;
    const unsigned index=std::min(511u,static_cast<unsigned>(ratio));
    const float fract=ratio-index;
    g_luma_freq=g_ac_freq_lut[index]+
      (g_ac_freq_lut[index+1]-g_ac_freq_lut[index])*fract;
  } else g_luma_freq=g_freq;
  g_prev=g_lp;
  // Observe sync pulses even while idle. A missing VIS can be recovered
  // from a coherent pulse train without instantiating parallel decoders.
  const bool is_sync=g_freq>1030.f&&g_freq<1400.f;
  if(is_sync) {
    if(g_sync_run<60000u) {
      if(g_sync_run==0)g_plat_n=0;
      if(g_plat_n==0||std::fabs(g_freq-g_plat[g_plat_n-1])>0.5f) {
        if(g_plat_n<kSyncPlateaus)g_plat[g_plat_n++]=g_freq;
      }
      ++g_sync_run;
    }
  } else if(g_sync_run) {
    const uint32_t n=g_sync_run;
    float tone=0;
    if(n>=kMinToneSamples&&sync_tone_estimate(tone)) {   // a noise blip in the band is not a sync pulse to measure
      g_stats.sync_tone_hz=g_sync_pulses?0.8f*g_stats.sync_tone_hz+0.2f*tone:tone;
      if(g_sync_pulses<60000u)++g_sync_pulses;
      g_stats.sync_tone_pulses=static_cast<uint16_t>(g_sync_pulses);
      g_sync_tone_tick=g_ticks;
    }
    g_sync_run=0;
    const uint64_t sync_start=g_ticks-n;
    ++g_stats.observed_sync_pulses;
    g_stats.sync_pulse_ms=static_cast<float>(n)*1000.f/kRate;
    if(g_have_sync_start)
      g_stats.sync_interval_ms=static_cast<float>(sync_start-g_last_sync_start)*1000.f/kRate;
    g_last_sync_start=sync_start;g_have_sync_start=true;
    if(g_stats.receiving)sync_edge(n);
    else if(g_auto_strategy==AutoStrategy::vis_sync &&
            g_requested==Mode::auto_vis &&
            g_vis_stage==VisStage::lead1 && n<=65535u) {
      // The classifier only subtracts pulse times, so the low 32 bits suffice.
      const auto candidate=auto_detect::observe(
        static_cast<uint32_t>(sync_start),static_cast<uint16_t>(n));
      if(candidate.locked) {
        const ModeSpec* m=mode_spec(candidate.mode);
        if(m) {
          begin_picture(m,0,Identification::sync,candidate.score,true);
          // Fallback starts on the NEXT complete line. Scottie's 9ms
          // sync is in the middle of its RGB scan; use its offset.
          g_line_origin=static_cast<double>(sync_start)-sync_latency(*m)+
             static_cast<double>(m->line_us)*kRate/1000000.-
             sync_offset_samples(*m);
          ++g_stats.auto_sync_locks;
          std::snprintf(g_stats.status,sizeof(g_stats.status),
                        "SYNC LIKELY %s / PARTIAL",m->name);
        }
      }
    }
  }
  // Keep looking for a *new* VIS even when a previous image is active.
  // A validated new header archives interrupted rows as a partial image.
  vis_tick(g_freq);
  if(g_stats.receiving)sample_luma(g_luma_freq);
}
uint64_t now_us() {
#ifdef ESP_PLATFORM
  return static_cast<uint64_t>(esp_timer_get_time());
#else
  using namespace std::chrono;
  return static_cast<uint64_t>(duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
#endif
}
} // namespace
void start(uint32_t freq) {
  g_on.store(false,std::memory_order_release);
  if(g_stats.receiving)finish_picture(true,EndReason::aborted);
  reset_vis();reset_picture();auto_detect::reset();
  g_frequency=freq;g_stats.active=true;
  g_hp=g_lp=g_in_prev=g_prev=g_period=g_freq=g_level=g_env=0.f;
  g_last_edge_tick=0;g_last_edge_frac=0;g_have_edge=false;
  g_decim_sum=0;g_decim_n=0;
  g_gap_tail.store(g_gap_head.load(std::memory_order_acquire),std::memory_order_release);
  g_pending_gap=0;g_have_sync_start=false;g_have_end=false;g_last_service_us=0;
  // Performance counters describe the current listening session.
  g_stats.service_us_last=g_stats.service_us_max=g_stats.service_us_avg=0;
  g_stats.service_cpu_pct=0;g_stats.ring_fill_max=0;
  g_ac_y2=g_ac_p=g_ac_q=g_luma_freq=0.f;
  // Build a 1 KiB inverse-cosine LUT once, outside the real-time audio
  // service.  The sample loop only interpolates adjacent integer entries.
  if(!g_ac_lut_ready) {
    for(int i=0;i<=512;++i)
      g_ac_freq_lut[i]=static_cast<uint16_t>(
        std::acos(static_cast<float>(i)/512.f)*
        (kRate/(2.f*3.14159265358979323846f))+.5f);
    g_ac_lut_ready=true;
  }
  g_ticks=0;
#ifdef SSTV_ADAPTIVE_BP
  g_bq_hp=make_biquad(true,SSTV_BP_HP_HZ,SSTV_BP_Q);
  g_bq_lp=make_biquad(false,SSTV_BP_LP_HZ,SSTV_BP_Q);
  g_bq_noise=make_biquad(true,SSTV_NOISE_CUT_HZ,SSTV_BP_Q);
  g_e_hi=g_e_all=g_blend=0.f;g_blend_tick=0;
#endif
  if(!g_queue) {
#ifdef ESP_PLATFORM
    g_queue=static_cast<int16_t*>(heap_caps_calloc(kRingSize,sizeof(int16_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
#else
    g_queue=static_cast<int16_t*>(std::calloc(kRingSize,sizeof(int16_t)));
#endif
    if(!g_queue) {
      std::snprintf(g_stats.status,sizeof(g_stats.status),"INSUFFICIENT PSRAM");
      g_stats.active=false;
      return;
    }
  }
  if(!g_work) {
#ifdef ESP_PLATFORM
    g_work=static_cast<uint8_t*>(heap_caps_calloc(8,kMaxWidth,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
#else
    g_work=static_cast<uint8_t*>(std::calloc(8,kMaxWidth));
#endif
    if(!g_work) {
      std::snprintf(g_stats.status,sizeof(g_stats.status),"INSUFFICIENT PSRAM");
      g_stats.active=false;
      return;
    }
  }
  g_read.store(g_write.load(std::memory_order_acquire),std::memory_order_release);
  std::snprintf(g_stats.status,sizeof(g_stats.status),"WAITING FOR VIS");
  g_on.store(true,std::memory_order_release);
}
void stop(){
  g_on.store(false,std::memory_order_release);
  if(g_stats.receiving)finish_picture(true,EndReason::aborted);
  reset_vis();reset_picture();auto_detect::reset();g_stats.active=false;
  g_read.store(g_write.load(std::memory_order_acquire),std::memory_order_release);
  g_gap_tail.store(g_gap_head.load(std::memory_order_acquire),std::memory_order_release);
  g_pending_gap=0;
}
bool active(){return g_on.load(std::memory_order_acquire);}
void set_mode(Mode m){g_requested=mode_spec(m)?m:Mode::auto_vis;}
Mode selected_mode(){return g_requested;}
void set_auto_strategy(AutoStrategy strategy){
  g_auto_strategy=strategy;
  auto_detect::reset();
}
AutoStrategy auto_strategy(){return g_auto_strategy;}
void abort_image(){
  if(g_stats.receiving)finish_picture(true,EndReason::aborted);
  reset_vis();reset_picture();auto_detect::reset();
}
#ifdef ORCSDR_SSTV_TESTING
void testing_fail_next_frames(unsigned count){g_test_fail_frames=count;}
#endif
void set_frequency(uint32_t hz){
  if(g_stats.receiving)finish_picture(true,EndReason::retune);
  g_frequency=hz;reset_picture();reset_vis();auto_detect::reset();
  g_sync_pulses=0;g_stats.sync_tone_hz=0;g_stats.sync_tone_pulses=0;   // a new frequency starts a new tone reading
}
std::atomic<bool> g_live_muted{false};
void offer_core(const int16_t* src,size_t n);
void set_live_audio_muted(bool muted){g_live_muted.store(muted,std::memory_order_relaxed);}
void set_audio_observer(void (*observer)(const int16_t*,size_t)){g_observer=observer;}
void offer_audio_48k(const int16_t* src,size_t n){
  if(g_live_muted.load(std::memory_order_relaxed))return;
  offer_core(src,n);
}
void offer_injected_48k(const int16_t* src,size_t n){offer_core(src,n);}
void offer_core(const int16_t* src,size_t n){
  if(!active()||!src)return;
  g_offered.fetch_add(static_cast<uint32_t>(n),std::memory_order_relaxed);
  uint32_t w=g_write.load(std::memory_order_relaxed);
  for(size_t i=0;i<n;++i){
    g_decim_sum+=src[i];
    if(++g_decim_n<4)continue;
    g_decim_n=0;
    const int16_t sample=static_cast<int16_t>(g_decim_sum/4);
    g_decim_sum=0;
    if(w-g_read.load(std::memory_order_acquire)>=kRingSize){
      g_drops.fetch_add(1,std::memory_order_relaxed);
      if(g_pending_gap<0xffffffffu)++g_pending_gap;
      continue;
    }
    if(g_pending_gap) {
      // First stored sample after a drop burst: publish where the gap fell.
      const uint32_t head=g_gap_head.load(std::memory_order_relaxed);
      const uint32_t tail=g_gap_tail.load(std::memory_order_acquire);
      if(head-tail<kGapMarks) {
        g_gap_index[head%kGapMarks]=w;
        g_gap_count[head%kGapMarks]=g_pending_gap;
        g_gap_head.store(head+1,std::memory_order_release);
        g_pending_gap=0;
      }
      // Marker queue full: keep accumulating. A published marker belongs to the consumer, which reads its count
      // once; the lost samples are inserted at the next free marker, a little later than they occurred, and the
      // line-sync PLL absorbs that bounded error.
    }
    g_queue[w&kMask]=sample;++w;
  }
  g_write.store(w,std::memory_order_release);
}
SessionState session_state(){
  if(!active())return SessionState::searching;
  if(g_stats.receiving) {
    if(g_consecutive_missed>0)return SessionState::loss_recovery;
    // A sync-only lock stays provisional until further lines confirm the line timing.
    if(g_stats.identification==Identification::sync && g_session_locked<8)
      return SessionState::provisional_lock;
    return SessionState::receiving;
  }
  if(g_have_end) {
    const uint64_t since=g_ticks-g_last_end_tick;
    if(since<kRate/2)return SessionState::finalizing;
    if(since<3*kRate)return SessionState::ready;
  }
  // Only a full 200 ms leader advances the VIS stage; a short gray image tone does not.
  if(g_vis_stage!=VisStage::lead1)return SessionState::vis_acquisition;
  if(auto_detect::pending_pulses()>=2)return SessionState::sync_analysis;
  return SessionState::searching;
}
const char* state_name(SessionState state){
  switch(state){
    case SessionState::vis_acquisition:return "VIS ACQUISITION";
    case SessionState::sync_analysis:return "SYNC ANALYSIS";
    case SessionState::provisional_lock:return "PROVISIONAL LOCK";
    case SessionState::receiving:return "RECEIVING";
    case SessionState::loss_recovery:return "LOSS RECOVERY";
    case SessionState::finalizing:return "FINALIZING";
    case SessionState::ready:return "READY";
    default:return "SEARCHING";
  }
}
void service(size_t budget){
  if(!active())return;
  const uint64_t started=now_us();
  uint32_t r=g_read.load(std::memory_order_relaxed);
  const uint32_t end=g_write.load(std::memory_order_acquire);
  {
    const uint32_t fill=end-r;
    if(fill>g_stats.ring_fill_max)g_stats.ring_fill_max=static_cast<uint16_t>(fill>0xffffu?0xffffu:fill);
  }
  size_t n=0;
  auto* const observer=g_observer;
  int16_t batch[128];size_t batch_n=0;
  uint32_t tail=g_gap_tail.load(std::memory_order_relaxed);
  const uint32_t head=g_gap_head.load(std::memory_order_acquire);
  while(r!=end&&n<budget){
    while(tail!=head&&g_gap_index[tail%kGapMarks]==r) {
      skip_gap(g_gap_count[tail%kGapMarks]);
      ++tail;
    }
    const int16_t sample=g_queue[r&kMask];
    accept_sample(sample);++r;++n;
    if(observer) {
      batch[batch_n++]=sample;
      if(batch_n==sizeof(batch)/sizeof(batch[0])) { observer(batch,batch_n); batch_n=0; }
    }
  }
  if(observer&&batch_n)observer(batch,batch_n);
  g_gap_tail.store(tail,std::memory_order_release);
  g_read.store(r,std::memory_order_release);
  if(g_sync_pulses&&g_ticks-g_sync_tone_tick>5ull*kRate) {   // no sync pulse for 5 s: the reading is stale
    g_sync_pulses=0;g_stats.sync_tone_hz=0;g_stats.sync_tone_pulses=0;
  }
  g_stats.samples+=static_cast<uint32_t>(n);
  g_stats.tone_hz=g_freq;g_stats.audio_peak=g_level;
  g_stats.signal=g_level>.01f;
  g_stats.audio_overruns=g_drops.load(std::memory_order_relaxed);
  g_stats.audio_offered=g_offered.load(std::memory_order_relaxed);
  g_stats.vis_phase=static_cast<uint8_t>(g_vis_stage);
  g_stats.vis_phase_samples=g_phase_samples;
  g_stats.state=session_state();
  g_stats.session_sync_locked=g_session_locked;
  g_stats.session_sync_missed=g_session_missed;
  g_stats.session_id=g_stats.receiving?g_session_id:0;
  // Cost of this call, and its share of the time since the previous call.
  const uint64_t finished=now_us();
  const uint32_t us=static_cast<uint32_t>(finished-started);
  g_stats.service_us_last=us;
  if(us>g_stats.service_us_max)g_stats.service_us_max=us;
  g_stats.service_us_avg+=(static_cast<int32_t>(us)-static_cast<int32_t>(g_stats.service_us_avg))/8;
  if(g_last_service_us&&started>g_last_service_us) {
    const uint64_t window=started-g_last_service_us;
    const uint32_t pct=static_cast<uint32_t>(std::min<uint64_t>(100,(static_cast<uint64_t>(us)*100)/window));
    g_stats.service_cpu_pct=static_cast<uint8_t>(g_stats.service_cpu_pct+
        (static_cast<int32_t>(pct)-static_cast<int32_t>(g_stats.service_cpu_pct))/8);
  }
  g_last_service_us=started;
}
void end_of_stream(){
  if(!active())return;
  service(~static_cast<size_t>(0)>>1); // drain whatever is still queued
  if(g_stats.receiving&&g_spec&&g_live.pixels) {
    const auto& m=*g_spec;
    (void)m;
    double lum_end=0;
    for(uint8_t i=0;i<g_layout.count;++i)
      lum_end=std::max(lum_end,g_layout.seg[i].start_s+g_layout.seg[i].pix_s*g_layout.seg[i].px);
    const double shifted=static_cast<double>(g_ticks)-g_line_origin;
    // Commit the final line only if its pixel data has fully arrived.
    if(shifted>=lum_end)commit_line();
  }
  if(g_stats.receiving)finish_picture(true,EndReason::aborted);
}
Metrics metrics(){
  Metrics m=g_stats;
  m.state=session_state();
  m.frames_alive=g_frames_alive.load(std::memory_order_relaxed);
  m.frame_bytes=g_frame_bytes.load(std::memory_order_relaxed);
  return m;
}
Image live_image(){return g_live;}
Image archived_image(size_t i){return i<g_saved_count?g_saved[i]:Image{};}
size_t archive_count(){return g_saved_count;}
void clear_gallery(){
  for(auto& a:g_saved){frame_release(a.frame);a={};}
  g_saved_count=0;
}
Image pin_archived(size_t i){
  Image image=archived_image(i);
  frame_retain(image.frame);
  return image;
}
void release_image(const Image& image){frame_release(image.frame);}
uint32_t image_revision(){return g_revision;}
void set_utc_source(uint32_t (*utc_epoch)()){g_utc_source=utc_epoch;}
Image snapshot_live(){
  if(!g_live.pixels||g_live.decoded_rows==0)return Image{};
  const size_t n=static_cast<size_t>(g_live.width)*g_live.height;
  Frame* copy=frame_create(n);
  if(!copy)return Image{};
  std::memcpy(frame_pixels(copy),g_live.pixels,n*sizeof(uint16_t));
  Image image=g_live;
  image.pixels=frame_pixels(copy);
  image.frame=copy;
  image.partial=true;
  image.end_reason=EndReason::captured;
  image.serial=0;
  image.sync_locked=g_session_locked;
  image.sync_missed=g_session_missed;
  image.utc_epoch=g_utc_source?g_utc_source():0u;
  return image;
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
#ifdef ORCSDR_SSTV_TESTING
namespace testing {
void set_tick_base(uint64_t ticks){g_ticks=static_cast<decltype(g_ticks)>(ticks);}
} // namespace testing
#endif
} // namespace orcsdr::sstv
