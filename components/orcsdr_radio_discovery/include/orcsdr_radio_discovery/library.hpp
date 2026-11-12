#pragma once
// F0b: portable, bounded radio library and versioned two-slot snapshot journal.
// Only SlotIO knows a filesystem. No dynamic allocation, network, radio or P25 ownership.
#include "orcsdr_radio_discovery/evidence.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace orcsdr::radio_discovery {
enum class Decision : uint8_t { added, updated, duplicate, conflict, ambiguous, full, invalid };
enum class Target : uint8_t { rf, directory };
struct Source { char name[32]{}; char url[64]{}; char license[32]{}; uint8_t sha256[32]{};
                bool sha_known=false; uint64_t retrieved_utc_ms=0; bool retrieved_known=false; };
struct RfRecord { uint32_t id=0; Observation first{},last{}; uint32_t encounters=0; Proof strongest=Proof::candidate; };
struct DirectoryRecord { uint32_t id=0; Observation candidate{}; Source source{}; char label[48]{}; };
struct Annotation { uint32_t id=0; Target target=Target::rf; uint32_t target_id=0; char alias[48]{}; char notes[96]{};
                    uint64_t edited_utc_ms=0; bool edited_known=false; };

namespace detail {
template<size_t N> bool terminated(const char (&s)[N]) {return std::memchr(s,0,N)!=nullptr;}
inline bool identity_equal(const Observation&a,const Observation&b){
 if(a.protocol!=b.protocol||a.frequency_hz!=b.frequency_hz||a.identity.known_fields!=b.identity.known_fields)return false;
 const auto&x=a.identity;const auto&y=b.identity;const auto f=x.known_fields;
 return (!(f&access_code_known)||x.access_code==y.access_code)&&(!(f&slot_known)||x.slot==y.slot)&&
        (!(f&system_known)||x.system_id==y.system_id)&&(!(f&site_known)||x.site_id==y.site_id)&&
        (!(f&source_known)||x.source_id==y.source_id)&&(!(f&destination_known)||x.destination_id==y.destination_id);
}
inline bool overlaps(const Observation&a,const Observation&b){
 if(a.protocol!=b.protocol||a.frequency_hz!=b.frequency_hz)return false;
 const auto&x=a.identity;const auto&y=b.identity;const uint16_t common=x.known_fields&y.known_fields;
 return (!(common&access_code_known)||x.access_code==y.access_code)&&(!(common&slot_known)||x.slot==y.slot)&&
        (!(common&system_known)||x.system_id==y.system_id)&&(!(common&site_known)||x.site_id==y.site_id)&&
        (!(common&source_known)||x.source_id==y.source_id)&&(!(common&destination_known)||x.destination_id==y.destination_id);
}
inline bool sane(const Observation&o){
 return valid(o)&&static_cast<unsigned>(o.protocol)<=static_cast<unsigned>(Protocol::nxdn96)&&
        static_cast<unsigned>(o.origin)<=static_cast<unsigned>(Origin::user_entry)&&
        static_cast<unsigned>(o.proof)<=static_cast<unsigned>(Proof::site_confirmed);
}
inline bool source_valid(const Source&s){return terminated(s.name)&&s.name[0]&&terminated(s.url)&&terminated(s.license)&&
 (!s.retrieved_known||s.retrieved_utc_ms!=0);}
inline bool directory_candidate_valid(const Observation&o){
 return sane(o)&&o.origin==Origin::directory_import&&o.proof==Proof::candidate&&
 !o.receiver_location.known&&!o.utc_known&&o.utc_ms==0&&
 !o.measurements.level_known&&!o.measurements.offset_known&&
 o.measurements.fec_corrections==0&&o.measurements.crc_failures==0;
}
inline bool annotation_valid(const Annotation&a){return a.id&&a.target_id&&static_cast<unsigned>(a.target)<=1&&
 terminated(a.alias)&&terminated(a.notes)&&(!a.edited_known||a.edited_utc_ms!=0);}

struct Writer {
 uint8_t*data;size_t limit,pos=0;bool ok=true;
 void u8(uint8_t x){if(pos>=limit){ok=false;return;}data[pos++]=x;}
 void u16(uint16_t x){for(int i=0;i<2;++i)u8(static_cast<uint8_t>(x>>(i*8)));}
 void u32(uint32_t x){for(int i=0;i<4;++i)u8(static_cast<uint8_t>(x>>(i*8)));}
 void u64(uint64_t x){for(int i=0;i<8;++i)u8(static_cast<uint8_t>(x>>(i*8)));}
 void bytes(const uint8_t*p,size_t n){for(size_t i=0;i<n;++i)u8(p[i]);}
};
struct Reader {
 const uint8_t*data;size_t limit,pos=0;bool ok=true;
 uint8_t u8(){if(pos>=limit){ok=false;return 0;}return data[pos++];}
 uint16_t u16(){uint16_t r=0;for(int i=0;i<2;++i)r|=static_cast<uint16_t>(u8())<<(i*8);return r;}
 uint32_t u32(){uint32_t r=0;for(int i=0;i<4;++i)r|=static_cast<uint32_t>(u8())<<(i*8);return r;}
 uint64_t u64(){uint64_t r=0;for(int i=0;i<8;++i)r|=static_cast<uint64_t>(u8())<<(i*8);return r;}
 void bytes(uint8_t*p,size_t n){for(size_t i=0;i<n;++i)p[i]=u8();}
};
inline void put_observation(Writer&w,const Observation&o){
 w.u8(static_cast<uint8_t>(o.protocol));w.u8(static_cast<uint8_t>(o.origin));w.u8(static_cast<uint8_t>(o.proof));
 w.u8(o.utc_known);w.u32(o.frequency_hz);w.u64(o.utc_ms);
 w.u8(o.receiver_location.known);w.u32(static_cast<uint32_t>(o.receiver_location.latitude_e7));w.u32(static_cast<uint32_t>(o.receiver_location.longitude_e7));
 const auto&i=o.identity;w.u32(i.system_id);w.u32(i.site_id);w.u32(i.source_id);w.u32(i.destination_id);
 w.u16(i.access_code);w.u16(i.known_fields);w.u8(i.slot);
 const auto&m=o.measurements;w.u16(static_cast<uint16_t>(m.relative_level_db_tenths));w.u32(static_cast<uint32_t>(m.frequency_offset_hz_tenths));
 w.u16(m.fec_corrections);w.u16(m.crc_failures);w.u8(m.level_known);w.u8(m.offset_known);
}
inline Observation get_observation(Reader&r){
 Observation o{};o.protocol=static_cast<Protocol>(r.u8());o.origin=static_cast<Origin>(r.u8());o.proof=static_cast<Proof>(r.u8());
 const auto utc=r.u8();o.utc_known=utc==1;if(utc>1)r.ok=false;o.frequency_hz=r.u32();o.utc_ms=r.u64();
 const auto loc=r.u8();o.receiver_location.known=loc==1;if(loc>1)r.ok=false;
 o.receiver_location.latitude_e7=static_cast<int32_t>(r.u32());o.receiver_location.longitude_e7=static_cast<int32_t>(r.u32());
 auto&i=o.identity;i.system_id=r.u32();i.site_id=r.u32();i.source_id=r.u32();i.destination_id=r.u32();i.access_code=r.u16();i.known_fields=r.u16();i.slot=r.u8();
 auto&m=o.measurements;m.relative_level_db_tenths=static_cast<int16_t>(r.u16());m.frequency_offset_hz_tenths=static_cast<int32_t>(r.u32());
 m.fec_corrections=r.u16();m.crc_failures=r.u16();const auto lev=r.u8(),off=r.u8();m.level_known=lev==1;m.offset_known=off==1;if(lev>1||off>1)r.ok=false;
 return o;
}
inline void put_source(Writer&w,const Source&s){w.bytes(reinterpret_cast<const uint8_t*>(s.name),sizeof(s.name));w.bytes(reinterpret_cast<const uint8_t*>(s.url),sizeof(s.url));w.bytes(reinterpret_cast<const uint8_t*>(s.license),sizeof(s.license));w.bytes(s.sha256,sizeof(s.sha256));w.u8(s.sha_known);w.u64(s.retrieved_utc_ms);w.u8(s.retrieved_known);}
inline Source get_source(Reader&r){Source s{};r.bytes(reinterpret_cast<uint8_t*>(s.name),sizeof(s.name));r.bytes(reinterpret_cast<uint8_t*>(s.url),sizeof(s.url));r.bytes(reinterpret_cast<uint8_t*>(s.license),sizeof(s.license));r.bytes(s.sha256,sizeof(s.sha256));auto a=r.u8();s.sha_known=a==1;if(a>1)r.ok=false;s.retrieved_utc_ms=r.u64();a=r.u8();s.retrieved_known=a==1;if(a>1)r.ok=false;return s;}
inline uint32_t crc32(const uint8_t*data,size_t n){uint32_t c=0xFFFFFFFFu;for(size_t i=0;i<n;++i){if(i>=32&&i<36)continue;c^=data[i];for(int j=0;j<8;++j)c=(c>>1)^((c&1)?0xEDB88320u:0u);}return ~c;}
}

