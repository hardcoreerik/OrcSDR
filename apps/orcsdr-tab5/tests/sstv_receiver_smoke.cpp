// Host regression for the isolated native SSTV receiver.
// Build/run command lives in docs/sstv/README.md and GitHub Actions.
#include "sstv_receiver.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr int kSampleRate = 48000;
std::array<int16_t, 480> pcm{};
size_t pending = 0;
double phase = 0;
void flush() {
  if (!pending) return;
  orcsdr::sstv::offer_audio_48k(pcm.data(), pending);
  orcsdr::sstv::service(16000);
  pending = 0;
}
void tone(double hz, double ms) {
  const auto samples = static_cast<int>(std::lround(kSampleRate * ms / 1000.));
  const double delta = (2.0*kPi*hz)/kSampleRate;
  for (int i=0;i<samples;++i) {
    pcm[pending++] = static_cast<int16_t>(std::lround(11000.*std::sin(phase)));
    phase += delta;
    if (phase >= 2.0*kPi) phase -= 2.0*kPi;
    if (pending == pcm.size()) flush();
  }
}
void preamble() {
  tone(1900,300);
  tone(1200,10);
  tone(1900,300);
}
void vis(uint8_t code, bool flip_parity=false) {
  tone(1200,30);
  unsigned ones=0;
  for(unsigned i=0;i<7;++i) {
    const bool bit=(code>>i)&1u;
    if(bit)++ones;
    tone(bit?1100:1300,30);
  }
  tone(((ones&1u)!=0)^flip_parity?1100:1300,30);
  tone(1200,30);
}
void robot36_rows() {
  for(unsigned row=0;row<240;++row) {
    tone(1200,9);
    tone(1500,3);
    tone(1900,88); // neutral gray luminance
    tone((row&1u)?2300:1500,4.5);
    tone(1900,1.5);
    tone(1900,44); // neutral chroma
  }
}
void require(bool pass, const char* why) {
  if (!pass) { std::fprintf(stderr, "SSTV FAIL: %s\n",why);std::exit(1); }
}
}
int main() {
  using namespace orcsdr::sstv;
  require(self_check(),"static receiver invariants");
  set_mode(Mode::auto_vis);
  start(145800000);
  preamble();
  tone(1200,10); // break alone must NOT count as VIS
  flush();
  require(metrics().good_vis==0, "rejected incomplete VIS preamble");
  stop();
  start(145800000);
  preamble();
  vis(8);
  robot36_rows();
  tone(1900,50); // drain a possibly delayed stop-bit edge and final scan line
  flush();
  const auto stats = metrics();
  std::fprintf(stderr,"SSTV debug VIS_OK=%lu VIS_ERR=%lu frames=%lu row=%u phase=%u phase_n=%lu samples=%lu tone=%f peak=%f status=%s\\n",
      (unsigned long)stats.good_vis, (unsigned long)stats.bad_vis,
      (unsigned long)stats.frames, (unsigned)stats.row, (unsigned)stats.vis_phase,
      (unsigned long)stats.vis_phase_samples,(unsigned long)stats.samples,stats.tone_hz,stats.audio_peak,stats.status);
  require(stats.good_vis>=1,"recognized Robot36 VIS");
  require(archive_count()==1,"completed Robot36 frame");
  const auto image=archived_image(0);
  require(image.width==320 && image.height==240,"Robot36 dimensions");
  require(image.pixels!=nullptr,"decoded PSRAM/host image buffer");
  require(metrics().audio_overruns==0,"no audio overruns");
  clear_gallery();
  stop();
  std::puts("SSTV host smoke: PASS");
  return 0;
}
