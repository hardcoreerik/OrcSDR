#include "frequency_probe.hpp"
#include <fstream>
#include <iostream>
#include <cassert>
#include <string>
using namespace orcsdr::fcc::research2;
struct File:Storage {mutable std::ifstream f;uint64_t n;mutable size_t reads=0;explicit File(const char* p):f(p,std::ios::binary|std::ios::ate),n(f.tellg()){assert(f);}uint64_t size()const override{return n;}bool read(uint64_t o,uint8_t* b,size_t l)const override{++reads;if(o>n||l>n-o)return false;f.clear();f.seekg(o);return bool(f.read(reinterpret_cast<char*>(b),l));}};
int main(int argc,char** argv){assert(argc==2);File f(argv[1]);Reader r(f);if(!r.valid())return 2;uint64_t lo,hi;while(std::cin>>lo>>hi){auto c=r.begin(lo,hi);size_t reads=f.reads;for(size_t steps=0;;++steps){assert(steps<1000000);auto old=f.reads;auto p=r.step(c,7);assert(f.reads-old<=7);if(p.status==Status::invalid)return 3;for(size_t i=0;i<p.count;++i)std::cout<<p.ids[i]<<',';if(p.status==Status::done)break;}std::cout<<'|'<<f.reads-reads<<'\n';}auto c=r.begin(0,UINT64_MAX);assert(r.step(c,1,true).status==Status::cancelled);}
