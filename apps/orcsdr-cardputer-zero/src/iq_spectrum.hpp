#pragma once
#include "scan_dsp.h"
#include <array>
#include <algorithm>
#include <cstdint>
namespace orc::zero {
inline void spectrum_row(orcnode_fft_t& fft,const std::uint8_t* iq,std::array<std::uint8_t,256>& row){
  std::array<float,ORCNODE_FFT_N> psd{};
  if(orcnode_cu8_psd(&fft,iq,2*ORCNODE_FFT_N,psd.data())!=0)return;
  std::array<float,ORCNODE_WATERFALL_BINS> power{};
  for(std::size_t i=0;i<power.size();++i){
    const auto at=(2*i+ORCNODE_FFT_N/2)%ORCNODE_FFT_N;
    power[i]=std::max(psd[at],psd[(at+1)%ORCNODE_FFT_N]);
  }
  std::array<std::uint8_t,ORCNODE_WATERFALL_BINS> bins{};
  orcnode_waterfall_render(power.data(),bins.data());
  for(std::size_t i=0;i<row.size();++i) row[i]=std::max(bins[2*i],bins[2*i+1]);
}
}