template<size_t RF,size_t DIR,size_t NOTE>class Library {
 static_assert(RF>0&&DIR>0&&NOTE>0&&RF<=65535&&DIR<=65535&&NOTE<=65535,"supported bounded capacities");
public:
 static constexpr size_t max_wire_bytes=36+RF*125+DIR*280+NOTE*162;
 size_t rf_count=0,dir_count=0,note_count=0;
 uint32_t next_id=1;
 std::array<RfRecord,RF> rf{};
 std::array<DirectoryRecord,DIR> directory{};
 std::array<Annotation,NOTE> annotations{};
 bool has_id(Target t,uint32_t id)const{
   if(t==Target::rf){for(size_t i=0;i<rf_count;++i)if(rf[i].id==id)return true;}
   else {for(size_t i=0;i<dir_count;++i)if(directory[i].id==id)return true;}
   return false;
 }
 Decision observe(const Observation&o,uint32_t*id=nullptr){
   if(!detail::sane(o)||o.origin!=Origin::rf_observation)return Decision::invalid;
   for(size_t i=0;i<rf_count;++i){auto&x=rf[i];if(detail::identity_equal(x.last,o)){
      if(x.encounters==UINT32_MAX)return Decision::full;
      ++x.encounters; if(o.utc_known&&(!x.first.utc_known||o.utc_ms<x.first.utc_ms))x.first=o;
      if(o.utc_known&&(!x.last.utc_known||o.utc_ms>=x.last.utc_ms))x.last=o;
      if(o.proof>x.strongest)x.strongest=o.proof;
      if(id)*id=x.id;
      return Decision::updated;
   }}
   for(size_t i=0;i<rf_count;++i)if(detail::overlaps(rf[i].last,o))return Decision::ambiguous;
   if(rf_count==RF||next_id==UINT32_MAX)return Decision::full;
   const auto assigned=next_id++;rf[rf_count++]={assigned,o,o,1,o.proof};if(id)*id=assigned;return Decision::added;
 }
 Decision import_candidate(const Observation&o,const Source&s,const char*label,bool keep_both=false,uint32_t*id=nullptr){
   if(!detail::directory_candidate_valid(o)||
      !detail::source_valid(s)||!label||std::strlen(label)>=sizeof(DirectoryRecord::label))return Decision::invalid;
   for(size_t i=0;i<dir_count;++i){const auto&x=directory[i];if(detail::identity_equal(x.candidate,o)){
      if(std::strcmp(x.label,label)==0&&std::strcmp(x.source.name,s.name)==0&&
         std::strcmp(x.source.url,s.url)==0&&std::strcmp(x.source.license,s.license)==0&&
         x.source.sha_known==s.sha_known&&(!s.sha_known||std::memcmp(x.source.sha256,s.sha256,32)==0)&&
         x.source.retrieved_known==s.retrieved_known&&(!s.retrieved_known||x.source.retrieved_utc_ms==s.retrieved_utc_ms)){if(id)*id=x.id;return Decision::duplicate;}
      if(!keep_both)return Decision::conflict;
   }else if(detail::overlaps(x.candidate,o)&&!keep_both)return Decision::ambiguous;
   }
   if(dir_count==DIR||next_id==UINT32_MAX)return Decision::full;
   DirectoryRecord x{};x.id=next_id++;x.candidate=o;x.source=s;std::memcpy(x.label,label,std::strlen(label)+1);
   directory[dir_count++]=x;if(id)*id=x.id;return Decision::added;
 }
 Decision annotate(Target t,uint32_t id,const char*alias,const char*notes,bool replace,uint64_t utc=0){
   if(!has_id(t,id)||!alias||!notes||std::strlen(alias)>=48||std::strlen(notes)>=96)return Decision::invalid;
   for(size_t i=0;i<note_count;++i)if(annotations[i].target==t&&annotations[i].target_id==id){
     if(!replace)return Decision::conflict;
     Annotation&x=annotations[i];std::memset(x.alias,0,sizeof(x.alias));std::memset(x.notes,0,sizeof(x.notes));
     std::memcpy(x.alias,alias,std::strlen(alias));std::memcpy(x.notes,notes,std::strlen(notes));x.edited_known=utc!=0;x.edited_utc_ms=utc;return Decision::updated;
   }
   if(note_count==NOTE||next_id==UINT32_MAX)return Decision::full;
   Annotation x{};x.id=next_id++;x.target=t;x.target_id=id;
   std::memcpy(x.alias,alias,std::strlen(alias));std::memcpy(x.notes,notes,std::strlen(notes));x.edited_utc_ms=utc;x.edited_known=utc!=0;
   annotations[note_count++]=x;return Decision::added;
 }
 bool validate()const{
   if(rf_count>RF||dir_count>DIR||note_count>NOTE||next_id==0)return false;
   uint32_t max_id=0;
   for(size_t i=0;i<rf_count;++i){const auto&x=rf[i];if(!x.id||!x.encounters||!detail::sane(x.first)||!detail::sane(x.last)||
     x.first.origin!=Origin::rf_observation||x.last.origin!=Origin::rf_observation||
     !detail::identity_equal(x.first,x.last)||static_cast<unsigned>(x.strongest)>4||
     x.strongest<x.first.proof||x.strongest<x.last.proof)return false;
     if(x.first.utc_known&&x.last.utc_known&&x.first.utc_ms>x.last.utc_ms)return false;
     if(x.id>max_id)max_id=x.id;
   }
   for(size_t i=0;i<dir_count;++i){const auto&x=directory[i];if(!x.id||!detail::sane(x.candidate)||
    !detail::directory_candidate_valid(x.candidate)||
    !detail::source_valid(x.source)||!detail::terminated(x.label))return false;
    if(x.id>max_id)max_id=x.id;
   }
   for(size_t i=0;i<note_count;++i){const auto&x=annotations[i];if(!detail::annotation_valid(x)||!has_id(x.target,x.target_id))return false;
    if(x.id>max_id)max_id=x.id;
   }
   if(next_id<=max_id)return false;
   for(size_t i=0;i<rf_count;++i)for(size_t j=0;j<i;++j)if(rf[i].id==rf[j].id||detail::identity_equal(rf[i].last,rf[j].last))return false;
   for(size_t i=0;i<dir_count;++i)for(size_t j=0;j<i;++j)if(directory[i].id==directory[j].id)return false;
   // IDs are globally unique, including annotation IDs.
   for(size_t i=0;i<dir_count;++i)for(size_t j=0;j<rf_count;++j)if(directory[i].id==rf[j].id)return false;
   for(size_t i=0;i<note_count;++i){for(size_t j=0;j<rf_count;++j)if(annotations[i].id==rf[j].id)return false;
      for(size_t j=0;j<dir_count;++j)if(annotations[i].id==directory[j].id)return false;
      for(size_t j=0;j<i;++j)if(annotations[i].id==annotations[j].id)return false;}
   return true;
 }
 bool encode(uint8_t*out,size_t cap,size_t&len,uint64_t generation)const{
   if(!out||!generation||!validate())return false;
   detail::Writer w{out,cap};w.bytes(reinterpret_cast<const uint8_t*>("ORAD"),4);w.u16(1);w.u16(36);
   w.u64(generation);w.u16(static_cast<uint16_t>(rf_count));w.u16(static_cast<uint16_t>(dir_count));
   w.u16(static_cast<uint16_t>(note_count));w.u16(0);w.u32(next_id);w.u32(0);w.u32(0);
   for(size_t i=0;i<rf_count;++i){const auto&x=rf[i];w.u32(x.id);detail::put_observation(w,x.first);detail::put_observation(w,x.last);w.u32(x.encounters);w.u8(static_cast<uint8_t>(x.strongest));}
   for(size_t i=0;i<dir_count;++i){const auto&x=directory[i];w.u32(x.id);detail::put_observation(w,x.candidate);detail::put_source(w,x.source);w.bytes(reinterpret_cast<const uint8_t*>(x.label),sizeof(x.label));}
   for(size_t i=0;i<note_count;++i){const auto&x=annotations[i];w.u32(x.id);w.u8(static_cast<uint8_t>(x.target));w.u32(x.target_id);w.bytes(reinterpret_cast<const uint8_t*>(x.alias),sizeof(x.alias));w.bytes(reinterpret_cast<const uint8_t*>(x.notes),sizeof(x.notes));w.u64(x.edited_utc_ms);w.u8(x.edited_known);}
   if(!w.ok||w.pos>UINT32_MAX)return false;
   len=w.pos;
   for(size_t i=0;i<4;++i)out[28+i]=static_cast<uint8_t>(len>>(8*i));
   const uint32_t crc=detail::crc32(out,len);for(size_t i=0;i<4;++i)out[32+i]=static_cast<uint8_t>(crc>>(8*i));return true;
 }
 bool decode(const uint8_t*data,size_t len,uint64_t&generation){
   if(!data||len<36||len>max_wire_bytes)return false;
   detail::Reader r{data,len};uint8_t magic[4]{};r.bytes(magic,4);
   if(std::memcmp(magic,"ORAD",4)!=0||r.u16()!=1||r.u16()!=36)return false;
   const uint64_t gen=r.u64();const auto nrf=r.u16(),ndir=r.u16(),nnote=r.u16();
   if(r.u16()!=0)return false;
   const uint32_t next=r.u32(),total=r.u32(),checksum=r.u32();
   if(!gen||nrf>RF||ndir>DIR||nnote>NOTE||total!=len||checksum!=detail::crc32(data,len))return false;
   // Fixed size bounds reject truncation and extension before any records are decoded.
   if(len!=36+static_cast<size_t>(nrf)*125+static_cast<size_t>(ndir)*280+static_cast<size_t>(nnote)*162)return false;
   Library candidate{};candidate.next_id=next;
   for(size_t i=0;i<nrf;++i){auto&x=candidate.rf[i];x.id=r.u32();x.first=detail::get_observation(r);x.last=detail::get_observation(r);x.encounters=r.u32();x.strongest=static_cast<Proof>(r.u8());}
   candidate.rf_count=nrf;
   for(size_t i=0;i<ndir;++i){auto&x=candidate.directory[i];x.id=r.u32();x.candidate=detail::get_observation(r);x.source=detail::get_source(r);r.bytes(reinterpret_cast<uint8_t*>(x.label),sizeof(x.label));}
   candidate.dir_count=ndir;
   for(size_t i=0;i<nnote;++i){auto&x=candidate.annotations[i];x.id=r.u32();x.target=static_cast<Target>(r.u8());x.target_id=r.u32();r.bytes(reinterpret_cast<uint8_t*>(x.alias),sizeof(x.alias));r.bytes(reinterpret_cast<uint8_t*>(x.notes),sizeof(x.notes));x.edited_utc_ms=r.u64();const auto known=r.u8();x.edited_known=known==1;if(known>1)r.ok=false;}
   candidate.note_count=nnote;
   if(!r.ok||r.pos!=len||!candidate.validate())return false;
   *this=candidate;generation=gen;return true;
 }
};

