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
    if(row==100) {
      // 88 one-millisecond alternating 1500/2300Hz stripes total exactly
      // 88ms: a demanding high-spatial-frequency luma regression that the
      // old zero-cross-only estimator badly blurred.
      for(unsigned stripe=0;stripe<88;++stripe)
        tone((stripe&1u)?2300:1500,1);
    } else tone(1900,88); // neutral gray luminance
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
  std::fprintf(stderr,"SSTV debug VIS_OK=%lu VIS_ERR=%lu frames=%lu row=%u phase=%u phase_n=%lu samples=%lu tone=%f peak=%f status=%s\n",
      (unsigned long)stats.good_vis, (unsigned long)stats.bad_vis,
      (unsigned long)stats.frames, (unsigned)stats.row, (unsigned)stats.vis_phase,
      (unsigned long)stats.vis_phase_samples,(unsigned long)stats.samples,stats.tone_hz,stats.audio_peak,stats.status);
  require(stats.good_vis>=1,"recognized Robot36 VIS");
  require(archive_count()==1,"completed Robot36 frame");
  const auto image=archived_image(0);
  require(image.width==320 && image.height==240,"Robot36 dimensions");
  require(image.pixels!=nullptr,"decoded PSRAM/host image buffer");
  // 320-column luminance reconstruction retains contrast for ~3.6-pixel
  // stripes. This test previously measured ~982 with the original
  // zero-cross frequency estimator vs ~5144 for the dual estimator.
  unsigned edge_energy=0;
  const uint16_t* bar=image.pixels+100u*320u;
  for(unsigned x=10;x<309;++x)
    edge_energy+=static_cast<unsigned>(std::abs(
      static_cast<int>((bar[x+1]>>5)&63)-
      static_cast<int>((bar[x]>>5)&63)));
  require(edge_energy>2500u,
          "Robot36 1ms luminance stripes preserve fine detail");
  require(metrics().audio_overruns==0,"no audio overruns");
  // PD120 transmits two luminance rows per 508.480 ms super-line:
  // 20ms sync + 2.08ms porch + 4 x 121.60ms Y/Cr/Cb/Y.
  // Validate progressive presentation BEFORE the whole frame finishes.
  const auto before_pd = metrics();
  stop();
  start(145800000);
  preamble();
  vis(95);
  for(unsigned pair=0;pair<248;++pair) {
    tone(1200,20);
    tone(1500,2.08);
    for(unsigned channel=0;channel<4;++channel)tone(1900,121.60);
    if(pair==5) {
      flush();
      const auto partial=live_image();
      require(partial.pixels && partial.partial,"PD120 live progressive buffer");
      require(partial.decoded_rows>=8 && partial.decoded_rows<496,
              "PD120 initial lines appear before frame completion");
      require(partial.width==640 && partial.height==496,"PD120 native size");
    }
  }
  tone(1900,45);
  flush();
  std::fprintf(stderr,"PD120 line sync: locked=%lu missed=%lu\\n",
      static_cast<unsigned long>(metrics().sync_locked_lines),
      static_cast<unsigned long>(metrics().sync_missed_lines));
  require(metrics().sync_locked_lines-before_pd.sync_locked_lines>=220,
          "PD120 sync locked on nearly all transmitted pairs");
  require(metrics().sync_missed_lines-before_pd.sync_missed_lines<=12,
          "PD120 missed sync count bounded");
  require(archive_count()==2,"Robot36 and PD120 archived");
  require(archived_image(0).mode==Mode::pd120 &&
          archived_image(0).decoded_rows==496,
          "PD120 complete 496-line image");
  require(metrics().audio_overruns==0,"no ring overruns during PD120");
  for(unsigned i=0;i<128;++i) {
    if(const auto* p=vis_spec(static_cast<uint8_t>(i)))
      require(p->width>0 && p->pixel_ns>0,"all modes have valid timing");
  }
  // Scottie S1: unlike PD/Martin, it has an extra 9ms 1200Hz pulse
  // immediately after VIS.  Its regular sync is mid-line after G/B.
  stop();
  start(145800000);
  preamble();
  vis(60);
  tone(1200,9); // pre-sync outside the 256 regular 428.22ms lines
  for(unsigned row=0;row<256;++row) {
    tone(1500,1.5); tone(1900,138.24); // G
    tone(1500,1.5); tone(1900,138.24); // B
    tone(1200,9);
    tone(1500,1.5); tone(1900,138.24); // R
    if(row==4) {
      flush();
      const auto img=live_image();
      require(img.pixels && img.partial && img.mode==Mode::scottie_s1,
              "Scottie S1 progressive image exists");
      require(img.decoded_rows>=3 && img.decoded_rows<256,
              "Scottie extra post-VIS sync does not erase early image");
    }
  }
  tone(1900,45);
  flush();
  require(archive_count()==3,"Robot36, PD120 and Scottie S1 archived");
  require(archived_image(0).decoded_rows==256,
          "Scottie S1 complete 256-row image");
  clear_gallery();
  stop();

  // Auto VIS+Sync: infer a mode from repeated line syncs without any VIS
  // header, and never label a mid-stream recovered slice as complete.
  set_auto_strategy(AutoStrategy::vis_sync);
  start(145800000);
  robot36_rows(); // intentionally omit the VIS header
  tone(1900,1700); // longer than the 8 missed lines (1.2 s of Robot 36) that end a picture
  flush();
  require(archive_count()==1,"missing-VIS Robot36 archived as partial");
  const auto partial_auto=archived_image(0);
  require(partial_auto.mode==Mode::robot36 &&
          partial_auto.identification==Identification::sync &&
          partial_auto.start_unknown && partial_auto.partial &&
          partial_auto.end_reason==EndReason::sync_lost,
          "sync-only result explicitly marks unknown origin and interrupted end");
  require(partial_auto.decoded_rows>=200 && partial_auto.decoded_rows<240,
          "sync-only decoder preserves useful rows without inventing full frame");
  clear_gallery();stop();

  // Disabled fallback must not invent an SSTV mode from raw sync pulses.
  set_auto_strategy(AutoStrategy::vis_only);
  start(145800000);
  for(unsigned row=0;row<15;++row) {
    tone(1200,9);tone(1500,3);tone(1900,138);
  }
  flush();
  require(archive_count()==0 && !metrics().receiving,
          "VIS-only strategy rejects headerless line sync");
  stop();
  set_auto_strategy(AutoStrategy::vis_sync);

  // A new valid VIS must preserve earlier incomplete image and decode
  // the next transmitter's mode without operator restart.
  start(145800000);
  preamble();vis(95);
  for(unsigned pair=0;pair<8;++pair) {
    tone(1200,20);tone(1500,2.08);
    for(unsigned segment=0;segment<4;++segment)tone(1900,121.60);
  }
  flush();
  require(metrics().receiving && metrics().received_mode==Mode::pd120,
          "PD120 still active before replacement VIS");
  preamble();vis(8);
  robot36_rows();tone(1900,50);flush();
  require(archive_count()==2,"new VIS creates new frame and retains partial");
  const auto old=archived_image(1),fresh=archived_image(0);
  require(old.partial && old.mode==Mode::pd120 &&
          old.end_reason==EndReason::new_vis,
          "interrupted PD120 preserved as partial on new VIS");
  require(!fresh.partial && fresh.mode==Mode::robot36 &&
          fresh.decoded_rows==240,
          "new Robot36 fully decoded and archived without manual reset");
  clear_gallery();
  stop();
  std::puts("SSTV host smoke: PASS");
  return 0;
}
