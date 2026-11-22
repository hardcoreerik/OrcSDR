#pragma once
#include "compact/compact_renderer.hpp"
#include "ft8_spectral_fft.hpp"
#include "ft8_model.hpp"
#include "ft8_world_data.hpp"
#include "ft8_world_projection.hpp"
#include <cstring>
#include <ctime>
namespace orc::zero {
class Ft8Visuals {
 public:
  static constexpr size_t width=320,height=68,n=1920,bins=481;
  Ft8Visuals(){
    ready_=orcsdr::ftx::spectral_fft::make_plan(&plan_,n);
    for(size_t j=0;j<n;++j)window_[j]=float(.5-.5*std::cos(6.283185307179586*j/(n-1)));
  }
  void clear(){used_=averages_=rows_=0;history_={};floor_ready_=false;++revision_;}
  void consume(const int16_t* pcm,size_t count,bool reset){
    if(reset){used_=averages_=0;}
    if(!ready_||!pcm)return;
    while(count){
      const auto take=std::min(count,n-used_);
      std::copy_n(pcm,take,samples_.begin()+used_);used_+=take;pcm+=take;count-=take;
      if(used_!=n)continue;
      for(size_t j=0;j<n;++j)weighted_[j]=int16_t(std::lround(samples_[j]*window_[j]));
      orcsdr::ftx::spectral_fft::power_bins(plan_,&scratch_,weighted_.data(),n,0,bins,power_.data());
      if(!averages_)sum_=power_;else for(size_t j=0;j<bins;++j)sum_[j]+=power_[j];
      used_=0;
      if(++averages_<2)continue;
      averages_=0;
      // One row is the mean of the two spectra (320 ms of audio), not their sum,
      // so the dB values below are independent of the averaging depth.
      for(size_t j=0;j<bins;++j)sum_[j]*=0.5f;
      // Noise floor from the lower fifth of the band, ignoring the audio filter
      // roll-off near DC. A per-row floor made whole rows jump in brightness as
      // the percentile moved; an EMA keeps the display steady, which is what
      // makes weak traces readable.
      std::copy(sum_.begin()+kFloorFrom,sum_.end(),sorted_.begin());
      const size_t span=bins-kFloorFrom;
      std::nth_element(sorted_.begin(),sorted_.begin()+span/5,sorted_.begin()+span);
      const float floor_now=10*std::log10(std::max(1e-12f,sorted_[span/5]/2147483648.f));
      if(!floor_ready_){floor_db_=floor_now;floor_ready_=true;}
      else floor_db_=floor_db_*0.75f+floor_now*0.25f;
      auto& row=history_[rows_%height];
      for(size_t x=0;x<width;++x){
        const size_t lo=x*(bins-1)/width,hi=std::min(bins-1,(x+1)*(bins-1)/width);
        // Peak within the pixel rather than the mean: one FT8 tone is 6.25 Hz and
        // a pixel is 9.4 Hz, so averaging would dilute a real tone with its
        // neighbours.
        float peak=0;for(size_t b=lo;b<=hi;++b)peak=std::max(peak,sum_[b]);
        const float db=10*std::log10(std::max(1e-12f,peak/2147483648.f));
        // 38 dB above the floor fills the palette. The previous 48 dB spent most
        // of the ramp on levels this receiver never produces, which is why traces
        // never left the dark end; a gentle gamma lifts the weak half without
        // flattening the strong signals.
        const float level=std::clamp((db-floor_db_+6.f)/38.f,0.f,1.f);
        row[x]=uint8_t(std::lround(std::pow(level,0.85f)*255.f));
      }
      ++rows_;++revision_;
    }
  }
  void draw(CompactRenderer& frame,unsigned palette,int top=45)const{
    frame.rect(0,top,width,height,CompactRenderer::bg);
    const auto count=std::min<size_t>(rows_,height);
    for(size_t y=0;y<count;++y){const auto& row=history_[(rows_-1-y)%height];
      for(size_t x=0;x<width;++x)frame.point(int(x),top+int(y),orcsdr::waterfall_style::color565(uint8_t(palette),row[x]/255.f));
    }
    if(!rows_)frame.text(4,top+20,"WAITING FOR LIVE USB AUDIO",CompactRenderer::muted);
  }
  uint64_t revision()const{return revision_;}
  size_t rows()const{return rows_;}
  const auto& newest()const{return history_[(rows_?rows_-1:0)%height];}
 private:
  // Lowest bin used to estimate the noise floor: 24 x 6.25 Hz = 150 Hz, above
  // the audio filter roll-off that would otherwise drag the estimate down.
  static constexpr size_t kFloorFrom=24;
  orcsdr::ftx::spectral_fft::Plan plan_;
  orcsdr::ftx::spectral_fft::Scratch scratch_;
  std::array<float,n> window_{};
  std::array<int16_t,n> samples_{},weighted_{};
  std::array<float,bins> power_{},sum_{},sorted_{};
  std::array<std::array<uint8_t,width>,height> history_{};
  size_t used_=0,averages_=0,rows_=0;uint64_t revision_=0;bool ready_=false;
  float floor_db_=0;bool floor_ready_=false;
};
class Ft8WorldMap {
 public:
  static constexpr int top=30,width=216,height=108;
  void clear(){store_.clear();heard_count_=0;++revision_;}
  void add(const orcsdr::ft8::Decode& decode){
    store_.append(decode);++revision_;
    if(!decode.callsign[0])return;
    size_t index=0;
    while(index<heard_count_&&std::strncmp(heard_[index].callsign,decode.callsign,sizeof(decode.callsign)))++index;
    auto updated=decode;
    if(index<heard_count_&&!orcsdr::ft8::maidenhead_valid(updated.grid))
      std::memcpy(updated.grid,heard_[index].grid,sizeof(updated.grid));
    if(index==heard_count_&&heard_count_<heard_.size())++heard_count_;
    for(size_t n=std::min(index,heard_.size()-1);n>0;--n)heard_[n]=heard_[n-1];
    heard_[0]=updated;
  }
  size_t heard_count()const{return heard_count_;}
  const orcsdr::ft8::Decode& heard(size_t index)const{return heard_.at(index);}
  size_t located()const{return store_.grid_count();}
  uint64_t revision()const{return revision_;}
  void draw(CompactRenderer& frame){
    if(!basemap_ready_){
      // Build once, then preserve this static map across decode/clock updates.
      frame.rect(0,top,width,height,0x0006);
      for(int lon=-120;lon<=120;lon+=60){int x=0,y=0;orcsdr::ft8::world_map::project(0,lon,width,height,&x,&y);frame.line(x,top,x,top+height-1,0x1088);}
      for(int lat=-60;lat<=60;lat+=30){int x=0,y=0;orcsdr::ft8::world_map::project(lat,0,width,height,&x,&y);frame.line(0,top+y,width-1,top+y,0x1088);}
      for(size_t k=0;k<orcsdr::ft8::kWorldSegmentCount;++k){
        const auto& s=orcsdr::ft8::kWorldSegments[k];
        for(size_t j=1;j<s.count;++j){
          const size_t a=(s.start+j-1)*2,b=(s.start+j)*2;int x1,y1,x2,y2;
          if(!orcsdr::ft8::world_map::project(orcsdr::ft8::kWorldPoints[a+1]/100.,orcsdr::ft8::kWorldPoints[a]/100.,width,height,&x1,&y1)||
             !orcsdr::ft8::world_map::project(orcsdr::ft8::kWorldPoints[b+1]/100.,orcsdr::ft8::kWorldPoints[b]/100.,width,height,&x2,&y2)||std::abs(x2-x1)>width/2)continue;
          frame.line(x1,top+y1,x2,top+y2,s.kind?0x2390:CompactRenderer::cyan);
        }
      }
      for(int y=0;y<height;++y)std::copy_n(frame.pixels.begin()+(top+y)*320,width,basemap_.begin()+y*width);
      basemap_ready_=true;
    }else for(int y=0;y<height;++y)std::copy_n(basemap_.begin()+y*width,width,frame.pixels.begin()+(top+y)*320);
    frame.rect(width,top,320-width,height,CompactRenderer::panel);
    frame.text(width+5,top+2,"LAST HEARD",CompactRenderer::cyan);
    for(size_t k=0;k<heard_count_;++k){
      const auto& d=heard_[k];const int y=top+14+int(k)*18;
      // Callsign on the left, the UTC time of the decode on the right. Length is
      // capped so a long call cannot run into the clock column.
      frame.text(width+5,y,std::string(d.callsign,strnlen(d.callsign,sizeof(d.callsign))),CompactRenderer::white,1,9);
      char clock[8]="-:--";
      if(d.utc_epoch){
        const std::time_t when=std::time_t(d.utc_epoch);
        std::tm utc{};
        if(gmtime_r(&when,&utc))std::snprintf(clock,sizeof(clock),"%02d:%02d",utc.tm_hour,utc.tm_min);
      }
      // The sidebar is 104px wide: the clock starts far enough left that five
      // characters land inside the panel instead of being clipped by it.
      frame.text(width+71,y,clock,CompactRenderer::muted,1,5);
      // Second line: grid, then what the message was. CQ and QSO are the two
      // things worth telling apart at a glance, so they are coloured.
      const char* kind=d.kind==orcsdr::ft8::DecodeKind::cq?"CQ":d.kind==orcsdr::ft8::DecodeKind::qso?"QSO":d.kind==orcsdr::ft8::DecodeKind::free_text?"TXT":"";
      const uint16_t kind_colour=d.kind==orcsdr::ft8::DecodeKind::cq?CompactRenderer::green:d.kind==orcsdr::ft8::DecodeKind::qso?CompactRenderer::cyan:CompactRenderer::muted;
      frame.text(width+5,y+8,orcsdr::ft8::maidenhead_valid(d.grid)?d.grid:"NO GRID",CompactRenderer::muted,1,7);
      if(*kind)frame.text(width+55,y+8,kind,kind_colour,1,3);
    }
    if(!heard_count_)frame.text(width+5,top+20,"WAITING",CompactRenderer::muted);
    frame.rect(0,139,320,7,CompactRenderer::bg);
    if(const auto* last=store_.newest())
      frame.text(4,139,std::string(last->message,strnlen(last->message,sizeof(last->message))),CompactRenderer::white,1,52);
    else frame.text(4,139,"WAITING FOR DECODED MESSAGES",CompactRenderer::muted);
    for(size_t k=0;k<store_.size();++k){
      const auto* d=store_.newest(k);orcsdr::ft8::GeoPoint p;int x,y;
      if(!orcsdr::ft8::maidenhead_center(d->grid,&p)||!orcsdr::ft8::world_map::project(p.latitude,p.longitude,width,height,&x,&y))continue;
      frame.rect(std::clamp(x-1,0,width-3),std::clamp(top+y-1,top,top+height-3),3,3,CompactRenderer::yellow);
    }


  }
 private:
  std::array<orcsdr::ft8::Decode,5> heard_{};
  size_t heard_count_=0;
  orcsdr::ft8::DecodeStore store_;
  std::array<uint16_t,width*height> basemap_{};
  bool basemap_ready_=false;uint64_t revision_=0;
};
}
