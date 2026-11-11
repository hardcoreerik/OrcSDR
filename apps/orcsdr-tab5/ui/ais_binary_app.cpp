#include "ais_binary_app.hpp"

namespace orcsdr::ais::app {
namespace {
const Descriptor kRegistry[]={
  {6,1,16,Kind::imo_persons,"IMO persons on board",
   "https://academy.iala.int/asm/number-persons-board-3/"},
  {6,200,55,Kind::inland_persons,"EU inland persons on board",
   "https://www.iala.int/asm/number-persons-board/"},
  {8,200,10,Kind::inland_vessel,"EU inland static/voyage data",
   "https://www.iala.int/asm/inland-ship-static-voyage-related-data/"}
};
bool extract(const uint8_t* b,size_t total,size_t off,unsigned bits,uint32_t* out) {
  if(!b||!out||bits>32||off>total||bits>total-off)return false;
  uint32_t value=0;
  for(unsigned i=0;i<bits;++i) {
    const size_t bit=off+i;
    value=(value<<1)|((b[bit/8]>>(7-bit%8))&1u);
  }
  *out=value;
  return true;
}
void preserve(const uint8_t* b,size_t off,size_t count,uint8_t* output) {
  for(size_t i=0;i<count;++i) {
    const size_t src=off+i;
    output[i/8]|=static_cast<uint8_t>(((b[src/8]>>(7-src%8))&1u)<<(7-i%8));
  }
}
char ascii6(uint32_t c) {
  const char x=static_cast<char>(c<32?c+64:c);
  return x=='@'?' ':x;
}
} // namespace

size_t descriptor_count(){return sizeof(kRegistry)/sizeof(kRegistry[0]);}
const Descriptor* descriptor_at(size_t i){return i<descriptor_count()?&kRegistry[i]:nullptr;}
const Descriptor* find(uint8_t type,uint16_t dac,uint8_t fi) {
  for(const auto& d:kRegistry)if(d.message_type==type&&d.dac==dac&&d.fi==fi)return &d;
  return nullptr;
}
Status decode(const uint8_t* data,size_t bits,Result* result) {
  if(!data||!result||bits<38||bits>kMaxBits)return Status::invalid_input;
  Result r{};
  uint32_t v=0;
  extract(data,bits,0,6,&v);r.message_type=static_cast<uint8_t>(v);
  extract(data,bits,8,30,&r.source_id);
  if(r.message_type!=6&&r.message_type!=8&&r.message_type!=25&&r.message_type!=26) {
    r.status=Status::not_binary;*result=r;return r.status;
  }
  size_t offset=0,end=bits;
  bool structured=true;
  if(r.message_type==6) {
    offset=72;r.addressed=r.has_destination=true;
    if(!extract(data,bits,40,30,&r.destination_id)) {
      r.status=Status::truncated;*result=r;return r.status;
    }
  } else if(r.message_type==8) {
    offset=40;
  } else {
    if(!extract(data,bits,38,1,&v)){r.status=Status::truncated;*result=r;return r.status;}
    r.addressed=r.has_destination=(v!=0);
    if(!extract(data,bits,39,1,&v)){r.status=Status::truncated;*result=r;return r.status;}
    structured=(v!=0);
    offset=40;
    if(r.addressed) {
      if(!extract(data,bits,offset,30,&r.destination_id)) {
        r.status=Status::truncated;*result=r;return r.status;
      }
      offset+=30;
    }
    // Message 26 reserves the final 20 bits for the radio communication state.
    if(r.message_type==26) {
      if(bits<offset+20){r.status=Status::truncated;*result=r;return r.status;}
      end=bits-20;
    }
  }
  if(offset>end||(structured&&end-offset<16)) {
    r.status=Status::truncated;*result=r;return r.status;
  }
  if(structured) {
    extract(data,bits,offset,10,&v);r.dac=static_cast<uint16_t>(v);
    extract(data,bits,offset+10,6,&v);r.fi=static_cast<uint8_t>(v);
    r.has_application_id=true;
    offset+=16;
  }
  if(end<offset||end-offset>kMaxBits) {
    r.status=Status::truncated;*result=r;return r.status;
  }
  r.app_bits=static_cast<uint16_t>(end-offset);
  preserve(data,offset,r.app_bits,r.app_raw);
  if(!structured) {
    r.status=Status::unstructured;*result=r;return r.status;
  }
  const Descriptor* descriptor=find(r.message_type,r.dac,r.fi);
  if(!descriptor) {
    r.status=Status::preserved_unknown;*result=r;return r.status;
  }
  r.kind=descriptor->kind;
  // Fail closed on incomplete *known* formats. The exact bits remain preserved.
  if(r.kind==Kind::imo_persons) {
    if(r.app_bits<48){r.status=Status::truncated;*result=r;return r.status;}
    extract(data,bits,offset,13,&v);
    r.persons=static_cast<uint16_t>(v);
    r.persons_known=v!=0;r.persons_at_least_8191=v==8191;
  } else if(r.kind==Kind::inland_persons) {
    if(r.app_bits<80){r.status=Status::truncated;*result=r;return r.status;}
    extract(data,bits,offset,8,&v);r.crew=static_cast<uint16_t>(v);r.crew_known=v!=255;
    extract(data,bits,offset+8,13,&v);
    r.passengers=static_cast<uint16_t>(v);r.passengers_known=v!=8191;
    extract(data,bits,offset+21,8,&v);
    r.personnel=static_cast<uint16_t>(v);r.personnel_known=v!=255;
  } else if(r.kind==Kind::inland_vessel) {
    if(r.app_bits<112){r.status=Status::truncated;*result=r;return r.status;}
    for(unsigned i=0;i<8;++i) {
      extract(data,bits,offset+6*i,6,&v);
      r.eni[i]=ascii6(v);
    }
    r.eni[8]='\0';
    extract(data,bits,offset+48,13,&v);
    r.length_dm=static_cast<uint16_t>(v);r.length_known=(v>=1&&v<=8000);
    extract(data,bits,offset+61,10,&v);
    r.beam_dm=static_cast<uint16_t>(v);r.beam_known=(v>=1&&v<=1000);
    extract(data,bits,offset+71,14,&v);r.ship_type=static_cast<uint16_t>(v);
    extract(data,bits,offset+85,3,&v);r.hazardous_cargo=static_cast<uint8_t>(v);
    extract(data,bits,offset+88,11,&v);
    r.draught_cm=static_cast<uint16_t>(v);r.draught_known=(v>=1&&v<=2000);
    extract(data,bits,offset+99,2,&v);r.loaded=static_cast<uint8_t>(v);
    extract(data,bits,offset+101,3,&v);r.quality_flags=static_cast<uint8_t>(v);
  }
  r.status=Status::decoded;*result=r;return r.status;
}
} // namespace orcsdr::ais::app
