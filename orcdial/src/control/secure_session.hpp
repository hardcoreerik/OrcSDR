#pragma once
#include "secure_crypto.hpp"

namespace orc::secure {
// Every field in the store has a fixed byte encoding; the store adapter commits
// this as ONE NVS blob and reads it back before authorizing a session.
struct Trust {
  uint8_t identity[16]{}, peer_identity[16]{}, peer_mac[6]{}, secret[32]{};
  bool trusted=false,boot_connect=false,upgrade=false;
};
enum class State : uint8_t { offline, searching, exchanging, verify, connecting, connected, paused, failed };
enum class Failure : uint8_t { none, timeout, rejected, invalid_key, commitment, authentication, storage, transport, upgrade_required };
struct Status { State state=State::offline;Failure failure=Failure::none;bool trusted=false,boot_connect=false,upgrade=false,channel_locked=false;uint32_t code=0;uint8_t identity[16]{},peer_identity[16]{}; };
inline const char* state_name(State s) {
  switch(s){case State::searching:return "Searching";case State::exchanging:return "Exchanging keys";
    case State::verify:return "Compare code";case State::connecting:return "Connecting";
    case State::connected:return "Connected";case State::paused:return "Disconnected";
    case State::failed:return "Failed";default:return "Offline";}
}
inline const char* failure_name(Failure f) {
  switch(f){case Failure::timeout:return "Timed out";case Failure::rejected:return "Cancelled";
    case Failure::invalid_key:return "Invalid key";case Failure::commitment:return "Code exchange failed";
    case Failure::authentication:return "Authentication failed";case Failure::storage:return "Storage failed";
    case Failure::transport:return "Transport unavailable";case Failure::upgrade_required:return "Pairing upgrade required";default:return "None";}
}
struct Hooks {
  void* context=nullptr;
  int (*random)(void*,unsigned char*,size_t)=nullptr;
  bool (*send)(void*,const uint8_t*,const Message&)=nullptr;
  bool (*save)(void*,const Trust&)=nullptr;
  void (*deliver)(void*,const uint8_t*,size_t)=nullptr;
};
class Session {
 public:
  Session(uint8_t role,const uint8_t mac[6],const Trust& trust,Hooks hooks):role_(role),trust_(trust),hooks_(hooks) {
    std::memcpy(mac_,mac,6);status_.upgrade=trust.upgrade;
    if(trust.upgrade)status_.failure=Failure::upgrade_required;
    if(trust.trusted && trust.boot_connect)connect(false,0);
  }
  ~Session() {clear();wipe(&trust_,sizeof trust_);}
  const Trust& trust() const {return trust_;}
  Status status() const {
    Status s=status_;s.trusted=trust_.trusted;s.boot_connect=trust_.boot_connect;s.upgrade=trust_.upgrade;s.channel_locked=have_offer_;
    std::memcpy(s.identity,trust_.identity,16);std::memcpy(s.peer_identity,trust_.peer_identity,16);return s;
  }
  bool pair(uint32_t now) {
    if(trust_.trusted || revocation_failed_) return false; // Failed deletion must be retried, never hidden by pairing.
    clear(true);paused_=false;status_.state=State::searching;status_.failure=Failure::none;
    deadline_=now+60000;last_send_=now-1200;attempts_=0;
    if(!key_.generate(hooks_.random,hooks_.context,public_) || hooks_.random(hooks_.context,nonce_,16))return fail(Failure::invalid_key);
    return true;
  }
  void cancel() {clear(true);status_.state=revocation_failed_?State::failed:paused_?State::paused:State::offline;status_.failure=revocation_failed_?Failure::storage:Failure::rejected;}
  bool confirm(uint32_t code,uint32_t now) {
    if(status_.state!=State::verify || code!=status_.code || expired(now,deadline_))return false;
    approved_=true;return send_approval(now);
  }
  bool boot(bool enabled) {
    Trust next=trust_;next.boot_connect=enabled;if(!hooks_.save(hooks_.context,next))return fail(Failure::storage);
    trust_=next;return true;
  }
  bool connect(bool explicit_request,uint32_t now) {
    if(!trust_.trusted || revocation_failed_ || (paused_ && !explicit_request))return false;
    clear();status_.state=State::connecting;status_.failure=Failure::none;explicit_=explicit_request;
    std::memcpy(peer_mac_,trust_.peer_mac,6);std::memcpy(peer_id_,trust_.peer_identity,16);
    if(hooks_.random(hooks_.context,nonce_,16))return fail(Failure::authentication);
    deadline_=now+20000;last_send_=now-1200;attempts_=0;return true;
  }
  void disconnect(uint32_t now) {
    Message notice{};
    if(status_.state==State::connected) {
      capture_notice_=true;const uint8_t command=1;send_data(&command,1,now);notice=cached_notice_;
    }
    clear(notice.size!=0);paused_=true;status_.state=State::paused;notice_is_forget_=false;
    // Retry the already encrypted final notice, retaining no session keys.
    if(notice.size){cached_notice_=notice;notice_retries_=2;notice_at_=now;}
  }
  bool forget(uint32_t now) {
    Message notice{};
    if(status_.state==State::connected){capture_notice_=true;const uint8_t command=2;send_data(&command,1,now);notice=cached_notice_;}
    if(notice.size)notice_is_forget_=true;
    clear(notice.size!=0 || notice_is_forget_);paused_=true;status_.state=State::paused;
    if(notice.size){cached_notice_=notice;notice_retries_=2;notice_at_=now;}
    Trust next=trust_;next.trusted=false;next.boot_connect=false;next.upgrade=false;
    wipe(next.secret,32);wipe(next.peer_identity,16);wipe(next.peer_mac,6);
    // On storage failure stay stopped; the old credential is never used in this boot.
    const bool saved=hooks_.save(hooks_.context,next);trust_=next;revocation_failed_=!saved;
    if(!saved)return fail(Failure::storage);
    status_.failure=Failure::none;
    return true;
  }
  bool send_control(const uint8_t* p,size_t n,uint32_t now) {
    if(n>128 || !p)return false;
    uint8_t payload[129]{0};std::memcpy(payload+1,p,n);return send_data(payload,n+1,now);
  }
  void tick(uint32_t now) {
    if(notice_retries_ && uint32_t(now-notice_at_)>=350) {
      hooks_.send(hooks_.context,notice_mac_,cached_notice_);--notice_retries_;notice_at_=now;
    }
    if(status_.state==State::connected) {
      if(uint32_t(now-last_rx_)>5000) {connect(false,now);return;}
      if(uint32_t(now-last_send_)>=1500){const uint8_t ping=3;send_data(&ping,1,now);}return;
    }
    if(status_.state==State::offline || status_.state==State::paused || status_.state==State::failed)return;
    if(expired(now,deadline_)) {fail(Failure::timeout);return;}
    if(uint32_t(now-last_send_)<1200)return;
    if(status_.state==State::connecting && ++attempts_>12){fail(Failure::timeout);return;}
    if(status_.state==State::searching) {
      uint8_t body[16];std::memcpy(body,trust_.identity,16);emit(Kind::discover,body,16,broadcast_,now);return;
    }
    if(status_.state==State::connecting) {
      if(pair_receipt_ && !have_offer_)emit(Kind::approval,pair_receipt_data_,48,peer_mac_,now);
      send_offer(now);if(have_offer_)send_proof(now);return;
    }
    // Resend each prerequisite, so a lost/reordered flight cannot deadlock pairing.
    emit(Kind::discover,trust_.identity,16,broadcast_,now);send_public(now);
    if(have_public_)send_commit(now);
    if(have_commit_)emit(Kind::nonce,nonce_,16,peer_mac_,now);
    if(approved_)send_approval(now);
  }
  void receive(const uint8_t mac[6],const Message& m,uint32_t now) {
    if(m.role!=3-role_ || !m.exchange)return;
    if(m.kind==Kind::data) {receive_data(mac,m,now);return;}
    if(m.kind==Kind::offer) {receive_offer(mac,m,now);return;}
    if(m.kind==Kind::proof) {receive_proof(mac,m,now);return;}
    const bool pairing=status_.state==State::searching || status_.state==State::exchanging || status_.state==State::verify;
    if(!pairing || expired(now,deadline_))return;
    if(m.kind==Kind::discover) {
      if(m.size!=16)return;
      if(status_.state==State::searching){std::memcpy(peer_mac_,mac,6);std::memcpy(peer_id_,m.data,16);status_.state=State::exchanging;}
      if(equal(peer_mac_,mac,6)&&equal(peer_id_,m.data,16))send_public(now);
      return;
    }
    if(!equal(peer_mac_,mac,6))return;
    if(m.kind==Kind::public_key && m.size==81 && equal(m.data,peer_id_,16)) {
      if(have_public_) {if(!equal(peer_public_,m.data+16,65))fail(Failure::invalid_key);return;}
      if(!key_.derive(m.data+16,hooks_.random,hooks_.context,dh_)){fail(Failure::invalid_key);return;}
      std::memcpy(peer_public_,m.data+16,65);have_public_=true;send_public(now);send_commit(now);return;
    }
    if(m.kind==Kind::commitment && m.size==16 && have_public_) {
      if(have_commit_&&!equal(commit_,m.data,16)){fail(Failure::commitment);return;}
      if(have_commit_){emit(Kind::nonce,nonce_,16,peer_mac_,now);return;}
      std::memcpy(commit_,m.data,16);have_commit_=true;send_commit(now);emit(Kind::nonce,nonce_,16,peer_mac_,now);return;
    }
    if(m.kind==Kind::nonce && m.size==16 && have_commit_) {
      if(have_nonce_) {if(!equal(peer_nonce_,m.data,16))fail(Failure::commitment);return;}
      uint8_t check[16];if(!f4(peer_public_+1,public_+1,m.data,0,check)||!equal(check,commit_,16)){fail(Failure::commitment);return;}
      std::memcpy(peer_nonce_,m.data,16);have_nonce_=true;
      if(!pair_keys()){fail(Failure::authentication);return;}
      key_.clear();wipe(dh_,32);status_.state=State::verify;return;
    }
    if(m.kind==Kind::approval && m.size==48 && have_nonce_) {
      uint8_t expected[48];if(!approval(3-role_,expected)||!equal(expected,m.data,48)){fail(Failure::authentication);return;}
      peer_approved_=true;if(approved_)finish_pair(now);return;
    }
  }
 private:
  static bool expired(uint32_t now,uint32_t deadline){return int32_t(now-deadline)>=0;}
  bool emit(Kind kind,const uint8_t* p,size_t n,const uint8_t* dest,uint32_t now) {
    Message m{};m.role=role_;m.kind=kind;m.size=uint16_t(n);
    do {if(hooks_.random(hooks_.context,reinterpret_cast<uint8_t*>(&m.exchange),4))return fail(Failure::authentication);}while(!m.exchange);
    std::memcpy(m.data,p,n);last_send_=now;
    if(capture_notice_ && kind==Kind::data){cached_notice_=m;std::memcpy(notice_mac_,dest,6);}
    return hooks_.send(hooks_.context,dest,m);
  }
  void clear(bool keep_notice=false) {
    capture_notice_=false;
    if(!keep_notice){notice_retries_=0;cached_notice_={};notice_is_forget_=false;wipe(notice_mac_,6);}
    proof_sent_=false;proof_sent_at_=0;
    pair_receipt_=false;wipe(pair_receipt_data_,48);
    key_.clear();wipe(dh_,32);wipe(mac_key_,16);wipe(ltk_,16);wipe(pair_secret_,32);
    wipe(tx_key_,32);wipe(rx_key_,32);wipe(session_,8);wipe(nonce_,16);wipe(peer_nonce_,16);
    have_public_=have_commit_=have_nonce_=approved_=peer_approved_=have_offer_=false;
    tx_sequence_=rx_sequence_=0;status_.code=0;
  }
  bool fail(Failure f) {clear(true);status_.state=State::failed;status_.failure=f;return false;}
  const uint8_t* first(const uint8_t* local,const uint8_t* peer)const{return role_==1?local:peer;}
  const uint8_t* second(const uint8_t* local,const uint8_t* peer)const{return role_==1?peer:local;}
  bool send_public(uint32_t now) {uint8_t p[81];std::memcpy(p,trust_.identity,16);std::memcpy(p+16,public_,65);return emit(Kind::public_key,p,81,peer_mac_,now);}
  bool send_commit(uint32_t now) {uint8_t p[16];return f4(public_+1,peer_public_+1,nonce_,0,p)&&emit(Kind::commitment,p,16,peer_mac_,now);}
  // Full transcript supplements the standard Bluetooth functions with OrcDial
  // identities, protocol and roles. It is not a claim of Bluetooth compliance.
  size_t transcript(uint8_t* p)const {
    std::memcpy(p,"OrcDial4",8);p[8]=wire_version;p[9]=1;p[10]=2;
    std::memcpy(p+11,first(trust_.identity,peer_id_),16);std::memcpy(p+27,second(trust_.identity,peer_id_),16);
    std::memcpy(p+43,first(mac_,peer_mac_),6);std::memcpy(p+49,second(mac_,peer_mac_),6);
    std::memcpy(p+55,first(public_,peer_public_),65);std::memcpy(p+120,second(public_,peer_public_),65);
    std::memcpy(p+185,first(nonce_,peer_nonce_),16);std::memcpy(p+201,second(nonce_,peer_nonce_),16);return 217;
  }
  bool pair_keys() {
    uint8_t a[7]{},b[7]{},t[217];std::memcpy(a+1,first(mac_,peer_mac_),6);std::memcpy(b+1,second(mac_,peer_mac_),6);
    if(!f5(dh_,first(nonce_,peer_nonce_),second(nonce_,peer_nonce_),a,b,mac_key_,ltk_) ||
       !g2(first(public_,peer_public_)+1,second(public_,peer_public_)+1,first(nonce_,peer_nonce_),second(nonce_,peer_nonce_),status_.code))return false;
    return hmac(ltk_,16,t,transcript(t),pair_secret_);
  }
  bool approval(uint8_t sender,uint8_t out[48])const {
    const bool own=sender==role_;uint8_t a[7]{},b[7]{},r[16]{},io[3]={wire_version,sender,1},t[234];
    std::memcpy(a+1,own?mac_:peer_mac_,6);std::memcpy(b+1,own?peer_mac_:mac_,6);
    if(!f6(mac_key_,own?nonce_:peer_nonce_,own?peer_nonce_:nonce_,r,io,a,b,out))return false;
    const size_t n=transcript(t);t[n]=sender;std::memcpy(t+n+1,out,16);return hmac(pair_secret_,32,t,n+17,out+16);
  }
  bool send_approval(uint32_t now) {
    uint8_t out[48];if(!approval(role_,out))return fail(Failure::authentication);
    const bool sent=emit(Kind::approval,out,48,peer_mac_,now);if(peer_approved_)finish_pair(now);return sent;
  }
  void finish_pair(uint32_t now) {
    Trust next=trust_;next.trusted=true;next.upgrade=false;next.boot_connect=true;
    std::memcpy(next.peer_identity,peer_id_,16);std::memcpy(next.peer_mac,peer_mac_,6);std::memcpy(next.secret,pair_secret_,32);
    if(!hooks_.save(hooks_.context,next)){fail(Failure::storage);return;}
    uint8_t receipt[48];approval(role_,receipt);trust_=next;connect(true,now);
    std::memcpy(pair_receipt_data_,receipt,48);pair_receipt_=true;wipe(receipt,48);
  }
  bool send_offer(uint32_t now) {
    uint8_t p[81];std::memcpy(p,trust_.identity,16);std::memcpy(p+16,trust_.peer_identity,16);
    std::memcpy(p+32,nonce_,16);p[48]=explicit_?1:0;
    uint8_t bound[90];std::memcpy(bound,"offer4",6);bound[6]=role_;bound[7]=wire_version;
    std::memcpy(bound+8,p,49);
    if(!hmac(trust_.secret,32,bound,57,p+49))return fail(Failure::authentication);
    return emit(Kind::offer,p,sizeof p,trust_.peer_mac,now);
  }
  bool session_keys() {
    uint8_t t[83];std::memcpy(t,"session4",8);t[8]=wire_version;t[9]=1;t[10]=2;
    std::memcpy(t+11,first(trust_.identity,peer_id_),16);std::memcpy(t+27,second(trust_.identity,peer_id_),16);
    std::memcpy(t+43,first(nonce_,peer_nonce_),16);std::memcpy(t+59,second(nonce_,peer_nonce_),16);
    t[75]=role_==1?explicit_:peer_explicit_;t[76]=role_==1?peer_explicit_:explicit_;
    uint8_t root[32];if(!hmac(trust_.secret,32,t,77,root))return false;
    t[77]=role_;bool ok=hmac(root,32,t,78,tx_key_);t[77]=3-role_;ok=ok&&hmac(root,32,t,78,rx_key_);
    std::memcpy(session_,root,8);wipe(root,32);return ok;
  }
  void receive_offer(const uint8_t mac[6],const Message& m,uint32_t now) {
    if(!trust_.trusted || m.size!=81 || !equal(mac,trust_.peer_mac,6) ||
       !equal(m.data,trust_.peer_identity,16)||!equal(m.data+16,trust_.identity,16)||m.data[48]>1)return;
    uint8_t bound[57],check[32];std::memcpy(bound,"offer4",6);bound[6]=m.role;bound[7]=wire_version;std::memcpy(bound+8,m.data,49);
    if(!hmac(trust_.secret,32,bound,sizeof bound,check)||!equal(check,m.data+49,32))return;
    if(status_.state==State::connected) {
      if(equal(peer_nonce_,m.data+32,16))send_proof(now);
      return; // Do not let a replay interrupt a live session.
    }
    if(status_.state!=State::connecting) {
      if(paused_ && !m.data[48])return;
      // Keep manual stop until a fresh reciprocal proof authenticates this request.
      const bool stopped=paused_;paused_=false;connect(false,now);paused_=stopped;
    }
    if(have_offer_ && equal(peer_nonce_,m.data+32,16) && peer_explicit_==bool(m.data[48])){send_proof(now);return;}
    std::memcpy(peer_nonce_,m.data+32,16);peer_explicit_=m.data[48];have_offer_=true;
    if(!session_keys()){fail(Failure::authentication);return;}send_offer(now);send_proof(now);
  }
  bool send_proof(uint32_t now) {
    // Captured offers can be replayed. Do not turn them into an unbounded RPC
    // response flood; one proof per retry interval is enough for lost flights.
    if(proof_sent_ && uint32_t(now-proof_sent_at_)<1000)return true;
    uint8_t p[40];std::memcpy(p,session_,8);
    if(!hmac(tx_key_,32,p,8,p+8))return fail(Failure::authentication);
    proof_sent_=true;proof_sent_at_=now;return emit(Kind::proof,p,40,peer_mac_,now);
  }
  void receive_proof(const uint8_t mac[6],const Message& m,uint32_t now) {
    if((status_.state!=State::connecting && status_.state!=State::connected)||!have_offer_||m.size!=40||!equal(mac,peer_mac_,6)||!equal(m.data,session_,8))return;
    uint8_t check[32];if(!hmac(rx_key_,32,m.data,8,check)||!equal(check,m.data+8,32))return;
    if(paused_ && !peer_explicit_ && !explicit_)return;
    if(status_.state==State::connecting){send_proof(now);status_.state=State::connected;paused_=false;last_rx_=now;tx_sequence_=rx_sequence_=0;}
  }
  bool send_data(const uint8_t* p,size_t n,uint32_t now) {
    if(status_.state!=State::connected || n>129 || tx_sequence_==0xffffffffu)return false;
    uint8_t out[157],iv[12],aad[16];std::memcpy(out,session_,8);u32(out+8,++tx_sequence_);
    std::memcpy(iv,out,12);std::memcpy(aad,"ODS4",4);aad[4]=role_;aad[5]=wire_version;
    aad[6]=uint8_t(n);aad[7]=0;std::memcpy(aad+8,session_,8);
    if(!gcm(true,tx_key_,iv,aad,sizeof aad,p,n,out+12,out+12+n))return fail(Failure::authentication);
    return emit(Kind::data,out,n+28,peer_mac_,now);
  }
  void receive_data(const uint8_t mac[6],const Message& m,uint32_t now) {
    if(status_.state!=State::connected || m.size<29 || m.size>157 || !equal(mac,peer_mac_,6)||!equal(m.data,session_,8))return;
    const uint32_t seq=u32(m.data+8);if(!seq || seq<=rx_sequence_)return;
    const size_t n=m.size-28;uint8_t out[129],tag[16],aad[16];std::memcpy(tag,m.data+12+n,16);
    std::memcpy(aad,"ODS4",4);aad[4]=m.role;aad[5]=wire_version;aad[6]=uint8_t(n);aad[7]=0;std::memcpy(aad+8,session_,8);
    if(!gcm(false,rx_key_,m.data,aad,sizeof aad,m.data+12,n,out,tag))return;
    rx_sequence_=seq;last_rx_=now;
    if(out[0]==0)hooks_.deliver(hooks_.context,out+1,n-1);
    else if(n==1 && out[0]==1){clear();paused_=true;status_.state=State::paused;}
    else if(n==1 && out[0]==2)forget(now);
  }
  bool revocation_failed_=false;
  uint8_t role_,mac_[6]{},peer_mac_[6]{},peer_id_[16]{};Trust trust_;Hooks hooks_;Status status_{};KeyAgreement key_;
  uint8_t public_[65]{},peer_public_[65]{},nonce_[16]{},peer_nonce_[16]{},dh_[32]{},commit_[16]{},mac_key_[16]{},ltk_[16]{},pair_secret_[32]{};
  uint8_t tx_key_[32]{},rx_key_[32]{},session_[8]{};
  uint8_t pair_receipt_data_[48]{};
  bool notice_is_forget_=false;
  uint8_t notice_mac_[6]{};
  Message cached_notice_{};bool capture_notice_=false;uint8_t notice_retries_=0;uint32_t notice_at_=0;
  bool proof_sent_=false;uint32_t proof_sent_at_=0;
  bool pair_receipt_=false,paused_=false,explicit_=false,peer_explicit_=false,have_public_=false,have_commit_=false,have_nonce_=false,approved_=false,peer_approved_=false,have_offer_=false;
  uint32_t deadline_=0,last_send_=0,last_rx_=0,tx_sequence_=0,rx_sequence_=0;uint8_t attempts_=0;
  const uint8_t broadcast_[6]={255,255,255,255,255,255};
};
} // namespace orc::secure
