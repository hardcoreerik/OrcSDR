#pragma once
#include "sstv_receiver.hpp"
#include <cstdio>
#include <algorithm>
#include <fstream>
#include <vector>
#include <filesystem>
#include <string>
#include <unistd.h>

namespace orc::zero {
inline bool save_completed_sstv(const orcsdr::sstv::Image& frame,
                                const std::filesystem::path& directory) {
  if(!frame.pixels || frame.partial || !frame.width || !frame.height ||
     frame.decoded_rows!=frame.height)return false;
  std::error_code error;
  std::filesystem::create_directories(directory,error);
  if(error)return false;
  auto temporary=(directory/("sstv-"+std::to_string(frame.frequency_hz)+"-XXXXXX")).string();
  const int fd=::mkstemp(temporary.data());
  if(fd<0)return false;
  FILE* output=::fdopen(fd,"wb");
  bool ok=false;
  if(output){
    ok=std::fprintf(output,"P6\n%u %u\n255\n",unsigned(frame.width),unsigned(frame.height))>0;
    for(size_t i=0;ok&&i<size_t(frame.width)*frame.height;++i){
      const uint16_t p=frame.pixels[i];
      const unsigned char rgb[]={static_cast<unsigned char>(((p>>11)&31)*255/31),
        static_cast<unsigned char>(((p>>5)&63)*255/63),static_cast<unsigned char>((p&31)*255/31)};
      ok=std::fwrite(rgb,1,sizeof(rgb),output)==sizeof(rgb);
    }
    if(std::fclose(output)!=0)ok=false;
  }else ::close(fd);
  if(ok)ok=::link(temporary.c_str(),(temporary+".ppm").c_str())==0;
  ::unlink(temporary.c_str());
  return ok;
}
inline std::vector<std::filesystem::path> saved_sstv_images(const std::filesystem::path& directory) {
  std::vector<std::filesystem::path> paths;
  std::error_code error;
  for(std::filesystem::directory_iterator it(directory,error),end;!error&&it!=end;it.increment(error)){
    const auto path=it->path();
    if(path.extension()!=".ppm"||path.filename().string().rfind("sstv-",0)!=0||
       !std::filesystem::is_regular_file(it->symlink_status(error)))continue;
    paths.push_back(path);
    // Retain a bounded, deterministic set for the handheld gallery.
    if(paths.size()>64)paths.erase(std::min_element(paths.begin(),paths.end()));
  }
  std::sort(paths.rbegin(),paths.rend());
  return paths;
}
inline bool read_saved_sstv(const std::filesystem::path& path,
                            std::vector<uint16_t>& pixels,unsigned& width,unsigned& height) {
  std::ifstream in(path,std::ios::binary);
  char magic[3]{};
  unsigned maximum=0;
  if(!in.getline(magic,sizeof(magic))||std::string(magic)!="P6"||
     !(in>>width>>height>>maximum)||in.get()!='\n'||
     !width||width>800||!height||height>800||maximum!=255)return false;
  pixels.resize(size_t(width)*height);
  for(auto& pixel:pixels){
    unsigned char rgb[3];
    if(!in.read(reinterpret_cast<char*>(rgb),3))return false;
    pixel=uint16_t(((unsigned(rgb[0])*31+127)/255)<<11 |
                   ((unsigned(rgb[1])*63+127)/255)<<5 |
                   ((unsigned(rgb[2])*31+127)/255));
  }
  return true;
}
}
