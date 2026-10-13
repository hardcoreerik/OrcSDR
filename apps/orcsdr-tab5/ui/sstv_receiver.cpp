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
constexpr size_t kGallery=4;
std::atomic<uint32_t> g_read{0},g_write{0},g_drops{0};
// Drop bursts are recorded in-band so the decoder can keep its sample clock
// true: a marker says "N samples were lost immediately before ring slot I".
// Producer writes head, consumer writes tail (single producer, single consumer).
constexpr uint32_t kGapMarks=8;
uint32_t g_gap_index[kGapMarks]{};
uint32_t g_gap_count[kGapMarks]{};
std::atomic<uint32_t> g_gap_head{0},g_gap_tail{0};
uint32_t g_pending_gap=0;   // producer-only: dropped since the last stored sample
std::atomic<bool> g_on{false};
int16_t g_queue[kRingSize]{};
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
// 1.6 KiB workspace for PD pairs; for other families, scan[0] is luminance.
uint8_t g_scan[2][kMaxWidth]{};
int32_t g_last_pixel[2]={-1,-1};
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
enum class VisStage:uint8_t { lead1, break1200, lead2, start1200, bits };
VisStage g_vis_stage=VisStage::lead1;
uint32_t g_phase_samples=0,g_phase_age=0;
uint64_t g_vis_start=0;
uint8_t g_vis_byte=0,g_vis_parity=0,g_vis_field=0;
bool g_vis_stop_good=false;
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
Frame* frame_create(size_t pixel_count) {
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
void reset_picture() {
  g_spec=nullptr;
  g_stats.receiving=false;
  g_stats.row=g_stats.rows=g_stats.width=0;
  g_line_index=0;
  g_sync_run=0;
  g_line_sync=false;
  g_consecutive_missed=0;
  g_last_pixel[0]=g_last_pixel[1]=-1;
}
uint16_t gray(uint8_t v) {
  return static_cast<uint16_t>(((v&248)<<8)|((v&252)<<3)|(v>>3));
}
uint8_t tone_luma(float hz) {
  return static_cast<uint8_t>(std::clamp(static_cast<int>((hz-1500.f)*255.f/800.f+.5f),0,255));
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
  if(!frame) {
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
  g_line_index=0;
  // Scottie sends an additional 9 ms (1200 Hz) sync *after* VIS.
  // Its first RGB scan starts after this pre-sync, not at VIS stop;
  // see F4HWN test/modes.py: "pre=9" for Scottie S1/S2/DX.
  g_line_origin=static_cast<double>(g_ticks) +
       (spec->family==2 ? 9.0*kRate/1000.0 : 0.0);
  g_line_sync=false; g_sync_run=0;g_consecutive_missed=0;
  auto_detect::reset();
  g_last_pixel[0]=g_last_pixel[1]=-1;
  g_stats.width=spec->width;g_stats.rows=spec->height;
  g_stats.row=0;g_stats.vis=vis;g_stats.received_mode=spec->mode;
  g_stats.receiving=true;
  g_stats.identification=source;
  g_stats.identification_score=confidence;
  g_stats.start_unknown=unknown_start;
  ++g_revision;
  std::snprintf(g_stats.status,sizeof(g_stats.status),"RECEIVING %s / MONO",spec->name);
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
    g_saved[0].partial?"PARTIAL IMAGE SAVED / AUTO SEARCH":"FRAME SAVED IN RAM / MONO");
}
void commit_line() {
  if(!g_spec||!g_live.pixels)return;
  if(!g_line_sync) {
    g_stats.sync_missed_lines++;
    if(g_session_missed<0xffffu)++g_session_missed;
    if(++g_consecutive_missed>=4) {
      finish_picture(true,EndReason::sync_lost);
      return;
    }
  } else g_consecutive_missed=0;
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
  if(g_stats.row>=g_live.height)
    finish_picture(g_live.start_unknown||g_consecutive_missed>0,
                   EndReason::complete);
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
        g_stats.bad_vis++;
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
          g_vis_field=g_vis_byte=g_vis_parity=0;g_vis_stop_good=false;
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
void accept_sample(int16_t pcm) {
  ++g_ticks;
  // One pole high-pass (~120 Hz) followed by LP (~2.6 kHz).  Damping is
  // deliberately mild; preserves 1100-2300-Hz SSTV tone transitions.
  const float x=static_cast<float>(pcm);
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
    if(g_sync_run<60000u)++g_sync_run;
  } else if(g_sync_run) {
    const uint32_t n=g_sync_run;
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
          g_line_origin=static_cast<double>(sync_start)+
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
void set_frequency(uint32_t hz){
  if(g_stats.receiving)finish_picture(true,EndReason::retune);
  g_frequency=hz;reset_picture();reset_vis();auto_detect::reset();
}
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
      } else {
        // Marker queue full: fold into the newest marker; the timeline error
        // is bounded by the burst length and PLL/sync relock absorbs it.
        g_gap_count[(head-1)%kGapMarks]+=g_pending_gap;
      }
      g_pending_gap=0;
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
  uint32_t tail=g_gap_tail.load(std::memory_order_relaxed);
  const uint32_t head=g_gap_head.load(std::memory_order_acquire);
  while(r!=end&&n<budget){
    while(tail!=head&&g_gap_index[tail%kGapMarks]==r) {
      skip_gap(g_gap_count[tail%kGapMarks]);
      ++tail;
    }
    accept_sample(g_queue[r&kMask]);++r;++n;
  }
  g_gap_tail.store(tail,std::memory_order_release);
  g_read.store(r,std::memory_order_release);
  g_stats.samples+=static_cast<uint32_t>(n);
  g_stats.tone_hz=g_freq;g_stats.audio_peak=g_level;
  g_stats.signal=g_level>.01f;
  g_stats.audio_overruns=g_drops.load(std::memory_order_relaxed);
  g_stats.vis_phase=static_cast<uint8_t>(g_vis_stage);
  g_stats.vis_phase_samples=g_phase_samples;
  g_stats.state=session_state();
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
    const double pix_s=static_cast<double>(m.pixel_ns)*kRate/1.e9;
    const double first=(m.family==2?m.porch_us:m.sync_us+m.porch_us)*kRate/1.e6;
    const double second=first+3.*pix_s*m.width;
    const double lum_end=(m.family==3?second:first)+pix_s*m.width;
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
