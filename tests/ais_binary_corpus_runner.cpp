// Independent real NMEA payload regression: feed 2025 worldwide corpus output
// through OrcSDR's original binary-app parser. No real RF CRC or IQ involved.
#include "ais_binary_app.hpp"
#include "ais_protocol.hpp"
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
uint64_t numeric(const std::string& s) {
  return s.empty()?0:std::stoull(s);
}
std::vector<std::string> tabs(const std::string& line) {
  std::vector<std::string> v;std::stringstream ss(line);std::string part;
  while(std::getline(ss,part,'\t'))v.push_back(part);
  return v;
}
std::vector<uint8_t> unhex(const std::string& s) {
  if(s.size()%2)throw std::runtime_error("odd hex");
  std::vector<uint8_t> out(s.size()/2);
  for(size_t i=0;i<out.size();++i) {
    const auto hex=[&](char c)->uint8_t {
      if(c>='0'&&c<='9')return static_cast<uint8_t>(c-'0');
      if(c>='a'&&c<='f')return static_cast<uint8_t>(c-'a'+10);
      if(c>='A'&&c<='F')return static_cast<uint8_t>(c-'A'+10);
      throw std::runtime_error("nonhex");
    };
    out[i]=static_cast<uint8_t>((hex(s[2*i])<<4)|hex(s[2*i+1]));
  }
  return out;
}
}
int main(int argc,char** argv) {
  if(argc<2||argc>3){std::fprintf(stderr,"usage: ais-binary-corpus corpus.tsv [minimum-records]\n");return 2;}
  std::ifstream file(argv[1]);
  if(!file){std::fprintf(stderr,"cannot open corpus TSV\n");return 2;}
  size_t complete=0,known=0,unknown=0,unstructured=0,truncated=0,
         not_binary=0,protocol_length_rejected=0;
  std::string line;
  std::getline(file,line); // header
  if(line!="type\tmmsi\tdac\tfi\tbits\thex\tsource\trow")return 2;
  try {
    while(std::getline(file,line)) {
      if(line.empty())continue;
      const auto cols=tabs(line);
      if(cols.size()!=8)throw std::runtime_error("wrong column count");
      const auto type=static_cast<uint8_t>(numeric(cols[0]));
      const auto mmsi=static_cast<uint32_t>(numeric(cols[1]));
      const size_t bits=static_cast<size_t>(numeric(cols[4]));
      auto bytes=unhex(cols[5]);
      if(bits<38||bits>orcsdr::ais::app::kMaxBits||bytes.size()!=(bits+7)/8)
        throw std::runtime_error("invalid payload size");
      orcsdr::ais::app::Result result{};
      auto st=orcsdr::ais::app::decode(bytes.data(),bits,&result);
      if(result.message_type!=type||result.source_id!=mmsi)
        throw std::runtime_error("original decoder header mismatch");
      if(type==6||type==8) {
        if(result.has_application_id) {
          if(result.dac!=numeric(cols[2])||result.fi!=numeric(cols[3]))
            throw std::runtime_error("original decoder DAC/FI mismatch");
        } else {
          throw std::runtime_error("missing DAC/FI in complete binary message");
        }
      }
      switch(st) {
        case orcsdr::ais::app::Status::decoded:++known;break;
        case orcsdr::ais::app::Status::preserved_unknown:++unknown;break;
        case orcsdr::ais::app::Status::unstructured:++unstructured;break;
        case orcsdr::ais::app::Status::truncated:++truncated;break;
        case orcsdr::ais::app::Status::not_binary:++not_binary;break;
        default:throw std::runtime_error("binary app rejected valid NMEA header");
      }
      orcsdr::ais::Message packet{};
      const auto parsed=orcsdr::ais::parse(bytes.data(),bits,&packet);
      if(parsed==orcsdr::ais::ParseStatus::invalid_length)++protocol_length_rejected;
      else if(parsed==orcsdr::ais::ParseStatus::ok) {
        if(packet.type!=type||packet.source_id!=mmsi)
          throw std::runtime_error("core parser header mismatch");
      } else throw std::runtime_error("core parser rejected valid message header");
      ++complete;
    }
  } catch(const std::exception& e) {
    std::fprintf(stderr,"corpus failed after %zu records: %s\n",complete,e.what());
    return 1;
  }
  std::printf("Worldwide 2025 NMEA corpus: %zu complete, %zu known DAC/FI, %zu unknown preserved, "
              "%zu unstructured, %zu known-format truncated, %zu nonbinary, %zu parser-length rejections\n",
              complete,known,unknown,unstructured,truncated,not_binary,protocol_length_rejected);
  const size_t minimum=(argc==3?static_cast<size_t>(std::stoull(argv[2])):9500u);
  if(complete<minimum)return 1;
  return 0;
}
