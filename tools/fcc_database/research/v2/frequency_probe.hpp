#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace orcsdr::fcc::research2 {
struct Storage { virtual ~Storage()=default; virtual uint64_t size() const=0; virtual bool read(uint64_t,uint8_t*,size_t) const=0; };
enum class Status { more, done, invalid, cancelled };
struct Page { std::array<uint64_t,16> ids{}; size_t count=0; Status status=Status::invalid; };
// Prototype: caller must authenticate and semantically validate immutable shard before opening.
class Reader {
    const Storage& s; uint64_t exact=0,ranges=0,nodes=0,roff=0,noff=0,leaves=0; bool ok=false;
    static uint64_t le(const uint8_t* p,size_t n=8){uint64_t v=0;for(size_t i=0;i<n;++i)v|=uint64_t(p[i])<<(8*i);return v;}
    bool entry(uint64_t base,uint64_t i,uint64_t& lo,uint64_t& hi,uint64_t& id) const {uint8_t b[24];if(!s.read(base+i*24,b,24))return false;lo=le(b);hi=le(b+8);id=le(b+16);return lo<=hi&&id!=0;}
public:
    struct Cursor { uint64_t low=0,high=0,l=0,r=0,pos=0,end=0; std::array<uint64_t,64> stack{}; size_t depth=0; unsigned phase=0; const Reader* owner=nullptr; };
    explicit Reader(const Storage& store):s(store){
        uint8_t h[64];if(s.size()<64||s.size()>(1ULL<<30)||!s.read(0,h,64))return;
        const char* magic="FCCFRQ02";for(size_t i=0;i<8;++i)if(h[i]!=uint8_t(magic[i]))return;
        if(le(h+8,2)!=2||le(h+10,2)!=0||le(h+12,4)!=64)return;
        for(size_t i=16;i<18;++i)if(h[i]<'A'||h[i]>'Z')return;
        for(size_t i=18;i<24;++i)if(h[i])return;
        exact=le(h+24);ranges=le(h+32);nodes=le(h+40);roff=le(h+48);noff=le(h+56);
        if(exact>s.size()/24||ranges>s.size()/24||nodes>s.size()/32)return;
        if(roff!=64+exact*24||noff!=roff+ranges*24||s.size()!=noff+nodes*32)return;
        uint64_t blocks=(ranges+31)/32;if(blocks){leaves=1;while(leaves<blocks)leaves*=2;}
        if(nodes!=(leaves?2*leaves-1:0))return;
        ok=true;
    }
    bool valid()const{return ok;}
    Cursor begin(uint64_t lo,uint64_t hi)const{Cursor c;c.low=lo;c.high=hi;c.r=exact;c.owner=this;return c;}
    Page step(Cursor& c,size_t budget,bool cancel=false)const{
        Page p;if(!ok||c.owner!=this||c.low>c.high||c.l>c.r||c.r>exact||c.depth>64||c.pos>c.end||c.end>ranges||c.phase>4)return p;
        if(cancel){c.phase=4;p.status=Status::cancelled;return p;}
        while(budget&&p.count<16){
            uint64_t lo,hi,id;
            if(c.phase==0){if(c.l==c.r){c.phase=1;continue;}auto mid=c.l+(c.r-c.l)/2;--budget;if(!entry(64,mid,lo,hi,id)||lo!=hi)return Page{};if(lo<c.low)c.l=mid+1;else c.r=mid;}
            else if(c.phase==1){if(c.l==exact){c.phase=2;if(nodes)c.stack[c.depth++]=0;continue;}--budget;if(!entry(64,c.l,lo,hi,id)||lo!=hi)return Page{};if(lo>c.high){c.phase=2;if(nodes)c.stack[c.depth++]=0;continue;}++c.l;c.r=c.l;p.ids[p.count++]=id;}
            else if(c.phase==2){if(!c.depth){c.phase=4;break;}uint64_t i=c.stack[--c.depth];uint8_t b[32];--budget;if(i>=nodes||!s.read(noff+i*32,b,32))return Page{};lo=le(b);hi=le(b+8);uint64_t start=le(b+16),count=le(b+24);
                if(i<leaves-1){if(start||count)return Page{};if(lo<=c.high&&hi>=c.low&&lo<=hi){if(c.depth>62)return Page{};c.stack[c.depth++]=2*i+2;c.stack[c.depth++]=2*i+1;}}
                else {uint64_t want=(i-(leaves-1))*32;if(want>=ranges){if(start||count||lo!=UINT64_MAX||hi)return Page{};continue;}uint64_t n=ranges-want<32?ranges-want:32;if(start!=want||count!=n||lo>hi)return Page{};if(lo<=c.high&&hi>=c.low){c.pos=start;c.end=start+count;c.phase=3;}}
            }
            else if(c.phase==3){if(c.pos==c.end){c.phase=2;continue;}--budget;if(!entry(roff,c.pos++,lo,hi,id)||lo==hi)return Page{};if(lo<=c.high&&hi>=c.low)p.ids[p.count++]=id;}
            else break;
        }
        p.status=c.phase==4?Status::done:Status::more;return p;
    }
};
}
