#include "sstv_auto_detect.hpp"
#include <algorithm>
#include <cmath>

namespace orcsdr::sstv::auto_detect {
namespace {
constexpr uint32_t kRate=12000;
constexpr unsigned kHistory=96;               // pulses kept (noise adds many false ones between the real ones)
constexpr uint32_t kWindow=kRate*10u;         // only the last ten seconds count
#ifndef SSTV_AD_MIN_NOISY
#define SSTV_AD_MIN_NOISY 8        // on-raster pulses needed when other pulses are mixed in
#endif
#ifndef SSTV_AD_OCC_NOISY
#define SSTV_AD_OCC_NOISY .75f     // and the share of the raster slots they must fill
#endif
#ifndef SSTV_AD_WIDTH_SPREAD
#define SSTV_AD_WIDTH_SPREAD 0     // widest allowed spread (samples) between on-raster pulse widths; 0 = not checked
#endif
constexpr unsigned kMaxSlots=16;              // furthest line slot from the newest pulse that is considered
struct Pulse {uint32_t begin=0; uint16_t length=0;};
Pulse g_pulses[kHistory]{};
unsigned g_count=0;
Ranked g_ranked[3]{};
unsigned g_ranked_count=0;
constexpr Mode kModes[] = {
  Mode::robot36,Mode::robot72,Mode::martin_m1,Mode::martin_m2,
  Mode::scottie_s1,Mode::scottie_s2,Mode::scottie_dx,
  Mode::pd50,Mode::pd90,Mode::pd120,Mode::pd160,Mode::pd180,
  Mode::pd240,Mode::pd290
};
// How many of the slots halfway (or a third of the way) between a mode's own line slots hold a pulse. A real Robot 72 stream
// has almost nothing there; a Robot 36 stream, which also puts a pulse on every second Robot 72 slot, has pulses in most of them.
float between_occupancy(uint32_t newest,float period,unsigned divisions,unsigned oldest_slot,float width,unsigned count) {
  unsigned total=0,filled=0;
  for(unsigned j=1;j<=oldest_slot*divisions;++j) {
    if(j%divisions==0)continue;                      // the mode's own slots
    ++total;
    const float target=j*period/divisions;
    const float tolerance=std::max(24.f,period/divisions*.02f)+0.002f*target;
    for(unsigned i=count-1;i-- >0;) {
      const float distance=static_cast<float>(newest-g_pulses[i].begin);
      if(distance>target+tolerance)break;
      if(std::fabs(distance-target)<=tolerance &&
         std::fabs(static_cast<float>(g_pulses[i].length)-width)<=std::max(18.f,width*.3f)) { ++filled; break; }
    }
  }
  return total?static_cast<float>(filled)/static_cast<float>(total):0.f;
}
struct Hypothesis {
  float width=0;
  unsigned oldest_slot=0;
  Mode mode=Mode::auto_vis;
  float period=0;
  unsigned inliers=0;     // line slots that hold a pulse of the right width
  unsigned span=0;        // line slots from the oldest inlier to the newest pulse (inclusive)
  unsigned extra=0;       // other pulses inside that span: noise, or the comb of a different mode
  float quality=0;
  float width_min=1e9f,width_max=0;   // pulse widths on the raster: real sync pulses agree, noise bursts do not
  bool valid=false;
};
}
void reset() {
  g_count=0;
  g_ranked_count=0;
}
unsigned pending_pulses() {return g_count;}
// A noisy channel puts many false pulses between the real ones, so a classifier that needs an unbroken chain of
// consistent pulses almost never finds one (about three false pulses per real one at the edge of sensitivity). Instead,
// for each mode this asks how many of the recent pulses sit on one regular line raster ending at the newest pulse
// (period, width and slot occupancy), and ignores the pulses that do not.
// How far a pulse may sit from its comb slot: edge jitter (a fixed part) plus a line-clock error that grows with the slot
// number (0.2% of the line period per slot). This cannot be widened much: a train 8.7% off Robot 36's period aliases onto
// the comb at slot 10 with 84 samples of error, so the 0.2% slope is what keeps that false train out (the auto-state suite
// checks it). A transmitter clock more than about 0.5% off is therefore identified by its VIS code instead.
static float slot_tolerance(float period,unsigned slot) {
  return std::max(24.f,period*.02f)+0.002f*static_cast<float>(slot)*period;
}
Candidate observe(uint32_t start,uint16_t length) {
  Candidate out{};
  // Ignore very short/long noise bursts. The tone estimator is slow to enter the 1200 Hz window, so pulses read
  // shorter than they are: a 9 ms sync reads about 80 samples, but Martin's 4.9 ms sync reads about 33, below
  // this floor. Sync-only identification therefore cannot recognise Martin; those modes are identified by VIS.
  if(length<35 || length>350) return out;
  // Forget pulses older than the window (wrap-safe unsigned arithmetic).
  unsigned keep_from=0;
  while(keep_from<g_count && (start-g_pulses[keep_from].begin)>kWindow)++keep_from;
  if(keep_from) {
    for(unsigned i=keep_from;i<g_count;++i)g_pulses[i-keep_from]=g_pulses[i];
    g_count-=keep_from;
  }
  if(g_count==kHistory) {
    for(unsigned i=1;i<kHistory;++i)g_pulses[i-1]=g_pulses[i];
    --g_count;
  }
  g_pulses[g_count++]={start,length};
  g_ranked_count=0;
  if(g_count<6) return out;

  Hypothesis hyp[sizeof(kModes)/sizeof(kModes[0])];
  unsigned hyp_count=0;
  for(Mode mode:kModes) {
    const ModeSpec* m=mode_spec(mode);
    if(!m)continue;
    const float period=static_cast<float>(m->line_us)*kRate/1000000.f;
    const float width=static_cast<float>(m->sync_us)*kRate/1000000.f;
    Hypothesis h;
    h.mode=mode;h.period=period;h.width=width;
    bool slot_taken[kMaxSlots+1]{};
    unsigned oldest_slot=0;
    for(unsigned i=g_count-1;i-- >0;) {     // older pulses, newest first
      const float distance=static_cast<float>(start-g_pulses[i].begin);
      const float slots=distance/period;
      if(slots>kMaxSlots+.5f)break;
      const unsigned slot=static_cast<unsigned>(slots+.5f);
      const float error=std::fabs(distance-slot*period);
      // Allow for jitter in where each noisy pulse edge is read, growing slowly with the number of lines (clock error).
      const float tolerance=slot_tolerance(period,slot);
      const bool width_ok=std::fabs(static_cast<float>(g_pulses[i].length)-width)<=std::max(18.f,width*.3f);
      if(slot>=1 && error<=tolerance && width_ok) {
        if(!slot_taken[slot]) { slot_taken[slot]=true; ++h.inliers; }
        h.width_min=std::min(h.width_min,static_cast<float>(g_pulses[i].length));
        h.width_max=std::max(h.width_max,static_cast<float>(g_pulses[i].length));
        oldest_slot=std::max(oldest_slot,slot);
      }
    }
    // The newest pulse counts as slot 0 and an inlier by definition (and must have a believable width itself).
    if(std::fabs(static_cast<float>(length)-width)>std::max(18.f,width*.3f)) { hyp[hyp_count++]=h; continue; }
    h.inliers+=1;
    h.width_min=std::min(h.width_min,static_cast<float>(length));
    h.width_max=std::max(h.width_max,static_cast<float>(length));
    h.span=oldest_slot+1;h.oldest_slot=oldest_slot;
    // Only pulses inside the occupied span can contradict the hypothesis.
    h.extra=0;
    for(unsigned i=g_count-1;i-- >0;) {
      const float distance=static_cast<float>(start-g_pulses[i].begin);
      if(distance/period>static_cast<float>(oldest_slot)+.5f)break;
      const unsigned slot=static_cast<unsigned>(distance/period+.5f);
      const float error=std::fabs(distance-slot*period);
      const float tolerance=slot_tolerance(period,slot);
      const bool width_ok=std::fabs(static_cast<float>(g_pulses[i].length)-width)<=std::max(18.f,width*.3f);
      if(!(slot>=1 && error<=tolerance && width_ok))++h.extra;
    }
    const float occupancy=h.span?static_cast<float>(h.inliers)/static_cast<float>(h.span):0.f;
    // A clean train (nothing else in the span) needs six pulses, as before. With other pulses mixed in, demand more
    // evidence: eight on-raster pulses filling most of their span.
    if(h.extra==0) h.valid=h.inliers>=6 && occupancy>=.99f;
    else h.valid=h.inliers>=SSTV_AD_MIN_NOISY && occupancy>=SSTV_AD_OCC_NOISY;
    if(SSTV_AD_WIDTH_SPREAD>0 && h.extra>0 && (h.width_max-h.width_min)>static_cast<float>(SSTV_AD_WIDTH_SPREAD))h.valid=false;
    h.quality=std::clamp(50.f+6.f*static_cast<float>(h.inliers)-30.f*(1.f-occupancy)-.5f*static_cast<float>(h.extra),0.f,100.f);
    hyp[hyp_count++]=h;
  }
  // A train of lines at period P also fills every second slot of a mode with period 2P (Robot 36 against Robot 72):
  // when both look valid the shorter period is the real one.
  for(unsigned a=0;a<hyp_count;++a) {
    if(!hyp[a].valid)continue;
    for(unsigned b=0;b<hyp_count;++b) {
      if(a==b||!hyp[b].valid)continue;
      const float ratio=hyp[a].period/hyp[b].period;
      if((std::fabs(ratio-2.f)<.06f||std::fabs(ratio-3.f)<.09f)) { hyp[a].valid=false;break; }
    }
  }
  // A comb that is really a slower mode's comb with pulses halfway between its slots belongs to the faster mode.
  for(unsigned a=0;a<hyp_count;++a) {
    if(!hyp[a].valid)continue;
    for(unsigned div=2;div<=3&&hyp[a].valid;++div)
      if(between_occupancy(start,hyp[a].period,div,hyp[a].oldest_slot,hyp[a].width,g_count)>=.35f)hyp[a].valid=false;
  }
  float best_quality=-1.f,second_quality=-1.f;
  Mode best_mode=Mode::auto_vis;
  unsigned best_inliers=0;
  for(unsigned i=0;i<hyp_count;++i) {
    if(!hyp[i].valid)continue;
    const uint8_t q=static_cast<uint8_t>(hyp[i].quality+.5f);
    unsigned at=g_ranked_count<3?g_ranked_count:3;
    while(at>0 && g_ranked[at-1].score<q) --at;   // keep the three strongest valid candidates for the Signal page
    if(at<3) {
      for(unsigned k=(g_ranked_count<3?g_ranked_count:2);k>at;--k)g_ranked[k]=g_ranked[k-1];
      g_ranked[at]={hyp[i].mode,q};
      if(g_ranked_count<3)++g_ranked_count;
    }
    if(hyp[i].quality>best_quality) {
      second_quality=best_quality;
      best_quality=hyp[i].quality;
      best_mode=hyp[i].mode;
      best_inliers=hyp[i].inliers;
    } else if(hyp[i].quality>second_quality) second_quality=hyp[i].quality;
  }
  // Require a believable sequence and a substantial best-vs-runner-up margin.
  // Mode family/offset validation is subsequently performed by line tracking.
  if(best_quality>=75.f && best_quality-second_quality>=12.f) {
    out.mode=best_mode;
    out.score=static_cast<uint8_t>(best_quality+.5f);
    out.pulses=static_cast<uint8_t>(std::min<unsigned>(best_inliers,255u));
    out.sync_start=start;
    out.locked=true;
  }
  return out;
}
size_t ranked(Ranked* out,size_t capacity) {
  const size_t n=std::min<size_t>(capacity,g_ranked_count);
  for(size_t i=0;i<n;++i)out[i]=g_ranked[i];
  return n;
}
} // namespace orcsdr::sstv::auto_detect
