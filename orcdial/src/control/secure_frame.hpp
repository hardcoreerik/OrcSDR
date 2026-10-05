#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>

namespace orc::secure {
constexpr size_t frame_size = 64, message_limit = 192, fragment_payload = 48;
constexpr uint8_t wire_version = 4, fragment_limit = 8;
enum class Kind : uint8_t { discover=1, public_key, commitment, nonce, approval,
                           offer, proof, data };
inline void u32(uint8_t* p, uint32_t x) { for (int i=0;i<4;++i) p[i]=uint8_t(x>>(i*8)); }
inline uint32_t u32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1])<<8 | uint32_t(p[2])<<16 | uint32_t(p[3])<<24; }
inline bool equal(const uint8_t* a, const uint8_t* b, size_t n) {
  uint8_t d=0; for(size_t i=0;i<n;++i) d|=a[i]^b[i]; return d==0;
}
struct Message { Kind kind{}; uint8_t role=0; uint32_t exchange=0; uint16_t size=0; uint8_t data[message_limit]{}; };
inline bool fragment(const Message& m, uint8_t index, uint8_t out[frame_size]) {
  const size_t count=(m.size+fragment_payload-1)/fragment_payload;
  if(!m.exchange || !m.size || m.size>message_limit || count>fragment_limit || index>=count ||
     m.role<1 || m.role>2 || uint8_t(m.kind)<1 || uint8_t(m.kind)>8) return false;
  std::memset(out,0,frame_size);
  out[0]='O';out[1]='D';out[2]='S';out[3]='4';out[4]=wire_version;
  out[5]=uint8_t(m.kind);out[6]=m.role;out[7]=index;
  u32(out+8,m.exchange);out[12]=uint8_t(m.size);out[13]=uint8_t(m.size>>8);out[14]=uint8_t(count);
  const size_t offset=index*fragment_payload;
  const size_t length=m.size-offset<fragment_payload?m.size-offset:fragment_payload;
  out[15]=uint8_t(length); std::memcpy(out+16,m.data+offset,length);return true;
}
// One bounded assembly. A new exchange from the pinned peer replaces an abandoned one.
// Caller pins the peer MAC too; no allocation or work proportional to attacker input.
class Reassembly {
 public:
  void reset() {clear_message();for(auto& entry:retired_)entry={};next_retired_=0;}
  bool active() const { return mask_!=0; }
  void expire(uint32_t now) {if(active() && uint32_t(now-started_)>2000){retire(message_.exchange,now);clear_message();}}
  bool push(const uint8_t* f,size_t n,uint32_t now,Message& out) {
    expire(now);
    if(!f || n!=frame_size || std::memcmp(f,"ODS4",4) || f[4]!=wire_version ||
       f[5]<1 || f[5]>8 || f[6]<1 || f[6]>2 || !u32(f+8)) return false;
    const size_t size=size_t(f[12])|(size_t(f[13])<<8), count=(size+fragment_payload-1)/fragment_payload;
    if(!size || size>message_limit || count>fragment_limit || f[14]!=count || f[7]>=count) return false;
    const size_t offset=f[7]*fragment_payload,length=size-offset<fragment_payload?size-offset:fragment_payload;
    if(f[15]!=length) return false;
    for(size_t i=16+length;i<frame_size;++i) if(f[i]) return false;
    const uint32_t exchange=u32(f+8);
    for(const auto& entry:retired_)if(entry.exchange==exchange && uint32_t(now-entry.at)<=2000)return false;
    if(active() && message_.exchange!=exchange){retire(message_.exchange,now);clear_message();}
    if(!active()) { message_.kind=Kind(f[5]);message_.role=f[6];message_.exchange=u32(f+8);message_.size=uint16_t(size);started_=now; }
    if(message_.exchange!=u32(f+8) || message_.role!=f[6] || uint8_t(message_.kind)!=f[5] || message_.size!=size) return false;
    const uint8_t bit=uint8_t(1u<<f[7]);
    if(mask_&bit) { if(!equal(message_.data+offset,f+16,length)){retire(message_.exchange,now);clear_message();}return false; }
    std::memcpy(message_.data+offset,f+16,length);mask_|=bit;
    if(mask_!=uint8_t((1u<<count)-1)) return false;
    out=message_;retire(message_.exchange,now);clear_message();return true;
  }
 private:
  void clear_message(){mask_=0;started_=0;message_={};}
  void retire(uint32_t exchange,uint32_t now){retired_[next_retired_]={exchange,now};next_retired_=(next_retired_+1)%8;}
  struct Retired {uint32_t exchange=0,at=0;};
  Retired retired_[8]{};uint8_t next_retired_=0;
  Message message_{};uint8_t mask_=0;uint32_t started_=0;
};
} // namespace orc::secure
