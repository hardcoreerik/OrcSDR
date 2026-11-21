#include "web_server.hpp"
#include <array>
#include <cerrno>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <unistd.h>

namespace orc::linux_sdr {
namespace {
struct Socket {
  int fd=-1;
  explicit Socket(int value=-1):fd(value){}
  ~Socket(){if(fd>=0)::close(fd);}
  Socket(const Socket&)=delete;Socket& operator=(const Socket&)=delete;
};
struct Asset {const char* file;const char* mime;bool gzip;};
const std::map<std::string,Asset> files={
 {"/",{"m5.html.gz","text/html; charset=utf-8",true}},
 {"/m5",{"m5.html.gz","text/html; charset=utf-8",true}},
 {"/m5/",{"m5.html.gz","text/html; charset=utf-8",true}},
 {"/m5/app.js",{"app.js.gz","text/javascript",true}},
 {"/m5/decoders.js",{"decoders.js.gz","text/javascript",true}},
 {"/m5/decoder_worker.js",{"decoder_worker.js.gz","text/javascript",true}},
 {"/m5/core.js",{"core.js.gz","text/javascript",true}},
 {"/m5/core.wasm",{"core.wasm.gz","application/wasm",true}},
 {"/m5/orcdec.js",{"orcdec.js.gz","text/javascript",true}},
 {"/m5/orcdec.wasm",{"orcdec.wasm.gz","application/wasm",true}},
 {"/m5/splash.jpg",{"splash.jpg","image/jpeg",false}}
};
void send_bytes(int fd,const char* data,std::size_t count) {
  while(count) {
    auto sent=::send(fd,data,count,MSG_NOSIGNAL);
    if(sent<0&&errno==EINTR)continue;
    if(sent<=0)throw std::runtime_error("HTTP send failed");
    data+=sent;count-=static_cast<std::size_t>(sent);
  }
}
void answer(int fd,int code,const std::string& type,const std::string& body,bool head,bool gz=false) {
  const char* reason=code==200?"OK":code==404?"Not Found":code==405?"Method Not Allowed":
                     code==501?"Not Implemented":"Service Unavailable";
  std::string response="HTTP/1.1 "+std::to_string(code)+" "+reason+"\r\n"
      "Content-Type: "+type+"\r\n"
      "Content-Length: "+std::to_string(body.size())+"\r\n"
      "Cache-Control: no-store\r\n"
      "X-Content-Type-Options: nosniff\r\n"
      "Connection: close\r\n";
  if(gz)response+="Content-Encoding: gzip\r\n";
  response+="\r\n";
  send_bytes(fd,response.data(),response.size());
  if(!head&&!body.empty())send_bytes(fd,body.data(),body.size());
}
std::string request_line(int fd) {
  std::string input;
  std::array<char,512> buffer{};
  while(input.size()<8192) {
    const auto n=::recv(fd,buffer.data(),buffer.size(),0);
    if(n<=0)break;
    input.append(buffer.data(),static_cast<std::size_t>(n));
    const auto end=input.find("\r\n");
    if(end!=std::string::npos)return input.substr(0,end);
  }
  return {};
}
bool get_asset(const std::string& root,const Asset& asset,std::string& out) {
  std::ifstream in(root+"/"+asset.file,std::ios::binary|std::ios::ate);
  if(!in)return false;
  const auto n=in.tellg();
  if(n<0||n>static_cast<std::streampos>(4*1024*1024))return false;
  out.resize(static_cast<std::size_t>(n));
  in.seekg(0);
  if(!out.empty())in.read(&out[0],static_cast<std::streamsize>(out.size()));
  return static_cast<bool>(in);
}
void handle(int fd,const std::string& root) {
  const auto line=request_line(fd);
  const auto start=line.find(' '),end=line.rfind(' ');
  if(start==std::string::npos||end==std::string::npos||start==end) {
    answer(fd,404,"application/json","{\"error\":\"invalid_request\"}",false);return;
  }
  const auto method=line.substr(0,start);
  const bool head=method=="HEAD";
  if(method!="GET"&&!head) {
    answer(fd,405,"application/json","{\"error\":\"read_only_preview\"}",false);return;
  }
  std::string path=line.substr(start+1,end-start-1);
  path=path.substr(0,path.find('?'));
  if(path=="/api/v1/health") {
    answer(fd,200,"application/json",
      "{\"ok\":true,\"service\":\"OrcSDR Linux Web preview\","
      "\"loopback_only\":true,\"radio_control\":false,"
      "\"radio_service_v1_deployed\":false}",head);return;
  }
  if(path.rfind("/api/",0)==0) {
    answer(fd,501,"application/json",
      "{\"error\":\"radio_api_not_implemented_on_linux\"}",head);return;
  }
  const auto it=files.find(path);
  if(it==files.end()) {
    answer(fd,404,"application/json","{\"error\":\"path_not_found\"}",head);return;
  }
  std::string data;
  if(!get_asset(root,it->second,data)) {
    answer(fd,503,"application/json","{\"error\":\"missing_web_assets\"}",head);return;
  }
  answer(fd,200,it->second.mime,data,head,it->second.gzip);
}
}
int run_loopback_web(const std::string& dir,std::uint16_t port,std::uint32_t max_requests) {
  if(dir.empty())throw std::invalid_argument("web asset directory required");
  Socket server(::socket(AF_INET,SOCK_STREAM,0));
  if(server.fd<0)throw std::runtime_error("cannot create HTTP socket");
  int reuse=1;
  (void)::setsockopt(server.fd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
  sockaddr_in address{};
  address.sin_family=AF_INET;
  address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
  address.sin_port=htons(port);
  if(::bind(server.fd,reinterpret_cast<sockaddr*>(&address),sizeof(address))!=0)
    throw std::runtime_error("loopback bind failed");
  if(::listen(server.fd,4)!=0)throw std::runtime_error("loopback listen failed");
  socklen_t len=sizeof(address);
  if(::getsockname(server.fd,reinterpret_cast<sockaddr*>(&address),&len)!=0)
    throw std::runtime_error("port readback failed");
  std::cout<<"loopback_http_listening=127.0.0.1:"<<ntohs(address.sin_port)<<" read_only=1"<<std::endl;
  for(std::uint32_t served=0;!max_requests||served<max_requests;++served) {
    Socket client(::accept(server.fd,nullptr,nullptr));
    if(client.fd<0){if(errno==EINTR)continue;throw std::runtime_error("accept failed");}
    timeval timeout{};timeout.tv_sec=2;
    (void)::setsockopt(client.fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    (void)::setsockopt(client.fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
    try {handle(client.fd,dir);}
    catch(const std::exception& e){std::cerr<<"web_client_error="<<e.what()<<'\n';}
  }
  return 0;
}
}
