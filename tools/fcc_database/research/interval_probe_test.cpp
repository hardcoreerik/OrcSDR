#include "interval_probe.hpp"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <random>
#include <vector>
using namespace orcsdr::fcc::research;
struct Memory:Storage {
    std::vector<uint8_t> b;
    mutable size_t reads=0;
    uint64_t size() const override {return b.size();}
    bool read_at(uint64_t o,uint8_t* p,size_t n) const override {
        ++reads;
        if(o>b.size() || n>b.size()-o)return false;
        std::copy_n(b.data()+o,n,p); return true;
    }
    void put(uint64_t v,unsigned n=8){for(unsigned i=0;i<n;++i)b.push_back(uint8_t(v>>(8*i)));}
};
Memory build(const std::vector<Interval>& rows){
    Memory m; for(char c:std::string("FCCIDX01"))m.b.push_back(c);
    m.put(1,2);m.put(0,2);m.put(40,4);m.put(rows.size());m.put(40);m.put(40+32*rows.size());
    uint64_t prefix=0;
    for(auto r:rows){prefix=std::max(prefix,r.high);m.put(r.low);m.put(r.high);m.put(prefix);m.put(r.id);}return m;
}
int main(){
    std::vector<Interval> rows{{1,100000,0,1}};
    std::mt19937_64 rng(42);
    for(uint64_t i=0;i<1000;++i){uint64_t low=2+i*10;rows.push_back({low,low+rng()%50,0,i+2});}
    auto m=build(rows);IntervalProbe p(m);
    auto unready=p.begin(0,0);assert(p.step(unready,0).state==State::invalid);
    while(p.validate(17)==State::more){}
    for(size_t j=0;j<500;++j){
        uint64_t lo=rng()%11000,hi=lo+rng()%100;
        auto c=p.begin(lo,hi);std::vector<uint64_t> got,want;
        for(auto r:rows)if(r.low<=hi&&r.high>=lo)want.push_back(r.id);
        for(;;){auto before=m.reads;auto page=p.step(c,7);assert(m.reads-before<=7);assert(page.state!=State::invalid);got.insert(got.end(),page.ids.begin(),page.ids.begin()+page.count);if(page.state==State::done)break;}
        assert(got==want);
    }
    auto c=p.begin(90000,90000);auto page=p.step(c,1,true);assert(page.state==State::cancelled);
    auto broken=m;broken.b[56]^=1;IntervalProbe bad(broken);assert(bad.validate(1)==State::invalid);
    auto truncated=m;truncated.b.pop_back();IntervalProbe shortp(truncated);assert(shortp.validate(100)==State::invalid);
    auto wrong=m;wrong.b[8]=2;IntervalProbe version(wrong);assert(version.validate(1)==State::invalid);
    auto empty=build({});IntervalProbe e(empty);assert(e.validate(1)==State::done);auto ec=e.begin(0,UINT64_MAX);assert(e.step(ec,1).state==State::done);
    auto huge=build({{UINT64_MAX,UINT64_MAX,0,99}});IntervalProbe h(huge);assert(h.validate(1)==State::done);auto hc=h.begin(UINT64_MAX,UINT64_MAX);auto hp=h.step(hc,2);assert(hp.count==1&&hp.ids[0]==99);
    std::cout<<"500 randomized interval queries matched brute force; read budgets, cancellation, corruption, truncation, version, empty and uint64 limits passed\n";
}
