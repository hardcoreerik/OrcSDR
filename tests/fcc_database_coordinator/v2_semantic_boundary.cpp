#include "frequency_probe.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
using namespace orcsdr::fcc::research2;
struct Wire : Storage {
  std::vector<uint8_t> b;
  mutable size_t calls=0;
  uint64_t size() const override { return b.size(); }
  bool read(uint64_t o,uint8_t* p,size_t n) const override {
    ++calls; if(o>b.size() || n>b.size()-o) return false;
    std::memcpy(p,b.data()+o,n); return true;
  }
  void put(uint64_t n,unsigned width=8) { for(unsigned i=0;i<width;++i) b.push_back(uint8_t(n>>(i*8))); }
  void set(size_t off,uint64_t n) { for(unsigned i=0;i<8;++i) b.at(off+i)=uint8_t(n>>(i*8)); }
  void head(uint64_t exact,uint64_t ranges,uint64_t nodes) {
    for(char c:std::string("FCCFRQ02")) b.push_back(uint8_t(c));
    put(2,2); put(0,2); put(64,4); b.push_back('U'); b.push_back('S'); put(0,6);
    put(exact); put(ranges); put(nodes); put(64+exact*24); put(64+(exact+ranges)*24);
  }
  void row(uint64_t lo,uint64_t hi,uint64_t id) { put(lo);put(hi);put(id); }
};
std::vector<uint64_t> query(const Wire& w,uint64_t f) {
  Reader reader(w); assert(reader.valid()); auto c=reader.begin(f,f); std::vector<uint64_t> ids;
  for(size_t steps=0;steps<100;++steps){auto before=w.calls; auto p=reader.step(c,1); assert(w.calls-before<=1);
    assert(p.status==Status::more||p.status==Status::done); for(size_t i=0;i<p.count;++i)ids.push_back(p.ids[i]);
    if(p.status==Status::done)return ids;
  } assert(false); return {};
}
int main(){
  Wire interval;interval.head(1,1,1); interval.row(150,150,1);interval.row(100,200,2);interval.put(100);interval.put(200);interval.put(0);interval.put(1);
  assert(query(interval,175)==std::vector<uint64_t>{2});
  assert((query(interval,150)==std::vector<uint64_t>{1,2}));
  assert(query(interval,200)==std::vector<uint64_t>{2});
  std::cout<<"PASS valid control: exact+interval, interval-only and inclusive endpoint\n";
  interval.set(120,160); // leaf maximum is at 112+8; actual range still [100,200]
  assert(query(interval,175).empty());
  std::cout<<"REPRODUCED: inconsistent leaf maximum accepted; false no-match (semantic validation precondition violated)\n";
  Wire exact;exact.head(2,0,0);exact.row(100,100,1);exact.row(200,200,2);
  assert(query(exact,100)==std::vector<uint64_t>{1});
  exact.set(64,200);exact.set(72,200);exact.set(88,100);exact.set(96,100);
  assert(query(exact,100).empty());
  std::cout<<"REPRODUCED: unsorted discrete rows accepted; false no-match (semantic validation precondition violated)\n";
  std::cout<<"PASS bounded step read count; experimental reader boundary audit, not production acceptance\n";
}
