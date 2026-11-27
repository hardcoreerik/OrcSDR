// Compile against exact research header, not a vendored production copy.
#define main synthetic_benchmark_main
#include "benchmark.cpp"
#undef main
#include "interval_probe.hpp"
#include <cstring>
namespace rr=orcsdr::fcc::research;
struct Wire:rr::Storage{
 std::vector<uint8_t> b; mutable U calls=0,pages=0,bytes=0,last=UINT64_MAX;
 void put(U x,unsigned n){for(unsigned i=0;i<n;++i)b.push_back(uint8_t(x>>(8*i)));}
 explicit Wire(const std::vector<R>&a){for(char c:std::string("FCCIDX01"))b.push_back(uint8_t(c));put(1,2);put(0,2);put(40,4);put(a.size(),8);put(40,8);put(40+a.size()*32,8);U m=0;for(auto x:a){put(x.lo,8);put(x.hi,8);put(m=std::max(m,x.hi),8);put(x.id,8);}}
 uint64_t size()const override{return b.size();}
 bool read_at(U o,uint8_t*out,size_t n)const override{if(o>b.size()||n>b.size()-o)return false;++calls;bytes+=n;if(n)for(U p=o/4096;p<=(o+n-1)/4096;++p)if(p!=last){++pages;last=p;}std::memcpy(out,b.data()+o,n);return true;}
 void reset()const{calls=pages=bytes=0;last=UINT64_MAX;}
};
void run(bool stress){auto rows=data(100000,stress);Wire w(rows);rr::IntervalProbe reader(w);while(true){auto s=reader.validate(4096);if(s==rr::State::done)break;if(s!=rr::State::more)std::exit(2);}std::cerr<<"validated "<<rows.size()<<" rows; read_at="<<w.calls<<"\n";Engine split(rows,128,128,false);for(int mode=0;mode<(stress?1:5);++mode){auto qs=queries(stress?100:100000,100000,mode);U calls=0,pages=0,bytes=0,steps=0,maxsteps=0,result=0;std::vector<U> times;times.reserve(qs.size());for(auto q:qs){w.reset();std::vector<U> got,expected;U st=0;auto t=Clock::now();auto c=reader.begin(q.lo,q.hi);while(true){auto p=reader.step(c,256);++st;for(size_t i=0;i<p.count;++i)got.push_back(p.ids[i]);if(p.state==rr::State::done)break;if(p.state!=rr::State::more)std::exit(3);}times.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-t).count());split.query(q,[&](U id){expected.push_back(id);});std::sort(got.begin(),got.end());std::sort(expected.begin(),expected.end());if(got!=expected)std::exit(4);calls+=w.calls;pages+=w.pages;bytes+=w.bytes;steps+=st;maxsteps=std::max(maxsteps,st);result+=got.size();}std::sort(times.begin(),times.end());std::cout<<stress<<','<<mode<<','<<qs.size()<<','<<calls<<','<<pages<<','<<bytes<<','<<steps<<','<<maxsteps<<','<<result<<','<<times[(times.size()-1)/2]<<','<<times[(times.size()-1)*99/100]<<','<<times.back()<<'\n';}}
int main(){std::cout<<"stress,mode,queries,read_at_calls,logical_4k_pages,requested_bytes,steps,max_steps,result_count,p50_ns,p99_ns,max_ns\n";run(false);run(true);std::cerr<<"500100 complete result sets matched split experiment; step budget 256\n";}
