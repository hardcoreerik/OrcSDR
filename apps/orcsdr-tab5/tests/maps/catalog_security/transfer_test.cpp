#include "map_transfer.hpp"
#include "map_activation.hpp"
#include "map_install.hpp"
#include "map_http_headers.hpp"
#include "orcmap/pack_json.hpp"
#include <cJSON.h>
#include <mbedtls/sha256.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <algorithm>

namespace testfs {
struct PowerLoss{};
std::filesystem::path root;
bool fail_manifest=false,corrupt_read=false,fail_write=false;
bool fail_status=false;
int power_after=0,mutations=0;
std::string power_destination;
void boundary(){if(power_after&&++mutations==power_after)throw PowerLoss{};}
std::filesystem::path path(const char* p){return root/std::string(p).substr(1);}
}
namespace orcsdr::storage {
struct File::State {
  FILE* stream=nullptr;std::string path;
  ~State(){if(stream)std::fclose(stream);}
};
File::File(std::shared_ptr<State> s):state_(std::move(s)){}
File::operator bool()const{return state_&&state_->stream;}
size_t File::read(uint8_t* p,size_t n){
  if(!*this)return 0;
  auto got=std::fread(p,1,n,state_->stream);
  if(got&&testfs::corrupt_read&&state_->path.find(".part")!=std::string::npos){p[0]^=1;testfs::corrupt_read=false;}
  return got;
}
size_t File::write(const uint8_t* p,size_t n){
  if(!*this)return 0;
  if(testfs::fail_write&&state_->path.find("archive.part")!=std::string::npos&&std::ftell(state_->stream)>60000)n/=2;
  return std::fwrite(p,1,n,state_->stream);
}
bool File::seek(size_t p){return *this&&std::fseek(state_->stream,static_cast<long>(p),SEEK_SET)==0;}
size_t File::size()const{
  if(!*this)return 0;auto p=std::ftell(state_->stream);std::fseek(state_->stream,0,SEEK_END);
  auto end=std::ftell(state_->stream);std::fseek(state_->stream,p,SEEK_SET);return end<0?0:end;
}
bool File::sync(){
  if(!*this||std::fflush(state_->stream))return false;
  if(state_->path.find("/activation.pending")!=std::string::npos||
     state_->path.find("/activation.done")!=std::string::npos)testfs::boundary();
  return true;
}
bool File::close(){if(!*this)return true;auto r=std::fclose(state_->stream);state_->stream=nullptr;return r==0;}
bool File::truncate(size_t size){
  if(!*this||size>this->size()||!sync())return false;
  std::error_code ec;std::filesystem::resize_file(testfs::path(state_->path.c_str()),size,ec);
  return !ec&&std::fseek(state_->stream,0,SEEK_END)==0;
}
File FileSystem::open(const char* p,const char* mode,bool)const{
  auto s=std::make_shared<File::State>();s->path=p;
  const std::string binary=std::string(1,mode[0])+"b";
  s->stream=std::fopen(testfs::path(p).string().c_str(),binary.c_str());return File(std::move(s));
}
bool FileSystem::exists(const char* p)const{return std::filesystem::exists(testfs::path(p));}
EntryStatus FileSystem::status(const char* p)const{
  if(testfs::fail_status&&std::string(p).find("activation.pending")!=std::string::npos)return EntryStatus::error;
  std::error_code ec;const auto value=std::filesystem::status(testfs::path(p),ec);
  if(ec==std::errc::no_such_file_or_directory)return EntryStatus::missing;
  if(ec)return EntryStatus::error;
  if(std::filesystem::is_directory(value))return EntryStatus::directory;
  return std::filesystem::is_regular_file(value)?EntryStatus::file:EntryStatus::missing;
}
bool FileSystem::mkdir(const char* p)const{return std::filesystem::create_directories(testfs::path(p));}
bool FileSystem::remove(const char* p)const{return std::filesystem::remove(testfs::path(p));}
bool FileSystem::rename(const char* from,const char* to)const{
  if(testfs::fail_manifest&&std::string(to).find(".manifest.json")!=std::string::npos)return false;
  if(exists(to))return false;
  std::error_code ec;std::filesystem::rename(testfs::path(from),testfs::path(to),ec);
  if(ec)std::printf("rename failure %s; destination bytes=%zu\n",ec.message().c_str(),testfs::path(to).string().size());
  if(!ec&&testfs::power_destination==to)throw testfs::PowerLoss{};
  if(!ec){if(testfs::power_after)std::printf("rename mutation: %s -> %s\n",from,to);testfs::boundary();}return !ec;
}
}
namespace t=orcsdr::maps::transfer;
using orcsdr::maps::catalog::Pack;
std::vector<uint8_t> body;
std::string hash(const std::vector<uint8_t>& bytes){
  uint8_t digest[32];mbedtls_sha256(bytes.data(),bytes.size(),digest,0);
  char out[65]{};for(size_t i=0;i<32;++i)std::snprintf(out+i*2,3,"%02x",digest[i]);return out;
}
std::string load(const std::filesystem::path& path){
  std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};
}
Pack pack(const char* manifest){
  const auto original=load(manifest);
  std::unique_ptr<cJSON,decltype(&cJSON_Delete)> json(cJSON_Parse(original.c_str()),cJSON_Delete);
  cJSON_ReplaceItemInObjectCaseSensitive(json.get(),"size_bytes",cJSON_CreateNumber(static_cast<double>(body.size())));
  cJSON_ReplaceItemInObjectCaseSensitive(json.get(),"output_sha256",cJSON_CreateString(hash(body).c_str()));
  cJSON_ReplaceItemInObjectCaseSensitive(json.get(),"max_zoom",cJSON_CreateNumber(0));
  std::unique_ptr<char,decltype(&cJSON_free)> text(cJSON_PrintUnformatted(json.get()),cJSON_free);
  Pack result;orcmap::ParsePackManifestJson(text.get(),&result.manifest);
  result.manifest.pack_id=orcmap::MakePackId(result.manifest);
  cJSON_ReplaceItemInObjectCaseSensitive(json.get(),"pack_id",cJSON_CreateString(result.manifest.pack_id.c_str()));
  text.reset(cJSON_PrintUnformatted(json.get()));result.manifest_json=text.get();
  result.urls={"https://maps.example.test/"+hash(body)+"/fixture.pmtiles"};return result;
}
struct Stream:t::Stream {
  size_t position=0,fail_after=SIZE_MAX,read_bytes=0;int opens=0;uint64_t requested=0;
  bool changed_tag=false,corrupt_after=false;
  bool open(std::string_view,const orcsdr::maps::download::Request& r,t::Headers& h)override{
    ++opens;requested=r.offset;position=static_cast<size_t>(r.offset);
    h.status=r.offset?206:200;h.has_length=true;h.length=body.size()-position;
    h.etag=changed_tag?"\"changed\"":"\"fixture-v1\"";
    if(r.offset)h.range="bytes "+std::to_string(r.offset)+"-"+std::to_string(body.size()-1)+"/"+std::to_string(body.size());
    return true;
  }
  int read(uint8_t* p,size_t n)override{
    if(position>=fail_after)return -1;
    const auto take=std::min({n,body.size()-position,fail_after-position,size_t(7000)});
    std::memcpy(p,body.data()+position,take);position+=take;read_bytes+=take;
    if(position==body.size()&&corrupt_after)testfs::corrupt_read=true;
    return static_cast<int>(take);
  }
  void close()override{}
};
struct DeliveryStore:orcsdr::maps::delivery::Store {
  std::string bytes;int writes=0,fail_at=0;bool persist_failure=false;
  orcsdr::maps::delivery::Read read(std::string* out)override {
    if(bytes.empty())return orcsdr::maps::delivery::Read::missing;
    *out=bytes;return orcsdr::maps::delivery::Read::ok;
  }
  bool write(std::string_view value)override {
    const bool fail=++writes==fail_at;
    if(!fail||persist_failure)bytes=value;
    return !fail;
  }
};
int main(int argc,char** argv){
  if(argc!=4)return 2;
  testfs::root=std::filesystem::weakly_canonical(argv[3]);
  const auto allowed=std::filesystem::weakly_canonical(std::filesystem::current_path())/
      ".local/maps/map-transfer-fixture-20261019";
  if(testfs::root!=allowed){std::printf("refusing fixture path outside fixed workspace test directory\n");return 2;}
  std::filesystem::create_directories(testfs::root);
  auto original=load(argv[1]);body.assign(original.begin(),original.end());body.resize(2*1024*1024+123,0);
  orcsdr::storage::FileSystem fs;
  std::vector<uint8_t> buffer(16384);
  const auto p=pack(argv[2]);
  int failures=0;
  auto check=[&](bool ok,const char* what){if(!ok){std::printf("FAIL %s\n",what);++failures;}};
  t::HeaderCapture capture;
  check(capture.add("ETag","\"v1\""),"HTTP ETag captured");
  check(!capture.add("etag","\"v2\""),"duplicate HTTP critical header refused");
  capture={};check(!capture.add("ETag",std::string(129,'x')),"oversized HTTP ETag refused");
  check(t::https_endpoint("https://cdn.example.test/a?temporary=x"),"HTTPS redirect query allowed");
  check(!t::https_endpoint("http://cdn.example.test/a")&&
        !t::https_endpoint("https://user:secret@cdn.example.test/a")&&
        !t::https_endpoint("https://cdn.example.test/\r\nx"),"downgrade and unsafe redirects refused");
  auto reset=[&]{std::filesystem::remove_all(testfs::root);std::filesystem::create_directories(testfs::root/"orcmaps");
    std::ofstream(testfs::root/"orcmaps/old.pmtiles")<<"valuable map";
    testfs::fail_manifest=false;testfs::corrupt_read=false;testfs::fail_write=false;
    testfs::power_after=0;testfs::mutations=0;testfs::fail_status=false;};
  auto run=[&](Stream& s,uint64_t free=100000000){
    const auto result=t::stage(fs,p,0,s,free,buffer.data(),buffer.size(),{});
    std::printf("transfer result=%d request=%llu opens=%d\n",static_cast<int>(result),
                static_cast<unsigned long long>(s.requested),s.opens);return result;};
  reset();bool pre_cancel=true;Stream not_started;
  t::Hooks before_start{&pre_cancel,[](void* value){return *static_cast<bool*>(value);}};
  check(t::stage(fs,p,0,not_started,100000000,buffer.data(),buffer.size(),before_start)==t::Error::cancelled&&
        not_started.opens==0,"cancel before transfer avoids network");
  reset();Stream first;
  check(run(first,1)==t::Error::space&&first.opens==0,"insufficient space before HTTP");
  first.fail_after=65000;
  check(run(first)==t::Error::network,"network failure leaves checkpoint");
  auto paths=t::paths(p);
  check(!fs.exists(paths.manifest.c_str()),"partial transfer undiscoverable");
  {std::ofstream tail(testfs::path(paths.partial.c_str()),std::ios::binary|std::ios::app);tail<<"unsynced tail";}
  const auto before=load(testfs::path(paths.partial.c_str()));
  Stream changed;changed.changed_tag=true;
  check(run(changed)==t::Error::response,"changed ETag rejected");
  check(load(testfs::path(paths.partial.c_str()))==before,"bad response preserves staging bytes");
  Stream resumed;
  check(run(resumed)==t::Error::none&&resumed.requested==65000,"resume truncates unsynced tail");
  check(fs.exists(paths.manifest.c_str()),"verified staging manifest prepared");
  check(!std::filesystem::exists(testfs::root/"orcmaps/pack.manifest.json"),"staging stays outside flat discovery");
  check(load(testfs::root/"orcmaps/old.pmtiles")=="valuable map","old packs preserved");
  struct Verification {
    std::vector<uint64_t> used;uint64_t total=0;bool cancel=false,stop=false;
    std::vector<t::VerificationProfile> profiles;
  };
  Verification measured;
  t::Hooks measured_hooks{&measured,[](void* p){return static_cast<Verification*>(p)->cancel;},nullptr,nullptr,
    [](void* p,uint64_t used,uint64_t total){
      auto& v=*static_cast<Verification*>(p);v.used.push_back(used);v.total=total;
      if(v.stop&&used==0)v.cancel=true;
    },[](void* p,const t::VerificationProfile& profile){
      static_cast<Verification*>(p)->profiles.push_back(profile);
    }};
  check(t::verify_archive(fs,p,paths.archive,buffer.data(),buffer.size(),measured_hooks)==t::Error::none&&
        measured.used.size()==2&&measured.used.front()==0&&measured.used.back()>0&&
        measured.used.back()<65536&&measured.total==measured.used.back(),
        "quick check reads only bounded PMTiles metadata, not the entire archive");
  check(measured.profiles.size()==1&&measured.profiles[0].result==t::Error::none&&
        measured.profiles[0].bytes==measured.used.back()&&measured.profiles[0].hash_us==0&&
        measured.profiles[0].elapsed_us>=measured.profiles[0].read_us,
        "quick check profiles actual metadata reads and no full-file hashing");
  std::printf("QUICK_FILE_CHECK metadata_bytes=%llu archive_bytes=%llu hash_us=%llu\n",
      static_cast<unsigned long long>(measured.profiles[0].bytes),
      static_cast<unsigned long long>(body.size()),
      static_cast<unsigned long long>(measured.profiles[0].hash_us));
  const auto verified_bytes=load(testfs::path(paths.archive.c_str()));
  measured.used.clear();measured.stop=true;
  check(t::verify_archive(fs,p,paths.archive,buffer.data(),buffer.size(),measured_hooks)==t::Error::cancelled&&
        load(testfs::path(paths.archive.c_str()))==verified_bytes,
        "quick check retains cancellation and read-only archive");
  measured={};auto damaged=verified_bytes;damaged.back()^=1;
  {std::ofstream out(testfs::path(paths.archive.c_str()),std::ios::binary);out.write(damaged.data(),damaged.size());}
  check(t::verify_archive(fs,p,paths.archive,buffer.data(),buffer.size(),measured_hooks)==t::Error::none,
        "quick check intentionally does not scan unchanged-size tile payload for corruption");
  damaged[0]=0;
  {std::ofstream out(testfs::path(paths.archive.c_str()),std::ios::binary);out.write(damaged.data(),damaged.size());}
  check(t::verify_archive(fs,p,paths.archive,buffer.data(),buffer.size(),measured_hooks)==t::Error::archive,
        "quick check rejects invalid PMTiles header");
  {std::ofstream out(testfs::path(paths.archive.c_str()),std::ios::binary);out.write(verified_bytes.data(),verified_bytes.size()-1);}
  check(t::verify_archive(fs,p,paths.archive,buffer.data(),buffer.size(),measured_hooks)==t::Error::storage,
        "quick check rejects incorrect saved file size");
  reset();Stream corrupted;corrupted.corrupt_after=true;
  check(run(corrupted)==t::Error::archive&&!fs.exists(paths.manifest.c_str()),"SD header readback corruption rejected");
  reset();Stream manifest_rename_failure;testfs::fail_manifest=true;
  check(run(manifest_rename_failure)==t::Error::storage&&!fs.exists(paths.manifest.c_str()),"staging rename failure preserves old packs");
  testfs::fail_manifest=false;Stream recovery;
  check(run(recovery)==t::Error::none&&recovery.opens==0,"staged archive orphan verified and prepared");
  reset();Stream partial;partial.fail_after=65000;
  check(run(partial)==t::Error::network,"metadata recovery fixture interrupted");
  {std::fstream record(testfs::path(paths.record[1].c_str()),std::ios::binary|std::ios::in|std::ios::out);
   record.seekp(249);record.put('x');}
  Stream fallback;
  check(run(fallback)==t::Error::none&&fallback.requested==0,"bad metadata falls back to earlier valid checkpoint");
  reset();Stream write_error;testfs::fail_write=true;
  check(run(write_error)==t::Error::storage,"partial SD write rejected");
  testfs::fail_write=false;Stream write_recovery;
  check(run(write_recovery)==t::Error::none&&write_recovery.requested==0,"partial write tail discarded before retry");
  reset();Stream cancel_stream;bool cancelled=false;
  t::Hooks hooks{&cancelled,[](void* p){return *static_cast<bool*>(p);},
    [](void* p,uint64_t used,uint64_t){if(used>=65000)*static_cast<bool*>(p)=true;}};
  check(t::stage(fs,p,0,cancel_stream,100000000,buffer.data(),buffer.size(),hooks)==t::Error::cancelled,
        "cancellation checkpoints prefix");
  const auto prefix=load(testfs::path(paths.partial.c_str()));
  struct SecondPause {bool cancelled=false;uint64_t stop;};
  SecondPause second{false,prefix.size()+14000};
  t::Hooks second_hooks{&second,[](void* p){return static_cast<SecondPause*>(p)->cancelled;},
    [](void* p,uint64_t used,uint64_t){auto& s=*static_cast<SecondPause*>(p);if(used>=s.stop)s.cancelled=true;}};
  Stream interrupted_resume;
  check(t::stage(fs,p,0,interrupted_resume,100000000,buffer.data(),buffer.size(),second_hooks)==t::Error::cancelled&&
        interrupted_resume.requested==prefix.size(),"second pause resumes from exact saved prefix");
  const auto second_prefix=load(testfs::path(paths.partial.c_str()));
  check(second_prefix.size()>prefix.size()&&second_prefix.compare(0,prefix.size(),prefix)==0,
        "second pause grows checkpoint without replacing saved bytes");
  Stream cancelled_resume;
  check(run(cancelled_resume)==t::Error::none&&cancelled_resume.requested==second_prefix.size()&&
        cancelled_resume.read_bytes==body.size()-second_prefix.size(),"repeated cancellation fetches only remaining bytes");
  check(load(testfs::path(paths.archive.c_str()))==std::string(body.begin(),body.end()),
        "resumed archive is byte-identical to complete source");
  reset();const auto good=body;body[0]=0;
  const auto invalid=pack(argv[2]);Stream bad_archive;
  check(t::stage(fs,invalid,0,bad_archive,100000000,buffer.data(),buffer.size(),{})==t::Error::archive,
        "matching body hash cannot bypass PMTiles header validation");
  body=good;
  namespace activation=orcsdr::maps::activation;
  const auto current_body=body;
  body.back()=1;const auto previous=pack(argv[2]);const auto previous_body=body;body=current_body;
  auto install_previous=[&] {
    std::ofstream archive(testfs::root/"orcmaps/previous.pmtiles",std::ios::binary);
    archive.write(reinterpret_cast<const char*>(previous_body.data()),previous_body.size());archive.close();
    std::ofstream(testfs::root/"orcmaps/previous.manifest.json",std::ios::binary)<<previous.manifest_json;
  };
  for(int point=1;point<=5;++point) {
    reset();install_previous();Stream prepared;
    check(run(prepared)==t::Error::none,"activation fixture staged");
    testfs::power_after=point;bool interrupted=false;
    try{const auto result=activation::activate(fs,p,"/orcmaps/previous.manifest.json",buffer.data(),buffer.size(),{});
        std::printf("activate returned %d after %d boundaries\n",static_cast<int>(result),testfs::mutations);}
    catch(const testfs::PowerLoss&){interrupted=true;}
    testfs::power_after=0;
    check(interrupted,"power boundary reached");
    check(activation::recover(fs,paths.directory)==activation::Error::none,"activation recovered");
    const auto target=activation::target_manifest(p);
    check(point==5?(!fs.exists("/orcmaps/previous.manifest.json")&&fs.exists(target.c_str())):
                  (fs.exists("/orcmaps/previous.manifest.json")&&!fs.exists(target.c_str())),
          "only committed new or restored old manifest visible");
    const auto old=load(testfs::root/"orcmaps/previous.pmtiles");
    check(old==std::string(previous_body.begin(),previous_body.end()),"previous archive bytes preserved");
    size_t visible=0;
    for(const auto& file:std::filesystem::directory_iterator(testfs::root/"orcmaps"))
      if(file.is_regular_file()&&file.path().filename().string().find(".manifest.json")!=std::string::npos)++visible;
    check(visible==1,"no duplicate visible pack identities");
    std::printf("activation interruption boundary %d checked\n",point);
  }
  auto prepare_activation=[&] {
    reset();install_previous();Stream prepared;
    check(run(prepared)==t::Error::none,"activation validation fixture staged");
  };
  auto activate=[&] {return activation::activate(fs,p,"/orcmaps/previous.manifest.json",
      buffer.data(),buffer.size(),{});};
  prepare_activation();
  check(activate()==activation::Error::none,"replacement committed");
  const auto target=activation::target_manifest(p);
  check(activation::recover(fs,paths.directory)==activation::Error::none,"committed recovery idempotent");
  check(activate()==activation::Error::none,"committed activation retry idempotent");
  testfs::fail_status=true;
  check(activation::recover(fs,paths.directory)==activation::Error::storage&&
        activation::finalize(fs,paths.directory)==activation::Error::storage,
        "SD status errors cannot masquerade as missing journals");
  testfs::fail_status=false;
  check(activation::finalize(fs,paths.directory)==activation::Error::none&&
        activation::finalize(fs,paths.directory)==activation::Error::none,"finalization idempotent");
  check(fs.exists((paths.directory+"/previous.bak").c_str())&&
        fs.exists("/orcmaps/previous.pmtiles")&&fs.exists(target.c_str()),"finalization retains recovery data");
  prepare_activation();
  {std::ofstream(testfs::path((paths.directory+"/activation.pending").c_str()),std::ios::binary)<<"torn";}
  check(activation::recover(fs,paths.directory)==activation::Error::none&&
        load(testfs::path((paths.directory+"/activation.invalid").c_str()))=="torn"&&
        fs.exists("/orcmaps/previous.manifest.json"),"torn initial journal quarantined without losing old map");
  prepare_activation();
  testfs::power_after=3;
  try{activate();}catch(const testfs::PowerLoss&){}
  testfs::power_after=0;
  {std::ofstream(testfs::path((paths.directory+"/activation.pending").c_str()),std::ios::binary)<<"torn";}
  check(activation::recover(fs,paths.directory)==activation::Error::corrupt&&
        fs.exists((paths.directory+"/previous.bak").c_str()),"corrupt mutated journal retains backup for attention");
  prepare_activation();
  testfs::power_after=3;
  try{activate();}catch(const testfs::PowerLoss&){}
  testfs::power_after=0;
  std::ofstream(testfs::root/"orcmaps/previous.manifest.json",std::ios::binary)<<"external replacement";
  check(activation::recover(fs,paths.directory)==activation::Error::conflict&&
        load(testfs::root/"orcmaps/previous.manifest.json")=="external replacement",
        "recovery never overwrites externally changed metadata");
  prepare_activation();
  check(activation::activate(fs,p,"/orcmaps/../previous.manifest.json",buffer.data(),buffer.size(),{})==
        activation::Error::input&&fs.exists("/orcmaps/previous.manifest.json"),"old manifest traversal refused");
  check(activation::recover(fs,"/orcmaps/.stage-../invalid")==activation::Error::input,
        "recovery directory traversal refused");
  prepare_activation();
  {std::fstream archive(testfs::path(paths.archive.c_str()),std::ios::binary|std::ios::in|std::ios::out);
   archive.seekp(0);archive.put('x');}
  check(activate()==activation::Error::corrupt&&fs.exists("/orcmaps/previous.manifest.json")&&
        !fs.exists(target.c_str()),"activation checks staging header before changing visible maps");
  for(int point=1;point<=4;++point) {
    reset();Stream prepared;check(run(prepared)==t::Error::none,"first install staged");
    testfs::power_after=point;bool interrupted=false;
    try{activation::activate(fs,p,"",buffer.data(),buffer.size(),{});}
    catch(const testfs::PowerLoss&){interrupted=true;}
    testfs::power_after=0;
    check(interrupted&&activation::recover(fs,paths.directory)==activation::Error::none,
          "first install interruption recovered");
    check(fs.exists(target.c_str())==(point==4),"only committed first install visible");
    check(load(testfs::root/"orcmaps/old.pmtiles")=="valuable map","unmanaged map preserved");
  }
  namespace install=orcsdr::maps::install;
  namespace delivery=orcsdr::maps::delivery;
  orcsdr::maps::catalog::Catalog accepted;accepted.revision=1;accepted.sha256=std::string(64,'a');
  accepted.expires_at=200;
  accepted.packs={p};accepted.packs[0].product_id="global-regional";
  auto trusted=[&](DeliveryStore& store) {
    delivery::State state;state.accepted_revision=1;state.catalog_sha256=accepted.sha256;
    check(delivery::save(store,state)==delivery::Error::none,"installer prior catalog trust saved");
  };
  auto install_pack=[&](DeliveryStore& store,Stream& stream) {
    return install::run(fs,store,accepted,0,0,stream,100000000,buffer.data(),buffer.size(),false,1,{});
  };
  reset();DeliveryStore normal;trusted(normal);Stream normal_stream;
  check(install_pack(normal,normal_stream).error==install::Error::none,"installer completes entire transaction");
  delivery::State state;
  check(delivery::load(normal,&state)==delivery::Error::none&&state.pending.product.empty()&&
        state.owned.size()==1&&state.owned[0].manifest==target&&
        !fs.exists((paths.directory+"/activation.pending").c_str()),"ownership durable before journal finalized");
  Stream already_installed;
  check(install_pack(normal,already_installed).error==install::Error::none&&already_installed.opens==0,
        "retry already-owned archive does not download it again");
  const auto installed_archive=target.substr(0,target.size()-14)+".pmtiles";
  check(fs.remove(installed_archive.c_str()),"simulate missing owned archive without deleting metadata");
  Stream repair_missing;
  check(install_pack(normal,repair_missing).error==install::Error::none&&repair_missing.opens==1&&
        fs.exists(installed_archive.c_str()),"missing owned archive can be downloaded and repaired");
  check(fs.remove(installed_archive.c_str()),"simulate archive missing before interrupted repair");
  testfs::power_destination=installed_archive;bool repair_cut=false;Stream repair_power;
  try{install_pack(normal,repair_power);}catch(const testfs::PowerLoss&){repair_cut=true;}
  testfs::power_destination.clear();
  check(repair_cut,"power cut injected immediately after repaired archive publication");
  check(install::resume(fs,normal,buffer.data(),buffer.size(),{}).error==install::Error::none&&
        delivery::load(normal,&state)==delivery::Error::none&&state.pending.product.empty()&&
        fs.exists(installed_archive.c_str()),"interrupted repair finishes offline from persisted intent");
  for(bool persisted:{false,true}) {
    reset();DeliveryStore interrupted;trusted(interrupted);Stream stream;
    interrupted.fail_at=3;interrupted.persist_failure=persisted;
    check(install_pack(interrupted,stream).error==install::Error::state,"ownership commit failure reported");
    check(fs.exists((paths.directory+"/activation.pending").c_str()),"failed commit retains recovery journal");
    interrupted.fail_at=0;
    check(install::resume(fs,interrupted,buffer.data(),buffer.size(),{}).error==install::Error::none,
          "offline restart reconciles uncertain ownership commit");
    check(delivery::load(interrupted,&state)==delivery::Error::none&&state.pending.product.empty()&&
          state.owned.size()==1&&!fs.exists((paths.directory+"/activation.pending").c_str()),
          "offline completion finalizes only after saved ownership");
  }
  reset();DeliveryStore plan_failure;trusted(plan_failure);Stream plan_stream;plan_failure.fail_at=2;
  check(install_pack(plan_failure,plan_stream).error==install::Error::state&&!fs.exists(target.c_str()),
        "failed intent commit prevents visible activation");
  plan_failure.fail_at=0;Stream retry;
  check(install_pack(plan_failure,retry).error==install::Error::none&&retry.opens==0,
        "intent save retry reuses verified staging without network");
  reset();DeliveryStore uncertain_plan;trusted(uncertain_plan);Stream uncertain_stream;
  uncertain_plan.fail_at=2;uncertain_plan.persist_failure=true;
  check(install_pack(uncertain_plan,uncertain_stream).error==install::Error::state,
        "uncertain intent commit reported");
  uncertain_plan.fail_at=0;
  check(install::resume(fs,uncertain_plan,buffer.data(),buffer.size(),{}).error==install::Error::none&&
        fs.exists(target.c_str()),"offline resume uses staging when activation never started");
  reset();DeliveryStore wiped;trusted(wiped);
  check(delivery::load(wiped,&state)==delivery::Error::none&&
        delivery::plan_install(state,accepted.packs[0].product_id,target,false)==delivery::Error::none&&
        delivery::save(wiped,state)==delivery::Error::none,"pending intent saved before the card is wiped");
  check(install::resume(fs,wiped,buffer.data(),buffer.size(),{}).error==install::Error::none&&
        delivery::load(wiped,&state)==delivery::Error::none&&state.pending.product.empty(),
        "pending install with every file deleted is abandoned, not an eternal error");
  for(int point=1;point<=4;++point) {
    reset();DeliveryStore powered;trusted(powered);Stream prepared;
    check(run(prepared)==t::Error::none,"installer power fixture staged");
    testfs::power_after=point;bool cut=false;Stream no_network;
    try{install_pack(powered,no_network);}catch(const testfs::PowerLoss&){cut=true;}
    testfs::power_after=0;
    check(cut&&no_network.opens==0&&install::resume(fs,powered,buffer.data(),buffer.size(),{}).error==install::Error::none,
          "coordinated restart rolls forward after activation interruption");
    check(delivery::load(powered,&state)==delivery::Error::none&&state.pending.product.empty()&&
          state.owned.size()==1&&fs.exists(target.c_str()),"restarted filesystem and ownership agree");
  }
  reset();DeliveryStore untrusted;Stream untouched;
  check(install_pack(untrusted,untouched).error==install::Error::input&&untouched.opens==0,
        "installer refuses catalog not matching persisted trust");
  reset();DeliveryStore expired;trusted(expired);Stream expired_stream;
  check(install::run(fs,expired,accepted,0,0,expired_stream,100000000,buffer.data(),buffer.size(),false,200,{}).error==
        install::Error::input&&expired_stream.opens==0,"expired catalog cannot start network install");
  reset();DeliveryStore permanent;trusted(permanent);Stream permanent_stream;
  const auto dated_expiry=accepted.expires_at;accepted.expires_at=0;
  check(install::run(fs,permanent,accepted,0,0,permanent_stream,100000000,buffer.data(),buffer.size(),false,2000000000,{}).error==
        install::Error::none,"explicitly nonexpiring accepted catalog installs at a later time");
  accepted.expires_at=dated_expiry;
  reset();DeliveryStore replaced;trusted(replaced);Stream old_stream;
  accepted.packs[0]=previous;accepted.packs[0].product_id="global-regional";body=previous_body;
  check(install_pack(replaced,old_stream).error==install::Error::none,"managed prior version installed");
  const auto old_target=activation::target_manifest(previous);
  accepted.packs[0]=p;accepted.packs[0].product_id="global-regional";body=current_body;
  accepted.revision=2;accepted.sha256=std::string(64,'b');
  check(delivery::load(replaced,&state)==delivery::Error::none,"prior ownership loaded for new catalog");
  state.accepted_revision=2;state.catalog_sha256=accepted.sha256;
  check(delivery::save(replaced,state)==delivery::Error::none,"new catalog trust saved preserving ownership");
  Stream replacement;
  check(install_pack(replaced,replacement).error==install::Error::none&&
        !fs.exists(old_target.c_str())&&fs.exists(target.c_str()),"new product version replaces only its managed metadata");
  const auto old_archive=old_target.substr(0,old_target.size()-14)+".pmtiles";
  check(load(testfs::path(old_archive.c_str()))==std::string(previous_body.begin(),previous_body.end()),
        "coordinated update preserves prior archive for recovery");
  for(int revision=3;revision<=6;++revision) {
    const bool old=(revision%2)!=0;
    accepted.packs[0]=old?previous:p;accepted.packs[0].product_id="global-regional";
    body=old?previous_body:current_body;
    accepted.revision=revision;accepted.sha256=std::string(64,static_cast<char>('a'+revision%6));
    check(delivery::load(replaced,&state)==delivery::Error::none,"artifact reuse loads managed ownership");
    state.accepted_revision=revision;state.catalog_sha256=accepted.sha256;
    check(delivery::save(replaced,state)==delivery::Error::none,"artifact reuse has a newer accepted catalog");
    Stream reused;const auto result=install_pack(replaced,reused);
    check(result.error==install::Error::none,"A-B-A artifact reuse completes safely");
    if(result.error!=install::Error::none)break;
    check(fs.exists((old?old_target:target).c_str())&&!fs.exists((old?target:old_target).c_str()),
          "artifact reuse leaves one visible managed version");
  }
  for(const auto* version:{&previous,&p}) {
    const auto& raw=version->manifest_json;
    const auto saved="/orcmaps/.recovery/"+hash(std::vector<uint8_t>(raw.begin(),raw.end()))+".bak";
    check(load(testfs::path(saved.c_str()))==raw,"artifact reuse preserves earlier metadata in hidden history");
  }
  const auto conflict_history="/orcmaps/.recovery/"+hash(std::vector<uint8_t>(p.manifest_json.begin(),p.manifest_json.end()))+".bak";
  std::ofstream(testfs::path(conflict_history.c_str()),std::ios::binary)<<"foreign history";
  accepted.revision=7;accepted.sha256=std::string(64,'b');accepted.packs[0]=previous;
  accepted.packs[0].product_id="global-regional";body=previous_body;
  check(delivery::load(replaced,&state)==delivery::Error::none,"history conflict prior state loaded");
  state.accepted_revision=7;state.catalog_sha256=accepted.sha256;
  check(delivery::save(replaced,state)==delivery::Error::none,"history conflict newer catalog trusted");
  Stream conflicted;
  const auto blocked=install_pack(replaced,conflicted);
  check(blocked.error==install::Error::activation&&blocked.activation==activation::Error::conflict&&
        fs.exists(target.c_str())&&!fs.exists(old_target.c_str())&&
        load(testfs::path(conflict_history.c_str()))=="foreign history",
        "backup collision leaves current map and conflicting bytes untouched");
  for(int point=1;point<=5;++point) {
    reset();DeliveryStore powered;accepted.revision=1;accepted.sha256=std::string(64,'a');
    accepted.packs[0]=previous;accepted.packs[0].product_id="global-regional";body=previous_body;
    trusted(powered);Stream old;
    check(install_pack(powered,old).error==install::Error::none,"replacement interruption prior version ready");
    accepted.revision=2;accepted.sha256=std::string(64,'b');accepted.packs[0]=p;
    accepted.packs[0].product_id="global-regional";body=current_body;
    check(delivery::load(powered,&state)==delivery::Error::none,"replacement trust state loaded");
    state.accepted_revision=2;state.catalog_sha256=accepted.sha256;
    check(delivery::save(powered,state)==delivery::Error::none,"replacement trust state saved");
    Stream prepared;check(run(prepared)==t::Error::none,"replacement pre-staged before power boundary");
    testfs::power_after=point;bool cut=false;Stream no_network;
    try{install_pack(powered,no_network);}catch(const testfs::PowerLoss&){cut=true;}
    testfs::power_after=0;
    check(cut&&install::resume(fs,powered,buffer.data(),buffer.size(),{}).error==install::Error::none,
          "coordinated replacement restart completes offline");
    check(delivery::load(powered,&state)==delivery::Error::none&&state.pending.product.empty()&&
          state.owned.size()==1&&state.owned[0].manifest==target&&fs.exists(target.c_str())&&
          !fs.exists(old_target.c_str()),"replacement restart preserves consistent ownership and visible metadata");
  }
  std::printf("RESULT: transfer scenarios %s\n",failures?"FAIL":"PASS");return failures?1:0;
}
