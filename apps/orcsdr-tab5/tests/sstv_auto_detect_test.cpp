// Fixed-size classifier regression. No ESP-IDF, SDR driver or display required.
#include "sstv_auto_detect.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

using namespace orcsdr::sstv;
namespace {
void check(bool ok,const char* what) {
  if(!ok){std::fprintf(stderr,"AUTO DETECT FAIL: %s\n",what);std::exit(1);}
}
constexpr Mode kModes[]={
  Mode::robot36,Mode::robot72,Mode::martin_m1,Mode::martin_m2,
  Mode::scottie_s1,Mode::scottie_s2,Mode::scottie_dx,
  Mode::pd50,Mode::pd90,Mode::pd120,Mode::pd160,Mode::pd180,
  Mode::pd240,Mode::pd290
};
}
int main() {
  for(Mode mode:kModes) {
    auto_detect::reset();
    const auto* spec=mode_spec(mode);
    check(spec!=nullptr,"known mode");
    const double per=static_cast<double>(spec->line_us)*12000/1000000.;
    const auto width=static_cast<uint16_t>(std::lround(spec->sync_us*12000./1000000.));
    auto_detect::Candidate last{};
    for(unsigned i=0;i<8;++i) {
      auto c=auto_detect::observe(
         10000u+static_cast<uint32_t>(std::lround(per*i)),width);
      if(c.locked)last=c;
    }
    if(!last.locked || last.mode!=mode) {
      std::fprintf(stderr,"Unexpected classification for %s: locked=%u mode=%u score=%u\n",
        spec->name,last.locked,static_cast<unsigned>(last.mode),last.score);
      return 1;
    }
    check(last.pulses>=6 && last.score>=75,"classification confidence");
  }
  auto_detect::reset();
  bool noise_locked=false;
  uint32_t t=10000;
  for(unsigned i=0;i<60;++i) {
    t+=237u + (i*83u)%503u;
    noise_locked|=auto_detect::observe(t,static_cast<uint16_t>(38+(i*13u)%190u)).locked;
  }
  check(!noise_locked,"irregular noise must not falsely lock");
  // The weak-signal case: about three false pulses between every real one (noise and picture content crossing the sync band).
  // A classifier that needs an unbroken chain of consistent pulses almost never finds one; the comb search must.
  {
    struct Case {Mode mode; Mode must_not_be;};
    for(const Case c:{Case{Mode::robot36,Mode::robot72},Case{Mode::robot72,Mode::robot36},Case{Mode::scottie_s1,Mode::robot36}}) {
      auto_detect::reset();
      const auto* spec=mode_spec(c.mode);
      const double per=static_cast<double>(spec->line_us)*12000/1000000.;
      const auto width=static_cast<uint16_t>(std::lround(spec->sync_us*12000./1000000.));
      uint32_t lcg=12345u;
      auto next=[&](){lcg=lcg*1664525u+1013904223u;return lcg>>8;};
      bool locked=false;Mode got=Mode::auto_vis;unsigned lines_needed=0;
      for(unsigned line=0;line<40&&!locked;++line) {
        const uint32_t real=20000u+static_cast<uint32_t>(std::lround(per*line))+static_cast<uint32_t>(next()%25)-12u;   // edge jitter
        // false pulses scattered through the line, with widths anywhere in the accepted range
        for(unsigned k=0;k<3;++k) {
          const uint32_t at=20000u+static_cast<uint32_t>(std::lround(per*line))+static_cast<uint32_t>(next()%static_cast<uint32_t>(per));
          if(at<real) { auto_detect::observe(at,static_cast<uint16_t>(40+next()%200u)); }
        }
        const auto r=auto_detect::observe(real,static_cast<uint16_t>(width+static_cast<int>(next()%9u)-4));
        if(r.locked){locked=true;got=r.mode;lines_needed=line+1;}
        for(unsigned k=0;k<3;++k) {   // false pulses after the real one (kept in time order)
          const uint32_t at=real+1u+static_cast<uint32_t>(next()%static_cast<uint32_t>(per-2));
          if(at<20000u+static_cast<uint32_t>(std::lround(per*(line+1)))-12u) auto_detect::observe(at,static_cast<uint16_t>(40+next()%200u));
        }
      }
      char why[96];
      std::snprintf(why,sizeof(why),"noisy train locks %s within 40 lines",spec->name);
      check(locked,why);
      std::snprintf(why,sizeof(why),"noisy %s train is classified as %s",spec->name,spec->name);
      check(got==c.mode && got!=c.must_not_be,why);
      check(lines_needed<=25,"noisy train acquires within about 25 lines");
    }
  }
  // A transmitter (or sound card) whose line clock is off by 0.5% must still classify: the comb tolerance grows with the slot
  // number, so a small uniform rate error does not push the older pulses out of their slots. (Larger errors rely on VIS.)
  for(const double rate:{0.995,1.005}) {
    for(Mode mode:{Mode::robot36,Mode::robot72,Mode::scottie_s1}) {
      auto_detect::reset();
      const auto* spec=mode_spec(mode);
      const double per=static_cast<double>(spec->line_us)*12000/1000000.*rate;
      const auto width=static_cast<uint16_t>(std::lround(spec->sync_us*12000./1000000.));
      bool ok=false;
      for(unsigned i=0;i<12;++i) {
        const auto c=auto_detect::observe(10000u+static_cast<uint32_t>(std::lround(per*i)),width);
        ok|=c.locked&&c.mode==mode;
      }
      char why[96];
      std::snprintf(why,sizeof(why),"%s classifies with a %.1f%% line-clock error",spec->name,(rate-1.)*100.);
      check(ok,why);
    }
  }
  // Integer sample timestamps may wrap during unattended operation.
  auto_detect::reset();
  bool wrap_locked=false;
  uint32_t start=0xffff8000u;
  for(unsigned i=0;i<8;++i) {
    auto c=auto_detect::observe(start+i*1800u,108u);
    wrap_locked|=c.locked && c.mode==Mode::robot36;
  }
  check(wrap_locked,"wrap-safe pulse time arithmetic");
  // The Signal page shows the strongest candidates: Robot 36 must lead a Robot 36 pulse train.
  auto_detect::Ranked top[3];
  const size_t shown=auto_detect::ranked(top,3);
  check(shown>=1 && top[0].mode==Mode::robot36 && top[0].score>=75,"ranked candidates lead with the true mode");
  for(size_t i=1;i<shown;++i)check(top[i-1].score>=top[i].score,"ranked candidates are ordered");
  auto_detect::reset();
  check(auto_detect::ranked(top,3)==0 && auto_detect::pending_pulses()==0,"reset clears candidates and pulses");
  std::puts("SSTV AUTO DETECT: PASS (14 modes, noise rejection, counter wrap)");
}
