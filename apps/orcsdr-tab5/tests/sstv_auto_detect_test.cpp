// Fixed-size classifier regression. No ESP-IDF, SDR driver or display required.
#include "sstv_auto_detect.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>

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
  // Integer sample timestamps may wrap during unattended operation.
  auto_detect::reset();
  bool wrap_locked=false;
  uint32_t start=0xffff8000u;
  for(unsigned i=0;i<8;++i) {
    auto c=auto_detect::observe(start+i*1800u,108u);
    wrap_locked|=c.locked && c.mode==Mode::robot36;
  }
  check(wrap_locked,"wrap-safe pulse time arithmetic");
  std::puts("SSTV AUTO DETECT: PASS (14 modes, noise rejection, counter wrap)");
}
