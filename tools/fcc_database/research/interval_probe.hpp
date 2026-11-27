#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

// Experimental index ONLY: not the coordinator-approved national package ABI.
namespace orcsdr::fcc::research {
struct Storage {
    virtual ~Storage() = default;
    virtual uint64_t size() const = 0;
    virtual bool read_at(uint64_t offset, uint8_t* out, size_t length) const = 0;
};
struct Interval { uint64_t low, high, prefix_max, id; };
enum class State { more, done, invalid, cancelled };
struct Page {
    std::array<uint64_t, 16> ids{};
    size_t count = 0;
    State state = State::invalid;
};
// The caller must pin immutable storage throughout validation and query.
// A cursor belongs to exactly one reader and query. No shared async ABI here.
class IntervalProbe {
    const Storage& s_;
    uint64_t n_ = 0, validated_ = 0, prev_low_ = 0, max_high_ = 0;
    bool header_ok_ = false, valid_ = false, failed_ = false;
    static uint64_t le(const uint8_t* p, unsigned n) {
        uint64_t v=0;
        for (unsigned i=0;i<n;++i) v |= uint64_t(p[i]) << (8*i);
        return v;
    }
    bool record(uint64_t i, Interval& r) const {
        if(i>=n_) return false;
        uint8_t b[32];
        if(!s_.read_at(40+i*32,b,sizeof b)) return false;
        r={le(b,8),le(b+8,8),le(b+16,8),le(b+24,8)};
        return r.low<=r.high && r.high<=r.prefix_max;
    }
public:
    struct Cursor {
        uint64_t low=0, high=0, left=0, right=0, next=0;
        const IntervalProbe* owner=nullptr;
        bool search=true, finished=false;
    };
    explicit IntervalProbe(const Storage& s):s_(s) {
        uint8_t h[40];
        constexpr uint8_t magic[8]={'F','C','C','I','D','X','0','1'};
        if(s_.size()<sizeof h || !s_.read_at(0,h,sizeof h)) return;
        for(size_t i=0;i<8;++i) if(h[i]!=magic[i]) return;
        if(le(h+8,2)!=1 || le(h+10,2)!=0 || le(h+12,4)!=40 || le(h+24,8)!=40) return;
        n_=le(h+16,8);
        if(n_>(std::numeric_limits<uint64_t>::max()-40)/32) return;
        if(le(h+32,8)!=s_.size() || 40+n_*32!=s_.size()) return;
        header_ok_=true;
    }
    // Incremental O(N) verification; no query until complete. Budget is records.
    State validate(size_t budget) {
        if(!header_ok_ || failed_) return State::invalid;
        while(validated_<n_ && budget) {
            --budget;
            Interval r{};
            if(!record(validated_,r) || (validated_ && r.low<prev_low_) ||
               r.prefix_max!=(r.high>max_high_?r.high:max_high_)) {
                failed_=true; return State::invalid;
            }
            prev_low_=r.low; max_high_=r.prefix_max; ++validated_;
        }
        valid_=validated_==n_;
        return valid_?State::done:State::more;
    }
    Cursor begin(uint64_t low,uint64_t high) const {
        return {low,high,0,n_,0,this,true,false};
    }
    // Work budget counts individual record reads, including binary search.
    Page step(Cursor& c,size_t budget,bool cancelled=false) const {
        Page p;
        if(c.owner!=this || !valid_ || failed_ || c.low>c.high || c.right>n_ || c.left>c.right || c.next>n_) return p;
        if(cancelled) { c.finished=true; p.state=State::cancelled; return p; }
        if(c.finished) { p.state=State::done; return p; }
        while(c.search && budget && c.left<c.right) {
            --budget;
            uint64_t mid=c.left+(c.right-c.left)/2;
            Interval r{};
            if(!record(mid,r)) return p;
            if(r.prefix_max<c.low) c.left=mid+1; else c.right=mid;
        }
        if(c.search && c.left==c.right) { c.next=c.left; c.search=false; }
        while(!c.search && budget && c.next<n_ && p.count<p.ids.size()) {
            --budget;
            Interval r{};
            if(!record(c.next,r)) { p.count=0; return p; }
            if(r.low>c.high) { c.finished=true; break; }
            ++c.next;
            if(r.high>=c.low) p.ids[p.count++]=r.id;
        }
        if(!c.search && c.next==n_) c.finished=true;
        p.state=c.finished?State::done:State::more;
        return p;
    }
};
}
