// SPDX-License-Identifier: AGPL-3.0-only
#include "vdl2_rs.hpp"
#include "vdl2_burst.hpp"
#include <algorithm>
#include <cstring>
namespace orcsdr::vdl2 {
namespace {
uint8_t getbit(const uint8_t* in, size_t p) { return uint8_t((in[p/8] >> (p%8)) & 1u); }
void putbit(uint8_t* out, size_t p, uint8_t b) {
  if (b) out[p/8] |= uint8_t(1u << (p%8));
}
size_t rows_for(size_t bits) { return (bits + kRsData*8u-1u)/(kRsData*8u); }
size_t real_bytes(size_t total_bytes,size_t row) { return total_bytes > row*kRsData ? std::min(kRsData,total_bytes-row*kRsData):0; }
}
RsCodec::RsCodec() {
  unsigned x=1;
  for(unsigned i=0;i<255;++i){exp_[i]=uint8_t(x);log_[x]=uint8_t(i);x<<=1;if(x&256u)x^=0x187u;}
  for(unsigned i=255;i<512;++i)exp_[i]=exp_[i-255];
  generator_[0]=1;
  for(unsigned i=0;i<6;++i){
    const uint8_t root=apow(120u+i);
    for(int j=int(i)+1;j>=0;--j){
      const uint8_t prior=j?generator_[size_t(j-1)]:0;
      generator_[size_t(j)]=prior^mul(root,generator_[size_t(j)]);
    }
  }
}
uint8_t RsCodec::mul(uint8_t a,uint8_t b) const {
  return a&&b ? exp_[size_t(log_[a])+log_[b]]:0;
}
uint8_t RsCodec::div(uint8_t a,uint8_t b) const {
  return a ? (b?exp_[255u + log_[a]-log_[b]]:0):0;
}
void RsCodec::encode(const uint8_t data[kRsData],uint8_t parity[kRsCheck]) const {
  std::memset(parity,0,kRsCheck);
  for(size_t i=0;i<kRsData;++i){
    const uint8_t coef=data[i]^parity[0];
    for(size_t j=0;j<kRsCheck-1;++j)parity[j]=parity[j+1];
    parity[kRsCheck-1]=0;
    if(coef)for(size_t j=0;j<kRsCheck;++j)
      parity[j]^=mul(coef,generator_[kRsCheck-1-j]);
  }
}
bool RsCodec::correct(uint8_t cw[kRsBlock],const uint8_t* erasures,size_t f,size_t& corrected) const {
  corrected=0;
  if(f>kRsCheck || (f && !erasures))return false;
  for(size_t i=0;i<f;++i)if(erasures[i]>=255)return false;   // reject bad positions for clean codewords too
  uint8_t synd[6]{};
  bool zero=true;
  for(unsigned j=0;j<6;++j){
    const unsigned root=120u+j;
    for(unsigned i=0;i<255;++i)if(cw[i])
      synd[j]^=mul(cw[i],apow(root*(254u-i)));
    if(synd[j])zero=false;
  }
  if(zero)return true;
  uint8_t gamma[7]{};gamma[0]=1;
  unsigned gamma_deg=0;
  for(size_t i=0;i<f;++i){
    const size_t p=erasures[i];if(p>=255)return false;
    const uint8_t x=apow(unsigned(254u-p));
    for(int j=int(gamma_deg)+1;j>=1;--j)
      gamma[size_t(j)]^=mul(x,gamma[size_t(j)-1]);
    ++gamma_deg;
  }
  uint8_t fs[6]{};
  for(size_t j=0;j<6;++j)
    for(size_t k=0;k<=std::min<size_t>(j,gamma_deg);++k)fs[j]^=mul(gamma[k],synd[j-k]);
  uint8_t lambda[7]{};lambda[0]=1;
  uint8_t prev[7]{};prev[0]=1;
  size_t l=0,m=1;
  uint8_t b=1;
  for(size_t r=0;r<6-f;++r){
    uint8_t delta=fs[r+f];
    for(size_t i=1;i<=l;++i)delta^=mul(lambda[i],fs[r+f-i]);
    if(!delta){++m;continue;}
    if(2*l<=r){
      uint8_t copy[7];std::memcpy(copy,lambda,sizeof(copy));
      const uint8_t scale=div(delta,b);
      for(size_t i=0;i+m<=6;++i)lambda[i+m]^=mul(scale,prev[i]);
      std::memcpy(prev,copy,sizeof(prev));l=r+1-l;b=delta;m=1;
    } else {
      const uint8_t scale=div(delta,b);
      for(size_t i=0;i+m<=6;++i)lambda[i+m]^=mul(scale,prev[i]);
      ++m;
    }
  }
  if(l>(6-f)/2u)return false;
  uint8_t psi[13]{};
  for(size_t i=0;i<=6;++i)if(lambda[i])
    for(size_t j=0;j<=gamma_deg;++j)psi[i+j]^=mul(lambda[i],gamma[j]);
  size_t psi_deg=12;
  while(psi_deg>0 && !psi[psi_deg])--psi_deg;
  if(psi_deg>6u)return false;
  uint8_t omega[6]{};
  for(size_t j=0;j<6;++j)
    for(size_t k=0;k<=std::min(j,psi_deg);++k)omega[j]^=mul(psi[k],synd[j-k]);
  size_t found=0;
  for(unsigned pos=0;pos<255;++pos){
    const unsigned deg=254u-pos;
    const unsigned xin=(255u-(deg%255u))%255u;
    uint8_t loc=0;
    for(size_t k=0;k<=psi_deg;++k)loc^=mul(psi[k],apow(xin*unsigned(k)));
    if(loc)continue;
    uint8_t num=0,den=0;
    for(size_t k=0;k<6;++k)num^=mul(omega[k],apow(xin*unsigned(k)));
    for(size_t k=1;k<=psi_deg;k+=2)den^=mul(psi[k],apow(xin*unsigned(k-1)));
    if(!den)return false;
    // X^(1-first_root) · omega / derivative
    const unsigned adj=unsigned((int64_t(deg)*(1-120))%255+255)%255u;
    cw[pos]^=mul(div(num,den),apow(adj));
    ++found;
  }
  if(found!=psi_deg)return false;
  for(unsigned j=0;j<6;++j){
    uint8_t s=0;
    for(unsigned i=0;i<255;++i)if(cw[i])s^=mul(cw[i],apow((120u+j)*(254u-i)));
    if(s)return false;
  }
  corrected=found;
  return true;
}
size_t RsCodec::tx_bits_for(size_t bits) {
  if(bits==0 || bits>kMaxVdl2DataBits)return 0;
  const size_t nbytes=(bits+7u)/8u;
  const size_t rows=rows_for(bits);
  size_t tx=0;
  for(size_t r=0;r<rows;++r){
    const size_t n=real_bytes(nbytes,r);
    tx+=(n+final_block_rs_parity(n))*8u;
  }
  return tx;
}
bool RsCodec::deinterleave(const uint8_t* tx,size_t count,size_t bits,
                           uint8_t* out,size_t cap,FecResult& result) {
  result={};
  const size_t need=tx_bits_for(bits);
  if(!need || !tx || !out || count<need || cap<(bits+7u)/8u)return false;
  const size_t nbytes=(bits+7u)/8u, rows=rows_for(bits);
  for(size_t r=0;r<rows;++r)grid_[r].fill(0);
  size_t at=0;
  for(size_t col=0;col<255;++col)for(size_t r=0;r<rows;++r){
    const size_t n=real_bytes(nbytes,r),k=final_block_rs_parity(n);
    if(col>=n && (col<249 || col>=249+k))continue;
    uint8_t b=0;
    for(unsigned j=0;j<8;++j)b|=uint8_t(getbit(tx,at++) << j);
    grid_[r][col]=b;
  }
  std::memset(out,0,cap);
  size_t dest=0, fixes=0;
  for(size_t r=0;r<rows;++r){
    const size_t n=real_bytes(nbytes,r),k=final_block_rs_parity(n);
    if(k){
      uint8_t erased[6]{};
      for(size_t z=k;z<6;++z)erased[z-k]=uint8_t(249u+z);
      size_t changed=0;
      if(!correct(grid_[r].data(),erased,6u-k,changed))return false;
      if(changed>6u-k)fixes+=changed-(6u-k);
    }
    for(size_t j=0;j<n;++j)out[dest++]=grid_[r][j];
  }
  // Last octet may contain pad bits; leave the caller to read exactly TL bits.
  result.data_bits=bits;
  result.corrected_symbols=uint8_t(std::min<size_t>(fixes,255u));
  return true;
}
bool RsCodec::interleave(const uint8_t* data,size_t bits,uint8_t* out,size_t cap,size_t& count) {
  count=0;
  const size_t need=tx_bits_for(bits);
  if(!need || !data || !out || cap < need)return false;
  const size_t nbytes=(bits+7u)/8u,rows=rows_for(bits);
  for(size_t r=0;r<rows;++r){
    grid_[r].fill(0);
    const size_t n=real_bytes(nbytes,r);
    for(size_t j=0;j<n;++j)grid_[r][j]=data[r*kRsData+j];
    encode(grid_[r].data(),&grid_[r][249]);
  }
  std::memset(out,0,(need+7u)/8u);
  for(size_t col=0;col<255;++col)for(size_t r=0;r<rows;++r){
    const size_t n=real_bytes(nbytes,r),k=final_block_rs_parity(n);
    if(col>=n && (col<249 || col>=249+k))continue;
    const uint8_t v=grid_[r][col];
    for(unsigned bit=0;bit<8;++bit)putbit(out,count++,uint8_t((v>>bit)&1u));
  }
  return true;
}
} // namespace orcsdr::vdl2
