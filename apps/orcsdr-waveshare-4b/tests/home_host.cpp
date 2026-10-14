#include "../main/home_4b_layout.hpp"
#include "../main/home_fft.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
using namespace orcsdr::waveshare4b;
int main() {
  for (Rect r : {kHeader,kStatus,kTuner,kSpectrumPanel,kSpectrum,kWaterfallPanel,
                 kWaterfall,kControls,kPreset1,kPreset2,kPreset3,kPreset4,
                 kTuneDown,kStep,kTuneUp,kTunerMinus,kTunerReadout,kTunerPlus}) assert(r.inside());
  assert(kSpectrum.x>=kSpectrumPanel.x && kSpectrum.y>=kSpectrumPanel.y);
  assert(kSpectrum.x+kSpectrum.w<=kSpectrumPanel.x+kSpectrumPanel.w);
  assert(kWaterfall.y+kWaterfall.h<=kWaterfallPanel.y+kWaterfallPanel.h);
  assert(kControls.y+kControls.h<=720);
  assert(tune(kDefaultHz,1,12500)==96112500);
  assert(tune(kDefaultHz,-1,12500)==96087500);
  assert(tune(kMinHz,-1,12500)==kMinHz && tune(kMaxHz,1,12500)==kMaxHz);
  assert(!spectrum_from_cu8(nullptr,0).real_samples);
  std::vector<uint8_t> iq(kFftSize * 2);
  constexpr float pi=3.14159265358979323846f;
  for(size_t i=0;i<kFftSize;i++) {
    const float theta=2.f*pi*32.f*float(i)/256.f;
    iq[i*2] = static_cast<uint8_t>(127.5f + 80.f*cosf(theta));
    iq[i*2+1] = static_cast<uint8_t>(127.5f + 80.f*sinf(theta));
  }
  const auto s=spectrum_from_cu8(iq.data(),iq.size());
  assert(s.real_samples);
  auto peak=static_cast<size_t>(std::max_element(s.values.begin(),s.values.end())-s.values.begin());
  assert(peak>=159 && peak<=161);
  std::printf("HOME_4B_HOST PASS geometry + tuning + FFT peak bin %zu\n",peak);
}