enum class IoState:uint8_t{ok,missing,error};
// Implementation MUST write/truncate only the requested slot, flush/close durably,
// and return error for short reads, write failures or OS sync failure.
class SlotIO{public:virtual ~SlotIO()=default;
 virtual IoState read(uint8_t slot,uint8_t*dst,size_t capacity,size_t&actual)=0;
 virtual bool write_durable(uint8_t slot,const uint8_t*src,size_t length)=0;
};
enum class LoadState:uint8_t{ok,empty,recovered,corrupt,io_error,ambiguous};
enum class SaveState:uint8_t{ok,not_loaded,invalid,io_error,exhausted};

template<size_t RF,size_t DIR,size_t NOTE>class Journal{
public:
 using Db=Library<RF,DIR,NOTE>;
 struct Workspace{std::array<uint8_t,Db::max_wire_bytes> bytes{};Db candidate{},chosen{};};
 LoadState load(SlotIO&io,Db&out,Workspace&work){
   // Do not modify caller's data on error.
   ready_=false;active_=255;generation_=0;
   bool seen=false,has=false,corrupt=false;uint64_t best=0;uint8_t selected=0;
   for(uint8_t slot=0;slot<2;++slot){size_t length=0;const auto state=io.read(slot,work.bytes.data(),work.bytes.size(),length);
     if(state==IoState::error)return LoadState::io_error;
     if(state==IoState::missing)continue;
     seen=true;uint64_t gen=0;
     if(!work.candidate.decode(work.bytes.data(),length,gen)){corrupt=true;continue;}
     if(has&&gen==best)return LoadState::ambiguous; // Never guess between equal generation slots.
     if(!has||gen>best){work.chosen=work.candidate;best=gen;selected=slot;has=true;}
   }
   if(!has){if(seen)return LoadState::corrupt;out=Db{};active_=255;generation_=0;ready_=true;return LoadState::empty;}
   out=work.chosen;active_=selected;generation_=best;ready_=true;
   return corrupt?LoadState::recovered:LoadState::ok;
 }
 SaveState save(SlotIO&io,const Db&db,Workspace&work){
   if(!ready_)return SaveState::not_loaded;
   if(generation_==UINT64_MAX)return SaveState::exhausted;
   size_t len=0;const auto next=generation_+1;
   if(!db.encode(work.bytes.data(),work.bytes.size(),len,next))return SaveState::invalid;
   const uint8_t slot=active_==0?1:0;const uint32_t expected=detail::crc32(work.bytes.data(),len);
   if(!io.write_durable(slot,work.bytes.data(),len))return SaveState::io_error;
   size_t read_len=0;
   if(io.read(slot,work.bytes.data(),work.bytes.size(),read_len)!=IoState::ok||read_len!=len||detail::crc32(work.bytes.data(),read_len)!=expected)return SaveState::io_error;
   uint64_t check_gen=0;
   if(!work.candidate.decode(work.bytes.data(),read_len,check_gen)||check_gen!=next)return SaveState::io_error;
   active_=slot;generation_=next;return SaveState::ok;
 }
 uint64_t generation()const{return generation_;}
 uint8_t active_slot()const{return active_;}
private:uint64_t generation_=0;uint8_t active_=255;bool ready_=false;
};
} // namespace orcsdr::radio_discovery
