#include "orcsdr_radio_discovery/library.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>
#if defined(__unix__)
#include <unistd.h>
#include <sys/stat.h>
#include <cerrno>
#endif

#define CHECK(c) do {if(!(c)){std::fprintf(stderr,"FAIL at line %d: %s\n",__LINE__,#c);std::abort();}}while(0)
using namespace orcsdr::radio_discovery;
using Db=Library<3,3,3>;
using J=Journal<3,3,3>;

struct MemoryIO final:SlotIO{
 std::vector<uint8_t> data[2];bool fail_write=false,fail_read=false;size_t write_limit=SIZE_MAX;
 IoState read(uint8_t slot,uint8_t*dst,size_t cap,size_t&len)override{
   if(fail_read)return IoState::error;
   if(data[slot].empty())return IoState::missing;
   if(data[slot].size()>cap)return IoState::error;
   len=data[slot].size();std::memcpy(dst,data[slot].data(),len);return IoState::ok;
 }
 bool write_durable(uint8_t slot,const uint8_t*src,size_t len)override{
   if(fail_write)return false;
   data[slot].assign(src,src+std::min(len,write_limit));return write_limit>=len;
 }
};
static Observation rf(uint32_t freq=446500000){
 Observation x{};x.frequency_hz=freq;x.protocol=Protocol::dmr_tier2;x.origin=Origin::rf_observation;x.proof=Proof::crc_valid_decode;
 x.identity.known_fields=access_code_known|slot_known|destination_known;x.identity.access_code=1;x.identity.slot=1;x.identity.destination_id=91;
 x.utc_known=true;x.utc_ms=1000;
 return x;
}
static Source source(){Source s{};std::strcpy(s.name,"user exported CSV");std::strcpy(s.license,"user-supplied");std::strcpy(s.url,"file://local");return s;}
static Observation dir(){auto x=rf();x.origin=Origin::directory_import;x.proof=Proof::candidate;x.utc_known=false;x.utc_ms=0;return x;}
static void smoke(Db&db){
 uint32_t id=0;
 auto x=rf();CHECK(db.observe(x,&id)==Decision::added&&id==1);
  x.utc_ms=2000;x.proof=Proof::protocol_signature;CHECK(db.observe(x)==Decision::updated);
 CHECK(db.rf[0].first.utc_ms==1000&&db.rf[0].last.utc_ms==2000&&db.rf[0].strongest==Proof::crc_valid_decode);
 x.utc_ms=500;CHECK(db.observe(x)==Decision::updated);CHECK(db.rf[0].first.utc_ms==500&&db.rf[0].last.utc_ms==2000);
 auto unknown=x;unknown.identity.known_fields&=~destination_known;CHECK(db.observe(unknown)==Decision::ambiguous);
 // Deliberately preserve the partial observation alongside a validated identity, never merge them.
 CHECK(db.observe(unknown,nullptr,true)==Decision::added);
 auto another=x;another.identity.destination_id=92;CHECK(db.observe(another,nullptr,true)==Decision::added);
 auto imported=dir();CHECK(db.import_candidate(imported,source(),"Repeater One",false,&id)==Decision::added);
 CHECK(db.import_candidate(imported,source(),"Repeater One")==Decision::duplicate);
 CHECK(db.import_candidate(imported,source(),"Other Label")==Decision::conflict);
 CHECK(db.import_candidate(imported,source(),"Other Label",true)==Decision::added);
 CHECK(db.annotate(Target::directory,id,"My repeater","Local notes",false)==Decision::added);
 CHECK(db.annotate(Target::directory,id,"New", "Change",false)==Decision::conflict);
 CHECK(db.annotate(Target::directory,id,"New","Change",true,5000)==Decision::updated);
 CHECK(db.directory[0].label[0]=='R'); // User edit must not alter directory source.
 CHECK(db.annotate(Target::rf,999,"bad","bad",false)==Decision::invalid);
 CHECK(db.rf_count==3&&db.dir_count==2&&db.note_count==1&&db.validate());
}
static void roundtrip_and_recovery(){
 Db db{};smoke(db);MemoryIO io;J journal;J::Workspace scratch{};
 CHECK(journal.save(io,db,scratch)==SaveState::not_loaded);
 Db restored{};
 CHECK(journal.load(io,restored,scratch)==LoadState::empty);
 CHECK(journal.save(io,db,scratch)==SaveState::ok&&journal.generation()==1&&journal.active_slot()==0);
 J another;Db loaded{};CHECK(another.load(io,loaded,scratch)==LoadState::ok&&loaded.rf_count==3&&loaded.annotations[0].edited_utc_ms==5000);
 auto next=loaded;next.rf[0].last.measurements.fec_corrections=4;
 CHECK(another.save(io,next,scratch)==SaveState::ok&&another.generation()==2&&another.active_slot()==1);
 J verify;CHECK(verify.load(io,restored,scratch)==LoadState::ok&&restored.rf[0].last.measurements.fec_corrections==4);
 io.data[1][70]^=0x80; // Corrupt newest snapshot, preserve older.
 J recovery;CHECK(recovery.load(io,restored,scratch)==LoadState::recovered&&recovery.generation()==1);
 CHECK(restored.rf[0].last.measurements.fec_corrections==0);
 CHECK(recovery.save(io,restored,scratch)==SaveState::ok&&recovery.generation()==2);
 io.write_limit=18;
 auto before=recovery.generation();CHECK(recovery.save(io,restored,scratch)==SaveState::io_error&&recovery.generation()==before);
 J r;CHECK(r.load(io,loaded,scratch)==LoadState::recovered&&loaded.rf_count==3); // Previous remains valid.
 io.data[0].clear();io.data[1][10]^=0x80;
 CHECK(r.load(io,loaded,scratch)==LoadState::corrupt);
 CHECK(r.save(io,loaded,scratch)==SaveState::not_loaded); // cannot silently trust corrupt storage
}
static void rejects_corrupt_and_tamper(){
 Db db{};MemoryIO io;J::Workspace work{};J journal;Db load{};
 CHECK(journal.load(io,load,work)==LoadState::empty);
 CHECK(db.observe(rf())==Decision::added);
 CHECK(journal.save(io,db,work)==SaveState::ok);
 auto validbytes=io.data[0];
 io.data[0][4]=2;CHECK(journal.load(io,load,work)==LoadState::corrupt);
 io.data[0]=validbytes;
 io.data[0].push_back(0);CHECK(journal.load(io,load,work)==LoadState::corrupt);
 io.data[0]=validbytes;
 io.fail_read=true;CHECK(journal.load(io,load,work)==LoadState::io_error);io.fail_read=false;
 io.data[1]=io.data[0];CHECK(journal.load(io,load,work)==LoadState::ambiguous);
 auto x=rf();x.origin=Origin::directory_import;CHECK(db.observe(x)==Decision::invalid);
 auto pretend=dir();pretend.utc_known=true;pretend.utc_ms=7000;CHECK(db.import_candidate(pretend,source(),"bad")==Decision::invalid);
 pretend=dir();pretend.measurements.level_known=true;CHECK(db.import_candidate(pretend,source(),"bad")==Decision::invalid);
 x=rf();x.utc_known=true;x.utc_ms=0;CHECK(db.observe(x)==Decision::invalid);
}
#if defined(__unix__)
// A real file-backed SlotIO exercise. Production ESP-IDF still needs its own
// verified fsync/close/SD-card adapter: this is POSIX host-only.
struct DiskIO final:SlotIO{
 const char*folder;
 explicit DiskIO(const char*p):folder(p){}
 void path(uint8_t slot,char*out,size_t n){std::snprintf(out,n,"%s/observation-%c.bin",folder,slot?'b':'a');}
 IoState read(uint8_t slot,uint8_t*dst,size_t cap,size_t&len)override{
  char p[256];path(slot,p,sizeof(p));FILE*f=std::fopen(p,"rb");
  if(!f)return errno==ENOENT?IoState::missing:IoState::error;
  if(std::fseek(f,0,SEEK_END)!=0){std::fclose(f);return IoState::error;}
  long size=std::ftell(f);if(size<0||static_cast<size_t>(size)>cap||std::fseek(f,0,SEEK_SET)!=0){std::fclose(f);return IoState::error;}
  len=static_cast<size_t>(size);bool ok=std::fread(dst,1,len,f)==len;
  if(std::fclose(f)!=0)ok=false;
  return ok?IoState::ok:IoState::error;
 }
 bool write_durable(uint8_t slot,const uint8_t*src,size_t len)override{
  char p[256];path(slot,p,sizeof(p));FILE*f=std::fopen(p,"wb");if(!f)return false;
  bool ok=std::fwrite(src,1,len,f)==len&&std::fflush(f)==0;
  if(ok)ok=::fsync(::fileno(f))==0;
  if(std::fclose(f)!=0)ok=false;
  return ok;
 }
};
static void disk_restart(){
 char temp[]="/tmp/orcsdr-radio-XXXXXX";char*directory=::mkdtemp(temp);CHECK(directory);
 DiskIO io(directory);Db db{};J::Workspace work{};J first,second;
 CHECK(first.load(io,db,work)==LoadState::empty);CHECK(db.observe(rf())==Decision::added);
 CHECK(first.save(io,db,work)==SaveState::ok);
 Db restored{};CHECK(second.load(io,restored,work)==LoadState::ok);CHECK(restored.rf_count==1);
 restored.rf[0].encounters++;
 CHECK(second.save(io,restored,work)==SaveState::ok);
 J reboot;Db again{};CHECK(reboot.load(io,again,work)==LoadState::ok&&again.rf[0].encounters==2);
 char path[256];for(uint8_t slot=0;slot<2;++slot){io.path(slot,path,sizeof(path));CHECK(::unlink(path)==0);}
 CHECK(::rmdir(directory)==0);
}
#endif

static void bounds_and_identity(){
 Db db{};
 auto x=rf();CHECK(db.observe(x)==Decision::added);
 x.frequency_hz++;CHECK(db.observe(x)==Decision::added);
 x.frequency_hz++;CHECK(db.observe(x)==Decision::added);
 x.frequency_hz++;CHECK(db.observe(x)==Decision::full);
 Db fresh{};auto d=dir();d.identity.slot=0;CHECK(fresh.import_candidate(d,source(),"Invalid")==Decision::invalid);
 d.identity.slot=1;auto bad=source();std::memset(bad.name,'X',sizeof(bad.name));CHECK(fresh.import_candidate(d,bad,"X")==Decision::invalid);
 CHECK(fresh.annotate(Target::directory,1,"a","b",true)==Decision::invalid);
}
int main(){
 static_assert(Db::max_wire_bytes==36+3*125+3*280+3*162,"wire bounds");
 bounds_and_identity();rejects_corrupt_and_tamper();roundtrip_and_recovery();
#if defined(__unix__)
 disk_restart();
#endif
 std::puts("radio_discovery_storage_tests PASS");
}
