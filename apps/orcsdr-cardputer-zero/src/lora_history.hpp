#pragma once

#include "lora_native_decoder.hpp"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <string>
#include <unistd.h>
#include <vector>

namespace orc::zero::lora_history {

inline std::filesystem::path path(){
  const char* data=std::getenv("XDG_DATA_HOME"),*home=std::getenv("HOME");
  const auto root=data&&data[0]=='/'?std::filesystem::path(data):home?std::filesystem::path(home)/".local/share":std::filesystem::path{"."};
  return root/"orcsdr/lora/packets.tsv";
}
inline std::string clean(std::string text){
  for(char& c:text)if(c<32||c>126||c=='\t')c='?';
  return text;
}
inline std::pair<std::string,std::string> format(const orcsdr::lora_native::Packet& packet){
  char header[64];std::snprintf(header,sizeof(header),"%08X > %08X PORT %u",packet.sender,packet.destination,packet.port);
  std::string text(packet.text,strnlen(packet.text,sizeof(packet.text)));text=clean(text);
  if(text.empty())text=packet.encrypted?"ENCRYPTED PACKET":"NO TEXT";
  return {header,text};
}
inline bool append(const std::filesystem::path& file,const orcsdr::lora_native::Packet& packet){
  std::error_code ec;std::filesystem::create_directories(file.parent_path(),ec);if(ec)return false;
  const int fd=::open(file.c_str(),O_WRONLY|O_CREAT|O_APPEND|O_CLOEXEC,0600);if(fd<0)return false;
  const auto [header,text]=format(packet);const std::string record=header+'\t'+text+'\n';size_t at=0;bool ok=true;
  while(at<record.size()){
    const auto wrote=::write(fd,record.data()+at,record.size()-at);
    if(wrote<0&&errno==EINTR)continue;
    if(wrote<=0){ok=false;break;}
    at+=size_t(wrote);
  }
  if(ok&&::fsync(fd)!=0)ok=false;
  if(::close(fd)!=0)ok=false;
  return ok;
}
inline std::vector<std::string> load(const std::filesystem::path& file,size_t packets=16){
  std::ifstream input(file);std::vector<std::pair<std::string,std::string>> records;std::string line;
  while(std::getline(input,line)){
    const auto split=line.find('\t');if(split==std::string::npos||!split||split+1>=line.size())continue;
    records.emplace_back(clean(line.substr(0,split)),clean(line.substr(split+1)));
    if(records.size()>packets)records.erase(records.begin());
  }
  std::vector<std::string> lines;lines.reserve(records.size()*2);
  for(auto it=records.rbegin();it!=records.rend();++it){lines.push_back(it->first);lines.push_back(it->second);}
  return lines;
}

} // namespace orc::zero::lora_history
