#include "settings.hpp"
#include "waterfall_style.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace orc::zero {
namespace {
bool dec(const std::string& v,long long& n){
 if(v.empty())return false;
 try {size_t pos=0; n=std::stoll(v,&pos,10);return pos==v.size();}
 catch(...) {return false;}
}
bool device_name(const std::string& s){
 if(s.empty()||s.size()>96)return false;
 for(char c:s)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
   (c>='0'&&c<='9')||c==':'||c=='_'||c=='-'||c=='.'||c==','||c=='/'))return false;
 return true;
}
}
bool validate_settings(const UserSettings& s){
 return s.frequency_hz>=87500000&&s.frequency_hz<=108000000&&s.volume<=100&&s.palette<orcsdr::waterfall_style::kPaletteCount&&
  s.step_index<5&&s.gain_tenth_db>=-200&&s.gain_tenth_db<=900&&device_name(s.audio_device);
}
std::string settings_path(){
 const char* xdg=std::getenv("XDG_CONFIG_HOME");
 if(xdg&&*xdg&&xdg[0]=='/')return std::string(xdg)+"/orcsdr/cardputer-zero.conf";
 const char* home=std::getenv("HOME");
 return std::string(home&&*home?home:".")+"/.config/orcsdr/cardputer-zero.conf";
}
bool load_settings(const std::string& filename,UserSettings& out){
 std::ifstream input(filename);
 if(!input)return false;
 UserSettings next{};std::string line;bool version=false;
 while(std::getline(input,line)){
  if(line.empty()||line[0]=='#')continue;
  if(line.size()>128)return false;
  const auto pos=line.find('=');if(pos==std::string::npos)return false;
  const auto key=line.substr(0,pos),value=line.substr(pos+1);
  long long n=0;
  if(key=="audio_device"){next.audio_device=value;continue;}
  if(!dec(value,n))return false;
  if(key=="version"){if(n!=1)return false;version=true;}
  else if(key=="frequency_hz"){if(n<0||n>UINT32_MAX)return false;next.frequency_hz=static_cast<std::uint32_t>(n);}
  else if(key=="volume"){if(n<0||n>100)return false;next.volume=unsigned(n);}
  else if(key=="palette"){if(n<0||n>=orcsdr::waterfall_style::kPaletteCount)return false;next.palette=unsigned(n);}
  else if(key=="step_index"){if(n<0||n>4)return false;next.step_index=unsigned(n);}
  else if(key=="mute"){if(n<0||n>1)return false;next.mute=n==1;}
  else if(key=="gain_auto"){if(n<0||n>1)return false;next.gain_auto=n==1;}
  else if(key=="gain_tenth_db"){if(n<-200||n>900)return false;next.gain_tenth_db=int(n);}
  else return false;
 }
 if(!input.eof()||!version||!validate_settings(next))return false;
 out=next;return true;
}
bool save_settings(const std::string& filename,const UserSettings& s){
 if(!validate_settings(s))return false;
 namespace fs=std::filesystem;
 const fs::path p(filename);
 std::error_code ec;fs::create_directories(p.parent_path(),ec);if(ec)return false;
 const auto tmp=p.string()+".tmp."+std::to_string(getpid());
 const int fd=::open(tmp.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600);
 if(fd<0)return false;
 char buf[512]{};
 const int len=std::snprintf(buf,sizeof(buf),
  "version=1\nfrequency_hz=%u\nvolume=%u\npalette=%u\nstep_index=%u\nmute=%d\ngain_auto=%d\ngain_tenth_db=%d\naudio_device=%s\n",
  s.frequency_hz,s.volume,s.palette,s.step_index,s.mute?1:0,s.gain_auto?1:0,s.gain_tenth_db,s.audio_device.c_str());
 bool ok=len>0&&len<int(sizeof(buf));
 size_t at=0;
 while(ok&&at<size_t(len)){
  const ssize_t got=::write(fd,buf+at,size_t(len)-at);
  if(got<0&&errno==EINTR)continue;
  if(got<=0){ok=false;break;}
  at+=size_t(got);
 }
 if(ok&&::fsync(fd)!=0)ok=false;
 if(::close(fd)!=0)ok=false;
 if(ok&&::rename(tmp.c_str(),filename.c_str())!=0)ok=false;
 if(!ok) ::unlink(tmp.c_str());
 return ok;
}
}