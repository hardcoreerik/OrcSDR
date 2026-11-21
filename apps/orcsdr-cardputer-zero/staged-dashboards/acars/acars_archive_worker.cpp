#include "acars_archive_worker.hpp"
#include <chrono>
#include <cstdio>
#include <cstring>

namespace orc::zero::acars_dashboard {
namespace {
using namespace orcsdr::acars_archive;
bool ready(bool){return true;}
bool make_dir(const char* p){if(std::strcmp(p,"/orcsdr")==0)return true;std::error_code e;std::filesystem::create_directories(p,e);return !e||std::filesystem::is_directory(p);}
void* open_file(const char* p,bool append){return std::fopen(p,append?"ab+":"rb");}
size_t write_file(void* h,const void* p,size_t n){return std::fwrite(p,1,n,static_cast<FILE*>(h));}
size_t read_file(void* h,void* p,size_t n){return std::fread(p,1,n,static_cast<FILE*>(h));}
bool seek_file(void* h,uint32_t p){return std::fseek(static_cast<FILE*>(h),long(p),SEEK_SET)==0;}
uint32_t file_size(void* h){auto* f=static_cast<FILE*>(h);const long at=std::ftell(f);std::fseek(f,0,SEEK_END);const long n=std::ftell(f);std::fseek(f,at,SEEK_SET);return n<0?0:uint32_t(n);}
void close_file(void* h){std::fclose(static_cast<FILE*>(h));}
size_t list_files(const char* dir,DirEntry* out,size_t cap){
  std::error_code e;size_t n=0;
  for(std::filesystem::directory_iterator it(dir,e),end;!e&&it!=end&&n<cap;it.increment(e)){
    if(!it->is_regular_file(e))continue;const auto name=it->path().filename().string();
    std::snprintf(out[n].name,sizeof(out[n].name),"%s",name.c_str());out[n].size=uint32_t(it->file_size(e));++n;
  }return n;
}
bool remove_file(const char* p){std::error_code e;return std::filesystem::remove(p,e)&&!e;}
FileOps file_ops(){FileOps o{};o.ready=ready;o.make_dir=make_dir;o.open=open_file;o.write=write_file;o.read=read_file;
  o.seek=seek_file;o.file_size=file_size;o.close=close_file;o.list=list_files;o.remove=remove_file;return o;}
orcsdr::acars_ui::MessageRecord convert(const Record& r){
  orcsdr::acars_ui::MessageRecord out{};const auto& m=r.message;
  out.source=orcsdr::acars_ui::SourceKind::live;out.integrity=m.corrected?orcsdr::acars_ui::Integrity::crc_corrected:orcsdr::acars_ui::Integrity::crc_verified;
  out.corrected_bits=m.corrected_bits;out.end_marker=m.end_marker;out.mode=m.mode;out.acknowledgement=m.acknowledgement;out.block_id=m.block_id;
  std::memcpy(out.label,m.label,2);std::memcpy(out.tail,m.tail,7);std::memcpy(out.flight,m.flight,6);
  out.frequency_hz=m.frequency_hz;out.uptime_ms=uint32_t(r.monotonic_ns/1000000u);out.raw_bytes=m.raw_bytes;
  out.text_length=std::min<uint16_t>(m.text_length,orcsdr::acars::kMaxBlockBytes);out.has_text=out.text_length!=0;
  std::memcpy(out.text,m.text,out.text_length);orcsdr::acars_ui::sanitize_preview(out.preview,sizeof(out.preview),out.text,out.text_length);return out;
}
Record convert(const orcsdr::acars_ui::MessageRecord& m){
  Record out{};out.serial=m.seq;out.monotonic_ns=uint64_t(m.uptime_ms)*1000000u;out.unread=false;
  out.message.frequency_hz=m.frequency_hz;out.message.corrected=m.integrity==orcsdr::acars_ui::Integrity::crc_corrected;
  out.message.corrected_bits=m.corrected_bits;out.message.end_marker=m.end_marker;out.message.mode=m.mode;
  out.message.acknowledgement=m.acknowledgement;out.message.block_id=m.block_id;out.message.raw_bytes=m.raw_bytes;
  std::memcpy(out.message.label,m.label,2);std::memcpy(out.message.tail,m.tail,7);std::memcpy(out.message.flight,m.flight,6);
  out.message.text_length=std::min<uint16_t>(m.text_length,orcsdr::acars::kMaxBlockBytes);std::memcpy(out.message.text,m.text,out.message.text_length);return out;
}
}
bool ArchiveWorker::start(const std::filesystem::path& directory){stop();failed_=false;dropped_=0;running_=true;worker_=std::thread(&ArchiveWorker::run,this,directory);return true;}
void ArchiveWorker::stop(){running_=false;wake_.notify_all();if(worker_.joinable())worker_.join();}
bool ArchiveWorker::enqueue(const Record& r){std::lock_guard<std::mutex> lock(mutex_);if(held_==kQueue){++dropped_;return false;}queue_[head_]=r;head_=(head_+1)%kQueue;++held_;wake_.notify_one();return true;}
void ArchiveWorker::run(std::filesystem::path directory){
  orcsdr::acars_archive::Logger logger(&store_,directory.string().c_str());{
    std::lock_guard<std::mutex> lock(mutex_);logger.begin(file_ops(),0);logger.load_history();
  }
  for(;;){Record r{};bool have=false;{
      std::unique_lock<std::mutex> lock(mutex_);wake_.wait_for(lock,std::chrono::milliseconds(100),[&]{return !running_||held_;});
      if(held_){r=queue_[tail_];tail_=(tail_+1)%kQueue;--held_;have=true;}
      else if(!running_)break;
    }
    const auto ms=uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    if(have){std::lock_guard<std::mutex> lock(mutex_);logger.log(convert(r),ms);}logger.service(ms);
  }
  logger.end(0);failed_=logger.stats().state==orcsdr::acars_archive::LogState::write_error;
}
size_t ArchiveWorker::recent(size_t offset,Record* out,size_t capacity,size_t* total)const{
  std::lock_guard<std::mutex> lock(mutex_);std::array<const orcsdr::acars_ui::MessageRecord*,kHistory> rows{};
  orcsdr::acars_ui::ArchiveQuery q{};size_t all=0;const size_t n=store_.query(q,offset,rows.data(),std::min(capacity,rows.size()),&all);
  for(size_t i=0;i<n;++i)out[i]=convert(*rows[i]);if(total)*total=all;return n;
}
}
