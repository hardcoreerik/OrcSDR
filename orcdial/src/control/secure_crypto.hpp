#pragma once
#include "secure_frame.hpp"
#include <mbedtls/aes.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <mbedtls/platform_util.h>

namespace orc::secure {
inline void wipe(void* p,size_t n) { mbedtls_platform_zeroize(p,n); }
// RFC 4493 CMAC using mbedTLS AES: Arduino's bundled mbedTLS disables its
// cipher-CMAC wrapper. This avoids changing either firmware's library config.
inline bool cmac(const uint8_t key[16],const uint8_t* data,size_t n,uint8_t out[16]) {
  mbedtls_aes_context aes;mbedtls_aes_init(&aes);
  bool ok=mbedtls_aes_setkey_enc(&aes,key,128)==0;
  uint8_t k[16]{},x[16]{},block[16]{};
  if(ok) ok=mbedtls_aes_crypt_ecb(&aes,MBEDTLS_AES_ENCRYPT,k,k)==0;
  auto shift=[](uint8_t* v) { const bool high=v[0]&128;for(int i=0;i<15;++i) v[i]=uint8_t((v[i]<<1)|(v[i+1]>>7));v[15]<<=1;if(high)v[15]^=0x87; };
  shift(k);const bool complete=n && !(n%16);if(!complete) shift(k);
  const size_t blocks=n?(n+15)/16:1;
  for(size_t b=0;ok && b<blocks;++b) {
    std::memset(block,0,16);size_t length=n-b*16;if(length>16)length=16;
    if(length)std::memcpy(block,data+b*16,length);
    if(b+1==blocks) { if(!complete) block[length]=0x80;for(int j=0;j<16;++j)block[j]^=k[j]; }
    for(int j=0;j<16;++j) block[j]^=x[j];
    ok=mbedtls_aes_crypt_ecb(&aes,MBEDTLS_AES_ENCRYPT,block,x)==0;
  }
  if(ok)std::memcpy(out,x,16);else std::memset(out,0,16);
  wipe(k,16);wipe(x,16);wipe(block,16);mbedtls_aes_free(&aes);return ok;
}
// Bluetooth Core Vol 3 Part H 2.2.6-2.2.9. Inputs are MSB-first octets,
// unlike the little-endian fragment/control serialization.
inline bool f4(const uint8_t u[32],const uint8_t v[32],const uint8_t x[16],uint8_t z,uint8_t out[16]) {
  uint8_t m[65];std::memcpy(m,u,32);std::memcpy(m+32,v,32);m[64]=z;return cmac(x,m,sizeof m,out);
}
inline bool f5(const uint8_t w[32],const uint8_t n1[16],const uint8_t n2[16],const uint8_t a1[7],const uint8_t a2[7],uint8_t mac[16],uint8_t ltk[16]) {
  const uint8_t salt[16]={0x6c,0x88,0x83,0x91,0xaa,0xf5,0xa5,0x38,0x60,0x37,0x0b,0xdb,0x5a,0x60,0x83,0xbe};
  uint8_t t[16]{},m[53]={0,'b','t','l','e'};std::memcpy(m+5,n1,16);std::memcpy(m+21,n2,16);
  std::memcpy(m+37,a1,7);std::memcpy(m+44,a2,7);m[51]=1;m[52]=0;
  bool ok=cmac(salt,w,32,t)&&cmac(t,m,sizeof m,mac);m[0]=1;ok=ok&&cmac(t,m,sizeof m,ltk);wipe(t,16);return ok;
}
inline bool f6(const uint8_t w[16],const uint8_t n1[16],const uint8_t n2[16],const uint8_t r[16],const uint8_t io[3],const uint8_t a1[7],const uint8_t a2[7],uint8_t out[16]) {
  uint8_t m[65];std::memcpy(m,n1,16);std::memcpy(m+16,n2,16);std::memcpy(m+32,r,16);std::memcpy(m+48,io,3);std::memcpy(m+51,a1,7);std::memcpy(m+58,a2,7);return cmac(w,m,sizeof m,out);
}
inline bool g2(const uint8_t u[32],const uint8_t v[32],const uint8_t x[16],const uint8_t y[16],uint32_t& code) {
  uint8_t m[80],out[16];std::memcpy(m,u,32);std::memcpy(m+32,v,32);std::memcpy(m+64,y,16);
  if(!cmac(x,m,sizeof m,out))return false;
  code=((uint32_t(out[12])<<24)|(uint32_t(out[13])<<16)|(uint32_t(out[14])<<8)|out[15])%1000000;return true;
}
inline bool hmac(const uint8_t* key,size_t kn,const uint8_t* p,size_t n,uint8_t out[32]) {
  const auto* info=mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  return info && mbedtls_md_hmac(info,key,kn,p,n,out)==0;
}
class KeyAgreement {
 public:
  KeyAgreement() {mbedtls_ecp_group_init(&group_);mbedtls_mpi_init(&private_);mbedtls_ecp_point_init(&public_);}
  ~KeyAgreement() {mbedtls_ecp_group_free(&group_);mbedtls_mpi_free(&private_);mbedtls_ecp_point_free(&public_);}
  KeyAgreement(const KeyAgreement&)=delete;KeyAgreement& operator=(const KeyAgreement&)=delete;
  bool generate(int (*rng)(void*,unsigned char*,size_t),void* context,uint8_t out[65]) {
    mbedtls_mpi_free(&private_);mbedtls_mpi_init(&private_);
    size_t n=0;
    return mbedtls_ecp_group_load(&group_,MBEDTLS_ECP_DP_SECP256R1)==0 &&
      mbedtls_ecp_gen_keypair(&group_,&private_,&public_,rng,context)==0 &&
      mbedtls_ecp_point_write_binary(&group_,&public_,MBEDTLS_ECP_PF_UNCOMPRESSED,&n,out,65)==0 && n==65;
  }
  bool derive(const uint8_t in[65],int (*rng)(void*,unsigned char*,size_t),void* context,uint8_t out[32]) {
    mbedtls_ecp_point q;mbedtls_ecp_point_init(&q);mbedtls_mpi z;mbedtls_mpi_init(&z);
    bool ok=in[0]==4 && mbedtls_ecp_point_read_binary(&group_,&q,in,65)==0 &&
      mbedtls_ecp_check_pubkey(&group_,&q)==0 && mbedtls_ecdh_compute_shared(&group_,&z,&q,&private_,rng,context)==0 &&
      mbedtls_mpi_write_binary(&z,out,32)==0;
    mbedtls_ecp_point_free(&q);mbedtls_mpi_free(&z);if(!ok)wipe(out,32);return ok;
  }
  void clear() {mbedtls_mpi_free(&private_);mbedtls_mpi_init(&private_);}
 private: mbedtls_ecp_group group_;mbedtls_mpi private_;mbedtls_ecp_point public_;
};
inline bool gcm(bool encrypt,const uint8_t key[32],const uint8_t nonce[12],const uint8_t* aad,size_t an,
                const uint8_t* in,size_t n,uint8_t* out,uint8_t tag[16]) {
  mbedtls_gcm_context ctx;mbedtls_gcm_init(&ctx);
  int result=mbedtls_gcm_setkey(&ctx,MBEDTLS_CIPHER_ID_AES,key,256);
  if(!result)result=encrypt?mbedtls_gcm_crypt_and_tag(&ctx,MBEDTLS_GCM_ENCRYPT,n,nonce,12,aad,an,in,out,16,tag):
    mbedtls_gcm_auth_decrypt(&ctx,n,nonce,12,aad,an,tag,16,in,out);
  mbedtls_gcm_free(&ctx);if(result && !encrypt)wipe(out,n);return result==0;
}
} // namespace orc::secure
