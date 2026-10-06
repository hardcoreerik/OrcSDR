#include "../src/control/secure_session.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
#include <deque>
#include <random>
#include <memory>
#include <string>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif
using namespace orc::secure;
std::vector<uint8_t> hex(const char* s){std::vector<uint8_t> v;while(*s){unsigned x;assert(std::sscanf(s,"%2x",&x)==1);v.push_back(uint8_t(x));s+=2;}return v;}
void expect(const uint8_t* p,const char* s){auto v=hex(s);assert(equal(p,v.data(),v.size()));}
void vectors(){
  // Bluetooth Core 5.4 Vol 3 Part H Appendix D; RFC 4493.
  auto k=hex("2b7e151628aed2a6abf7158809cf4f3c"),m=hex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e5130c81c46a35ce411e5fbc1191a0a52eff69f2445df4f9b17ad2b417be66c3710");uint8_t out[32];
  assert(cmac(k.data(),nullptr,0,out));expect(out,"bb1d6929e95937287fa37d129b756746");
  assert(cmac(k.data(),m.data(),16,out));expect(out,"070a16b46b4d4144f79bdd9dd04a287c");
  assert(cmac(k.data(),m.data(),40,out));expect(out,"dfa66747de9ae63030ca32611497c827");
  assert(cmac(k.data(),m.data(),64,out));expect(out,"51f0bebf7e3b9d92fc49741779363cfe");
  auto u=hex("20b003d2f297be2c5e2c83a7e9f9a5b9eff49111acf4fddbcc0301480e359de6"),v=hex("55188b3d32f6bb9a900afcfbeed4e72a59cb9ac2f19d7cfb6b4fdd49f47fc5fd"),x=hex("d5cb8454d177733effffb2ec712baeab"),y=hex("a6e8e7cc25a75f6e216583f7ff3dc4cf");
  assert(f4(u.data(),v.data(),x.data(),0,out));expect(out,"f2c916f107a9bd1cf1eda1bea974872d");
  auto w=hex("ec0234a357c8ad05341010a60a397d9b99796b13b4f866f1868d34f373bfa698"),a=hex("0056123737bfce"),b=hex("00a713702dcfc1");uint8_t mk[16],ltk[16];
  assert(f5(w.data(),x.data(),y.data(),a.data(),b.data(),mk,ltk));expect(mk,"2965f176a1084a02fd3f6a20ce636e20");expect(ltk,"6986791169d7cd23980522b594750a38");
  auto r=hex("12a3343bb453bb5408da42d20c2d0fc8"),io=hex("010102");
  assert(f6(mk,x.data(),y.data(),r.data(),io.data(),a.data(),b.data(),out));expect(out,"e3c473989cd0e8c5d26c0b09da958f61");
  uint32_t code;assert(g2(u.data(),v.data(),x.data(),y.data(),code));assert(code==uint32_t(0x2f9ed5ba)%1000000);
  // NIST SP800-38D / GCMVS: AES-256, all-zero key, IV and plaintext.
  uint8_t key[32]{},iv[12]{},plain[16]{},cipher[16],tag[16],decoded[16];
  assert(gcm(true,key,iv,nullptr,0,plain,16,cipher,tag));expect(cipher,"cea7403d4d606b6e074ec5d3baf39d18");expect(tag,"d0d1c8a799996bf0265b98b5d48ab919");
  assert(gcm(false,key,iv,nullptr,0,cipher,16,decoded,tag));tag[0]^=1;assert(!gcm(false,key,iv,nullptr,0,cipher,16,decoded,tag));
}
void fragments(){
  Message m{},got{};m.role=1;m.exchange=7;m.kind=Kind::data;m.size=192;for(int i=0;i<192;++i)m.data[i]=uint8_t(i);
  uint8_t f[4][64];for(int i=0;i<4;++i)assert(fragment(m,i,f[i]));Reassembly r;
  assert(!r.push(f[2],64,1,got));assert(!r.push(f[0],64,2,got));assert(!r.push(f[0],64,3,got));assert(!r.push(f[3],64,4,got));assert(r.push(f[1],64,5,got));assert(equal(got.data,m.data,192));
  m.exchange=70;for(int i=0;i<4;++i)assert(fragment(m,i,f[i]));
  assert(!r.push(f[0],63,10,got));f[0][4]=3;assert(!r.push(f[0],64,10,got));f[0][4]=4;
  f[0][13]=1;assert(!r.push(f[0],64,10,got));f[0][13]=0;
  assert(!r.push(f[0],64,10,got));f[0][16]^=1;assert(!r.push(f[0],64,11,got));assert(!r.active());
  m.exchange=71;for(int i=0;i<4;++i)assert(fragment(m,i,f[i]));
  assert(!r.push(f[0],64,20,got));r.expire(2021);assert(!r.active());
  // Drop one fragment, then recover immediately on the next complete exchange.
  m.size=81;m.exchange=8;assert(fragment(m,0,f[0]));assert(!r.push(f[0],64,2100,got));
  uint8_t delayed[64];assert(fragment(m,1,delayed));
  m.exchange=9;assert(fragment(m,0,f[0]));assert(fragment(m,1,f[1]));
  assert(!r.push(f[0],64,2200,got));assert(!r.push(delayed,64,2201,got));
  assert(r.push(f[1],64,2202,got));assert(got.exchange==9);
  // Changed metadata within the same exchange still cannot mix messages.
  m.exchange=10;assert(fragment(m,0,f[0]));assert(fragment(m,1,f[1]));
  assert(!r.push(f[0],64,2300,got));f[1][5]=uint8_t(Kind::proof);
  assert(!r.push(f[1],64,2301,got));f[1][5]=uint8_t(Kind::data);
  assert(r.push(f[1],64,2302,got));
  // A conflicting duplicate retires its exchange before a successor starts.
  m.exchange=11;assert(fragment(m,0,f[0]));assert(fragment(m,1,delayed));
  assert(!r.push(f[0],64,2400,got));f[0][16]^=1;assert(!r.push(f[0],64,2401,got));
  m.exchange=12;assert(fragment(m,0,f[0]));assert(fragment(m,1,f[1]));
  assert(!r.push(f[0],64,2402,got));assert(!r.push(delayed,64,2403,got));
  assert(r.push(f[1],64,2404,got));assert(got.exchange==12);


}
struct Node;
struct Flight {Node* from;uint8_t dest[6];Message m;};
std::deque<Flight> flights;
struct Node {
  uint8_t role,mac[6]{};Trust saved{};std::unique_ptr<Session> session;std::mt19937 rng;bool storage_ok=true,entropy_ok=true;int delivered=0;std::string payload;
  Node(uint8_t r):role(r),rng(r){mac[5]=r;saved.identity[15]=r;restart();}
  static int random(void* c,unsigned char* p,size_t n){auto& a=*static_cast<Node*>(c);if(!a.entropy_ok)return -1;for(size_t i=0;i<n;++i)p[i]=uint8_t(a.rng());return 0;}
  static bool send(void* c,const uint8_t* mac,const Message& m){Flight f{};f.from=static_cast<Node*>(c);std::memcpy(f.dest,mac,6);f.m=m;flights.push_back(f);return true;}
  static bool save(void* c,const Trust& t){auto& a=*static_cast<Node*>(c);if(!a.storage_ok)return false;a.saved=t;return true;}
  static void deliver(void* c,const uint8_t* p,size_t n){auto& a=*static_cast<Node*>(c);++a.delivered;a.payload.assign(reinterpret_cast<const char*>(p),n);}
  void restart(){session.reset(new Session(role,mac,saved,{this,random,send,save,deliver}));}
};
void pump(Node& a,Node& b,uint32_t now){int limit=200;while(!flights.empty()&&--limit){auto f=flights.front();flights.pop_front();Node& to=f.from==&a?b:a;if(f.dest[0]==255||equal(f.dest,to.mac,6))to.session->receive(f.from->mac,f.m,now);}assert(limit);}
void run(Node& a,Node& b,uint32_t begin,uint32_t end){for(uint32_t t=begin;t<end;t+=100){a.session->tick(t);b.session->tick(t);pump(a,b,t);}}
void pairing(Node& a,Node& b){flights.clear();assert(a.session->pair(0));assert(b.session->pair(0));run(a,b,0,2000);assert(a.session->status().state==State::verify);assert(b.session->status().state==State::verify);assert(a.session->status().code==b.session->status().code);}
void scenarios(){
  Node a(1),b(2);pairing(a,b);const auto code=a.session->status().code;
  assert(!a.session->confirm((code+1)%1000000,2000));assert(!a.saved.trusted);
  assert(a.session->confirm(code,2000));pump(a,b,2000);assert(!a.saved.trusted&&!b.saved.trusted);
  assert(b.session->confirm(code,2100));pump(a,b,2100);run(a,b,2100,4000);
  assert(a.saved.trusted&&b.saved.trusted&&equal(a.saved.secret,b.saved.secret,32));assert(a.session->status().state==State::connected&&b.session->status().state==State::connected);
  const uint8_t text[]={'t','u','n','e'};assert(a.session->send_control(text,4,4000));auto captured=flights.front();pump(a,b,4000);assert(b.delivered==1);
  b.session->receive(a.mac,captured.m,4100);assert(b.delivered==1);
  assert(a.session->send_control(text,4,4200));auto altered=flights.front();flights.clear();altered.m.data[12]^=1;b.session->receive(a.mac,altered.m,4200);assert(b.delivered==1);
  uint8_t wrong[6]={0,0,0,0,0,9};b.session->receive(wrong,captured.m,4300);assert(b.delivered==1);
  a.session->disconnect(4400);flights.clear(); // lose the first Disconnect notice
  assert(a.session->status().state==State::paused&&b.session->status().state==State::connected);
  run(a,b,4500,8000);assert(b.session->status().state==State::paused&&flights.empty());
  assert(b.session->connect(true,8000));run(a,b,8000,10000);assert(a.session->status().state==State::connected&&b.session->status().state==State::connected);
  a.session->disconnect(10100);pump(a,b,10100);assert(a.session->boot(false));a.restart();assert(a.session->status().state==State::offline);assert(b.session->connect(true,10200));run(a,b,10200,12000);assert(a.session->status().state==State::connected);
  // Offline revocation refuses old credentials after restart.
  flights.clear();assert(a.session->forget(12000));flights.clear();a.restart();b.restart();run(a,b,0,22000);assert(!a.saved.trusted&&a.session->status().state!=State::connected);
  // A failed persistent revocation blocks replacement pairing and all controls.
  Node revoke(1),trusted(2);pairing(revoke,trusted);
  revoke.session->confirm(revoke.session->status().code,2000);trusted.session->confirm(trusted.session->status().code,2000);pump(revoke,trusted,2000);run(revoke,trusted,2000,4000);
  revoke.storage_ok=false;assert(!revoke.session->forget(4000));
  assert(revoke.saved.trusted && !revoke.session->trust().trusted);
  assert(revoke.session->status().failure==Failure::storage);
  assert(!revoke.session->pair(4100));assert(!revoke.session->connect(true,4100));
  revoke.session->cancel();assert(revoke.session->status().failure==Failure::storage);
  revoke.storage_ok=true;assert(revoke.session->forget(4200));assert(!revoke.saved.trusted);
  assert(revoke.session->status().failure==Failure::none);assert(revoke.session->pair(4300));
  // Lost online Forget notices are retried with ciphertext only.
  Node forgetter(1),forgotten(2);pairing(forgetter,forgotten);
  forgetter.session->confirm(forgetter.session->status().code,2000);forgotten.session->confirm(forgotten.session->status().code,2000);pump(forgetter,forgotten,2000);run(forgetter,forgotten,2000,4000);
  assert(forgetter.session->forget(4000));flights.clear();run(forgetter,forgotten,4100,5500);
  assert(!forgetter.saved.trusted && !forgotten.saved.trusted);
  // Retain Forget ciphertext retries through immediate Pair and failed storage.
  for(bool failed_storage : {false,true}) {
    Node revoker(1),old_peer(2);pairing(revoker,old_peer);
    revoker.session->confirm(revoker.session->status().code,2000);old_peer.session->confirm(old_peer.session->status().code,2000);
    pump(revoker,old_peer,2000);run(revoker,old_peer,2000,4000);
    revoker.storage_ok=!failed_storage;assert(revoker.session->forget(4000)==!failed_storage);
    flights.clear(); // lose the first encrypted notice
    if(!failed_storage) {
      assert(revoker.session->pair(4001));
      uint8_t candidate_mac[6]={0,0,0,0,0,9};Message discover{};
      discover.role=2;discover.kind=Kind::discover;discover.exchange=99;discover.size=16;discover.data[15]=9;
      revoker.session->receive(candidate_mac,discover,4100); // changes the pairing candidate MAC
    }
    run(revoker,old_peer,4200,5500);
    assert(!old_peer.saved.trusted);
    if(failed_storage)assert(revoker.session->status().failure==Failure::storage);
  }
  // Failure to generate Pair keys must not cancel the prior Forget notification.
  // Disconnect after a lost Forget notice must not cancel revocation delivery.
  Node forgotten_then_stopped(1),stale_peer(2);pairing(forgotten_then_stopped,stale_peer);
  forgotten_then_stopped.session->confirm(forgotten_then_stopped.session->status().code,2000);
  stale_peer.session->confirm(stale_peer.session->status().code,2000);
  pump(forgotten_then_stopped,stale_peer,2000);run(forgotten_then_stopped,stale_peer,2000,4000);
  assert(forgotten_then_stopped.session->forget(4000));flights.clear();
  forgotten_then_stopped.session->disconnect(4100);
  run(forgotten_then_stopped,stale_peer,4200,6000);
  assert(!stale_peer.saved.trusted);
  Node keyfail(1),notify_peer(2);pairing(keyfail,notify_peer);
  keyfail.session->confirm(keyfail.session->status().code,2000);notify_peer.session->confirm(notify_peer.session->status().code,2000);
  pump(keyfail,notify_peer,2000);run(keyfail,notify_peer,2000,4000);
  assert(keyfail.session->forget(4000));flights.clear();keyfail.entropy_ok=false;
  assert(!keyfail.session->pair(4001));run(keyfail,notify_peer,4200,5500);assert(!notify_peer.saved.trusted);
  // Once already disconnected, Forget must not send an obsolete Disconnect notice.
  Node offline(1),still_trusted(2);pairing(offline,still_trusted);
  offline.session->confirm(offline.session->status().code,2000);still_trusted.session->confirm(still_trusted.session->status().code,2000);
  pump(offline,still_trusted,2000);run(offline,still_trusted,2000,4000);
  offline.session->disconnect(4000);flights.clear();assert(offline.session->forget(4001));
  offline.session->tick(4500);assert(flights.empty());assert(!offline.saved.trusted);
  // A lost Forget frame recovers even when new pairing discovery arrives first.
  Node partial(1),receiver(2);pairing(partial,receiver);
  partial.session->confirm(partial.session->status().code,2000);receiver.session->confirm(receiver.session->status().code,2000);
  pump(partial,receiver,2000);run(partial,receiver,2000,4000);
  assert(partial.session->forget(4000));const Message first_notice=flights.front().m;flights.clear();
  Reassembly receiver_assembly;Message assembled{};uint8_t frame[64];
  assert(first_notice.size<=fragment_payload); // teardown notices occupy one frame
  // Drop that whole frame; discovery is the first one the receiver sees.
  assert(partial.session->pair(4001));partial.session->tick(4001);
  const Message discovery=flights.front().m;flights.clear();assert(fragment(discovery,0,frame));
  assert(receiver_assembly.push(frame,64,4001,assembled));receiver.session->receive(partial.mac,assembled,4001);
  partial.session->tick(4500);const Message retry_notice=flights.front().m;flights.clear();
  assert(retry_notice.exchange==first_notice.exchange && retry_notice.size==first_notice.size);
  assert(equal(retry_notice.data,first_notice.data,first_notice.size));
  assert(fragment(retry_notice,0,frame));assert(receiver_assembly.push(frame,64,4501,assembled));
  receiver.session->receive(partial.mac,assembled,4501);assert(!receiver.saved.trusted);flights.clear();
  Node c(1),d(2);pairing(c,d);c.session->cancel();run(c,d,2000,62000);assert(!c.saved.trusted&&!d.saved.trusted);
  Node e(1),f(2);pairing(e,f);e.restart();run(e,f,2000,62000);assert(!e.saved.trusted&&!f.saved.trusted);
  Node g(1),h(2);pairing(g,h);g.storage_ok=false;assert(g.session->confirm(g.session->status().code,2000));assert(h.session->confirm(h.session->status().code,2000));pump(g,h,2000);assert(g.session->status().state==State::failed);assert(!g.saved.trusted);run(g,h,2000,24000);assert(h.session->status().state!=State::connected);
  Node invalid(1),remote(2);flights.clear();invalid.session->pair(0);
  Message discover{};discover.kind=Kind::discover;discover.role=2;discover.exchange=1;discover.size=16;std::memcpy(discover.data,remote.saved.identity,16);
  invalid.session->receive(remote.mac,discover,100);
  Message badkey{};badkey.kind=Kind::public_key;badkey.role=2;badkey.exchange=2;badkey.size=81;std::memcpy(badkey.data,remote.saved.identity,16);badkey.data[16]=4;
  invalid.session->receive(remote.mac,badkey,200);assert(invalid.session->status().failure==Failure::invalid_key&&!invalid.saved.trusted);
  // One-sided Pair never discovers or trusts the idle peer.
  Node alone(1),idle(2);flights.clear();alone.session->pair(0);run(alone,idle,0,61000);assert(alone.session->status().failure==Failure::timeout&&!idle.saved.trusted);
  // Drop the first flight of each kind, and duplicate subsequent flights.
  Node lossy(1),other(2);flights.clear();lossy.session->pair(0);other.session->pair(0);bool dropped[9]{};
  for(uint32_t t=0;t<5000;t+=100) {
    lossy.session->tick(t);other.session->tick(t);int bounded=200;
    while(!flights.empty()&&--bounded){auto f=flights.front();flights.pop_front();if(!dropped[uint8_t(f.m.kind)]){dropped[uint8_t(f.m.kind)]=true;continue;}
      Node& to=f.from==&lossy?other:lossy;to.session->receive(f.from->mac,f.m,t);to.session->receive(f.from->mac,f.m,t);
    }assert(bounded);
  }
  assert(lossy.session->status().state==State::verify&&other.session->status().state==State::verify);
  const auto matching=lossy.session->status().code;assert(other.session->status().code==matching);
  lossy.session->confirm(matching,5000);other.session->confirm(matching,5000);pump(lossy,other,5000);run(lossy,other,5000,7000);
  assert(lossy.session->status().state==State::connected&&other.session->status().state==State::connected);
  lossy.session->send_control(text,4,7100);const auto old=flights.front();pump(lossy,other,7100);
  lossy.restart();other.restart();run(lossy,other,0,3000);assert(lossy.session->status().state==State::connected&&other.session->status().state==State::connected);
  const int before=other.delivered;other.session->receive(lossy.mac,old.m,3000);assert(other.delivered==before);
}
int main(){
#ifdef _MSC_VER
  _set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
  _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
#endif
  vectors();fragments();scenarios();std::puts("secure pairing, vectors, fragments, trust, replay and revocation: PASS");}
