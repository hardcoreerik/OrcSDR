#pragma once
#include "adsb_renderer.hpp"
#include "acars_renderer.hpp"
#include "airband.hpp"
#include "linux_audio.hpp"
#include "iq_metrics.hpp"
#include "ft8_receiver.hpp"
#include "ft8_band.hpp"
#include "clock_control.hpp"
#include "ft8_visuals.hpp"
#include <ctime>
#include "compact/p25_screen.hpp"
#include "p25_voice.hpp"
#include <memory>
#include <cstring>
#include "sideband_pcm.hpp"
#include "cb_scanner.hpp"
#include "dead_air_screen.hpp"
#include "fm_dsp.h"
#include "pocsag_decoder_core.hpp"
#include "sstv_receiver.hpp"
#include "sstv_save.hpp"
#include <cstdlib>
#include "rtl_receiver.hpp"
#include <atomic>
#include <cstdio>
#include <sstream>
#include <string_view>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>

namespace orc::zero {
class DashboardSession {
 public:
  using Pixels=std::array<uint16_t,320*170>;
  struct MarineChannel { unsigned number; uint32_t hz; };
  // U.S. simplex voice presets from the USCG VHF channel table.
  inline static constexpr MarineChannel marine_channels_[]={
    {6,156300000},{9,156450000},{13,156650000},{16,156800000},
    {17,156850000},{68,156425000},{69,156475000},{71,156575000},
    {72,156625000},{73,156675000},{74,156725000},{77,156875000}
  };
  static constexpr unsigned marine_default_=3;
  static constexpr bool is_nfm(unsigned mode){return mode==7||mode==8;}
  ~DashboardSession(){stop();}
  unsigned mode() const {return mode_;}
  void stop(){
    running_=false;
    if(worker_.joinable())worker_.join();
    clock_.stop();
    mode_=0;
  }
  void start(unsigned mode,unsigned device,const std::string& audio,bool silent,uint32_t rf_center){
    stop();
    mode_=mode;
    error_.clear();
    ft8_band_=Ft8BandSelection{};
    ft8_applied_=0;
    ft8_header_dirty_=true;
    ft8_lines_.clear();
    ft8_last_utc_=0;
    ft8_drawn_=false;
    clock_open_=clock_entry_=false;clock_digits_.clear();clock_tick_=0;
    if(mode==15){clock_.start();ft8_visual_.clear();ft8_map_.clear();ft8_map_open_=false;ft8_log_offset_=0;ft8_visual_painted_=ft8_map_painted_=~uint64_t{0};ft8_visual_palette_=~0u;}
    if(mode==16)p25_=P25Screen{};
    if(mode==12)dead_air_=DeadAirScreen{};
    lab_={};
    wifi_rows_.clear();
    wifi_ready_=false;
    marine_index_=marine_default_;
    lab_target_=mode==8?marine_channels_[marine_index_].hz:mode==7?162400000:rf_center;
    if(mode==9)lab_target_=929662500;
    if(mode==10){
      lab_target_=145800000;
      sstv_index_=0;
      sstv_archived_=0;
      sstv_ready_=false;
      sstv_drawn_=false;
      sstv_rendered_=~uint64_t{0};
      ++sstv_revision_;
    }
    pager_text_.clear();
    pager_offset_=0;
    pager_stats_={};
    ++pager_revision_;
    weather_audio_=false;
    weather_drawn_=false;
    cb_channel_=orcsdr::cb::kHighwayChannel;
    if(mode==3||mode==11||mode==13||mode==14)am_.activate(mode==14?orcsdr::cb::kChannelsHz[cb_channel_]:mode==13?7100000:mode==11?1000000:airband::kGuardHz,mode==11,mode==13,mode==14);
    if(mode==2)ac_.receiver_connecting();
    running_=true;
    worker_=std::thread([=]{if(mode==6)run_wifi();else run(mode,device,audio,silent,rf_center);});
  }
  bool input(Key key,char digit){
    std::lock_guard<std::mutex> lock(mutex_);
    if(mode_==16)return p25_.input(key,digit);
    if(mode_==1){
      using A=adsb::Action;
      if(key==Key::escape&&!ads_.snapshot(now()).detail)return false;
      ads_.navigate(key==Key::up?A::up:key==Key::down?A::down:key==Key::enter?A::open_detail:key==Key::escape?A::back:A::none);
      return true;
    }
    if(mode_==2){
      ac_.service();
      if(key==Key::escape&&!ac_.snapshot().detail)return false;
      ac_.handle_compact_key(key);
      return true;
    }
    if(mode_==12){
      if(key==Key::escape)return false;
      if(key==Key::enter&&error_.empty())dead_air_.mark(uint32_t(now()/1000000));
      return true;
    }
    if(mode_==15){
      if(clock_open_){
        if(key==Key::escape){
          if(clock_entry_){clock_entry_=false;clock_digits_.clear();}
          else{clock_open_=false;ft8_render_epoch_=~uint64_t{0};ft8_header_dirty_=true;ft8_visual_painted_=ft8_map_painted_=~uint64_t{0};}
        }else if(key==Key::reconnect&&!clock_entry_)clock_.sync();
        else if(key==Key::frequency){clock_entry_=true;clock_digits_.clear();}
        else if(clock_entry_){
          if(key==Key::digit&&clock_digits_.size()<14&&digit>='0'&&digit<='9')clock_digits_+=digit;
          if(key==Key::backspace&&!clock_digits_.empty())clock_digits_.pop_back();
          if(key==Key::enter&&clock_.set(clock_digits_))clock_entry_=false;
        }
        clock_tick_=0;return true;
      }
      if(key==Key::mute){
        ft8_map_open_=!ft8_map_open_;ft8_screen_.rect(0,0,320,146,CompactRenderer::bg);ft8_map_painted_=ft8_visual_painted_=~uint64_t{0};
        ft8_render_epoch_=~uint64_t{0};ft8_header_dirty_=true;clock_tick_=0;return true;
      }
      if(key==Key::up||key==Key::down){
        const size_t maximum=ft8_lines_.size()>2?ft8_lines_.size()-2:0;
        if(key==Key::up&&ft8_log_offset_)--ft8_log_offset_;
        if(key==Key::down&&ft8_log_offset_<maximum)++ft8_log_offset_;
        ft8_render_epoch_=~uint64_t{0};return true;
      }
      if(key==Key::step){clock_open_=true;ft8_screen_.rect(0,18,320,152,CompactRenderer::bg);clock_tick_=0;return true;}
      if(key==Key::escape)return false;
      if(key==Key::left||key==Key::right){
        ft8_band_.step(key==Key::right);
        ft8_header_dirty_=true;
      }
      return true;
    }
    if(mode_==6)return key!=Key::escape;
    if(is_nfm(mode_)){
      if(mode_==8&&(key==Key::left||key==Key::right)){
        constexpr unsigned count=sizeof(marine_channels_)/sizeof(marine_channels_[0]);
        marine_index_=(marine_index_+(key==Key::left?count-1:1))%count;
        lab_target_=marine_channels_[marine_index_].hz;
        return true;
      }
      switch(key){
        case Key::left:lab_target_=lab_target_==162400000?162550000:lab_target_-25000;break;
        case Key::right:lab_target_=lab_target_==162550000?162400000:lab_target_+25000;break;
        case Key::volume_down:weather_volume_=std::max(0,weather_volume_-5);break;
        case Key::volume_up:weather_volume_=std::min(100,weather_volume_+5);break;
        case Key::mute:weather_muted_=!weather_muted_;break;
        case Key::escape:return false;
        default:break;
      }
      return true;
    }
    if(mode_==10){
      if(key==Key::escape)return false;
      if(key==Key::left||key==Key::right){
        const int step=key==Key::left?-12500:12500;
        lab_target_=static_cast<uint32_t>(std::clamp<int64_t>(int64_t(lab_target_)+step,144000000,148000000));
        sstv_ready_=false;
        ++sstv_revision_;
      }
      if(key==Key::up&&sstv_index_<sstv_archived_)++sstv_index_;
      if(key==Key::down&&sstv_index_)--sstv_index_;
      return true;
    }
    if(mode_==9){
      if(key==Key::escape)return false;
      if(key==Key::step)lab_target_=lab_target_>500000000?152007500:929662500;
      if(key==Key::left||key==Key::right){
        const int step=key==Key::left?-12500:12500;
        lab_target_=static_cast<uint32_t>(std::clamp<int64_t>(int64_t(lab_target_)+step,24000000,1766000000));
      }
      if(key==Key::up&&pager_offset_+5<pager_text_.size())++pager_offset_;
      if(key==Key::down&&pager_offset_)--pager_offset_;
      ++pager_revision_;
      return true;
    }
    if(mode_==5){
      if(key==Key::escape)return false;
      if(key==Key::left||key==Key::right){
        constexpr int64_t step=100000;
        lab_target_=static_cast<uint32_t>(std::clamp<int64_t>(
          int64_t(lab_target_)+(key==Key::left?-step:step),24000000,1766000000));
      }
      return true;
    }
    if(mode_==3||mode_==11||mode_==13||mode_==14){
      if(mode_==14&&(key==Key::left||key==Key::right)){
        constexpr auto count=orcsdr::cb::kChannelCount;
        cb_channel_=(cb_channel_+(key==Key::left?count-1:1))%count;
        am_.tune_direct(orcsdr::cb::kChannelsHz[cb_channel_]);
        return true;
      }
      if(mode_==14&&(key==Key::frequency||key==Key::digit||key==Key::point||key==Key::backspace||key==Key::enter))return true;
      if((mode_==13||mode_==14)&&key==Key::step){
        sw_choice_=orcsdr::shortwave_ssb::next_choice(sw_choice_);
        return true;
      }
      using A=airband::Action;
      switch(key){
        case Key::left:am_.action(A::tune_down);break;
        case Key::right:am_.action(A::tune_up);break;
        case Key::frequency:am_.action(A::frequency_entry);break;
        case Key::digit:am_.action(A::entry_digit,digit);break;
        case Key::point:am_.action(A::entry_point);break;
        case Key::backspace:am_.action(A::entry_backspace);break;
        case Key::enter:am_.action(A::entry_commit);break;
        case Key::escape:
          if(!am_.snapshot().entry_active)return false;
          am_.action(A::entry_cancel);break;
        case Key::volume_up:am_.action(A::volume_up);break;
        case Key::volume_down:am_.action(A::volume_down);break;
        case Key::mute:am_.action(A::mute);break;
        case Key::up:am_.action(A::squelch_up);break;
        case Key::down:am_.action(A::squelch_down);break;
        default:break;
      }
      return true;
    }
    return false;
  }
  const Pixels& render(unsigned palette,CompactRenderer& fallback){
    std::unique_lock<std::mutex> lock(mutex_);
    if(!error_.empty()){
      auto error=error_;
      lock.unlock();
      Snapshot state;
      state.page=Page::error;
      state.error=error;
      fallback.render(state);
      return fallback.pixels;
    }
    if(mode_==1){
      auto state=ads_.snapshot(now());lock.unlock();
      ads_render_.render(state,palette);return ads_render_.pixels();
    }
    if(mode_==2){
      ac_.service();auto state=ac_.snapshot();lock.unlock();
      ac_render_.render(state,now());return ac_render_.pixels();
    }
    if(mode_==16)return p25_.render();
    if(mode_==12){
      dead_air_.render();
      return dead_air_.frame().pixels;
    }
    if(mode_==15){
      const auto utc=std::time(nullptr);
      std::tm tm{};gmtime_r(&utc,&tm);
      const auto clock=clock_.snapshot();
      if(clock_open_){
        if(clock_tick_!=utc){
          ft8_screen_.rect(0,36,320,134,CompactRenderer::bg);
          ft8_screen_.text(4,39,"UTC CLOCK / FT8",CompactRenderer::green,2);
          char b[64];std::strftime(b,sizeof(b),"%Y-%m-%d %H:%M:%S UTC",&tm);
          ft8_screen_.text(4,61,b,CompactRenderer::white);
          ft8_screen_.text(4,74,clock.ntp?(clock.synced?"NTP SYNCHRONIZED":"NTP WAITING FOR NETWORK"):"MANUAL CLOCK - NTP OFF",clock.synced?CompactRenderer::green:CompactRenderer::yellow);
          ft8_screen_.text(4,87,"SERVER "+clock.server,CompactRenderer::cyan,1,51);
          if(clock_entry_){
            ft8_screen_.text(4,101,"YYYYMMDDHHMMSS UTC:",CompactRenderer::cyan);
            ft8_screen_.text(4,114,clock_digits_+"_",CompactRenderer::white,2);
            ft8_screen_.text(4,133,"ENTER SETS CLOCK AND TURNS NTP OFF",CompactRenderer::yellow);
          }else{
            ft8_screen_.text(4,103,"R SYNC NOW   F SET MANUAL UTC",CompactRenderer::white);
            ft8_screen_.text(4,118,clock.message,CompactRenderer::yellow,1,51);
            ft8_screen_.text(4,133,"MANUAL ENTRY DISABLES NETWORK SYNC",CompactRenderer::muted);
          }
          ft8_screen_.text(4,160,"ESC BACK   SPACE DASHBOARDS",CompactRenderer::cyan);
          clock_tick_=utc;
        }
        return ft8_screen_.pixels;
      }
      if(!ft8_drawn_){
        ft8_screen_.fill(CompactRenderer::bg);



        ft8_screen_.text(4,160,ft8_map_open_?"NATURAL EARTH  M RX T UTC SPACE DASH":"Z/C BAND M MAP T UTC SPACE DASH",CompactRenderer::cyan);
      }
      if(!ft8_drawn_||ft8_header_dirty_){
        const auto* preset=orcsdr::ft8::band(ft8_band_.index);
        char label[54];
        std::snprintf(label,sizeof(label),"%.3f MHZ USB  %s  %s",preset->dial_hz/1e6,preset->label,
          ft8_applied_==preset->dial_hz?"RX":"TUNING");
        ft8_screen_.rect(0,0,320,36,CompactRenderer::bg);
        ft8_screen_.badge(1,1,28);
        ft8_screen_.text(33,3,ft8_map_open_?"OrcSDR FT8 MAP":"OrcSDR FT8 RX",CompactRenderer::green,ft8_map_open_?1:2);
        ft8_screen_.text(33,23,label,CompactRenderer::white);
        ft8_header_dirty_=false;
      }
      if(ft8_map_open_){
        if(ft8_map_painted_!=ft8_map_.revision()){
          ft8_screen_.rect(0,30,320,116,CompactRenderer::bg);
          ft8_map_.draw(ft8_screen_);ft8_map_painted_=ft8_map_.revision();
        }
      }else{
        if(ft8_visual_painted_!=ft8_visual_.revision()||ft8_visual_palette_!=palette){
          ft8_screen_.rect(0,36,320,9,CompactRenderer::bg);
          ft8_screen_.text(2,37,"0",CompactRenderer::muted);
          ft8_screen_.text(92,37,"1 KHZ",CompactRenderer::muted);
          ft8_screen_.text(198,37,"2 KHZ",CompactRenderer::muted);
          ft8_screen_.text(284,37,"3 KHZ",CompactRenderer::muted);
          ft8_visual_.draw(ft8_screen_,palette);ft8_visual_painted_=ft8_visual_.revision();ft8_visual_palette_=palette;
        }
        if(!ft8_drawn_||ft8_render_epoch_!=ft8_last_utc_){
          ft8_screen_.rect(0,113,320,33,CompactRenderer::bg);
          if(ft8_lines_.empty())ft8_screen_.text(4,120,!ft8_last_utc_?"WAITING FOR COMPLETE UTC SLOT":"NO MESSAGES IN LAST SLOT",CompactRenderer::cyan);
          else for(size_t n=ft8_log_offset_;n<std::min(ft8_lines_.size(),ft8_log_offset_+2);++n)
            ft8_screen_.text(4,117+int(n-ft8_log_offset_)*12,ft8_lines_[n],CompactRenderer::white,1,52);
          ft8_render_epoch_=ft8_last_utc_;
        }
      }
      ft8_drawn_=true;
      if(clock_tick_!=utc){
        char b[64];std::strftime(b,sizeof(b),"UTC %H:%M:%S",&tm);
        const std::string label=std::string(b)+(clock.synced?" NTP SYNC":" UNSYNC")+" SLOT "+std::to_string(15-utc%15)+"S";
        ft8_screen_.rect(0,146,320,10,CompactRenderer::bg);
        ft8_screen_.text(4,147,label,clock.synced?CompactRenderer::green:CompactRenderer::yellow);
        clock_tick_=utc;
        ft8_screen_.rect(0,160,320,10,CompactRenderer::bg);
        ft8_screen_.text(4,160,ft8_map_open_?"NATURAL EARTH  M RX T UTC SPACE DASH":"Z/C BAND M MAP T UTC SPACE DASH",CompactRenderer::cyan);
      }
      return ft8_screen_.pixels;
    }
    if(mode_==6){
      auto rows=wifi_rows_;
      const bool ready=wifi_ready_;
      lock.unlock();
      if(!wifi_drawn_){
        wifi_render_.fill(CompactRenderer::bg);
        wifi_render_.text(4,4,"OrcSDR 2.4 GHZ WIFI",CompactRenderer::green,2);

        wifi_render_.text(4,42,"CH  SIGNAL   NETWORK",CompactRenderer::cyan);
        wifi_render_.text(4,160,"SPACE DASHBOARDS   ESC BACK",CompactRenderer::cyan);
        wifi_drawn_=true;
      }
      if(rows!=wifi_displayed_ || ready!=wifi_display_ready_ || !wifi_list_drawn_){
        wifi_render_.rect(0,55,320,100,CompactRenderer::bg);
        if(rows.empty())wifi_render_.text(4,58,ready?"NO 2.4 GHZ NETWORKS":"WAITING FOR WIFI SURVEY",CompactRenderer::yellow);
        for(size_t j=0;j<rows.size();++j)
          wifi_render_.text(4,58+int(j)*12,rows[j],CompactRenderer::white,1,52);
        wifi_displayed_=std::move(rows);
        wifi_display_ready_=ready;
        wifi_list_drawn_=true;
      }
      return wifi_render_.pixels;
    }
    if(is_nfm(mode_)){
      const auto center=lab_.center,target=lab_target_;
      const auto volume=weather_volume_;
      const auto marine_channel=marine_channels_[marine_index_].number;
      const bool muted=weather_muted_,audio=weather_audio_;
      lock.unlock();
      if(!weather_drawn_){
        weather_render_.fill(CompactRenderer::bg);
        weather_render_.text(4,4,mode_==8?"OrcSDR MARINE":"OrcSDR WEATHER",CompactRenderer::green,2);

        weather_render_.text(4,39,mode_==8?"VHF MARINE  NFM  RX ONLY":"NOAA WEATHER RADIO  NFM",CompactRenderer::cyan);
        weather_render_.text(4,144,"Z/C CHANNEL   S/D VOLUME   M MUTE",CompactRenderer::cyan);
        weather_render_.text(4,160,"SPACE DASHBOARDS   ESC BACK",CompactRenderer::cyan);
        weather_drawn_=true;
      }
      weather_render_.rect(0,55,320,77,CompactRenderer::bg);
      char line[48];
      if(mode_==8)std::snprintf(line,sizeof(line),"CH %02u %.3f MHZ",marine_channel,target/1e6);
      else std::snprintf(line,sizeof(line),"%.3f MHZ",target/1e6);
      weather_render_.text(4,60,line,center==target?CompactRenderer::white:CompactRenderer::yellow,2);
      weather_render_.text(4,85,center==target?"RECEIVING IQ":"TUNING",CompactRenderer::cyan);
      weather_render_.text(4,105,muted?"MUTED":"VOL "+std::to_string(volume)+"%",CompactRenderer::white);
      if(!audio)weather_render_.text(100,105,"AUDIO UNAVAILABLE",CompactRenderer::yellow);
      return weather_render_.pixels;
    }
    if(mode_==10){
      if(!sstv_drawn_){
        sstv_render_.fill(CompactRenderer::bg);
        sstv_render_.text(4,3,"OrcSDR SSTV",CompactRenderer::green,2);

        sstv_render_.text(4,159,"Z/C TUNE   UP/DOWN GALLERY   SPACE DASH",CompactRenderer::cyan);
        sstv_drawn_=true;
      }
      if(sstv_rendered_!=sstv_revision_){
        sstv_render_.rect(0,19,320,137,CompactRenderer::bg);
        char line[52];
        std::snprintf(line,sizeof(line),"%.4f MHZ  %s  GALLERY %zu",lab_target_/1e6,
          lab_target_!=lab_.center?"TUNING":sstv_index_?"GALLERY":sstv_ready_?"LIVE IMAGE":"WAITING FOR IMAGE",sstv_archived_);
        sstv_render_.text(4,21,line,CompactRenderer::white);
        if(sstv_ready_)
          for(int y=0;y<120;++y)
            for(int x=0;x<160;++x)
              sstv_render_.point(80+x,34+y,sstv_pixels_[y*160+x]);
        sstv_rendered_=sstv_revision_;
      }
      return sstv_render_.pixels;
    }
    if(mode_==9){
      const auto target=lab_target_,center=lab_.center;
      const auto revision=pager_revision_;
      const auto stats=pager_stats_;
      const auto messages=pager_text_;
      const auto offset=pager_offset_;
      lock.unlock();
      if(!pager_drawn_){
        pager_render_.fill(CompactRenderer::bg);
        pager_render_.text(4,4,"OrcSDR POCSAG",CompactRenderer::green,2);

        pager_render_.text(4,145,"Z/C TUNE 12.5K  T BAND  UP/DOWN LOG",CompactRenderer::cyan);
        pager_render_.text(4,160,"SPACE DASHBOARDS   ESC BACK",CompactRenderer::cyan);
        pager_drawn_=true;
      }
      if(revision!=pager_rendered_){
        pager_render_.rect(0,39,320,102,CompactRenderer::bg);
        char line[52];
        std::snprintf(line,sizeof(line),"%.4f MHZ",target/1e6);
        pager_render_.text(4,40,line,target==center?CompactRenderer::white:CompactRenderer::yellow,2);
        std::snprintf(line,sizeof(line),"%s   %u BAUD   MSG %u",target!=center?"TUNING":
          stats.lock==orcsdr::pocsag::LockState::locked?"LOCKED":"SEARCHING",
          unsigned(stats.detected_baud),unsigned(stats.messages_decoded));
        pager_render_.text(4,61,line,CompactRenderer::cyan);
        if(messages.empty())pager_render_.text(4,78,"NO DECODED MESSAGES",CompactRenderer::muted);
        for(size_t row=0;row<5&&row+offset<messages.size();++row)
          pager_render_.text(4,78+int(row)*12,messages[row+offset],CompactRenderer::white,1,52);
        pager_rendered_=revision;
      }
      return pager_render_.pixels;
    }
    if(mode_==5){
      const auto lab=lab_;
      const auto target=lab_target_;
      lock.unlock();
      if(!lab_drawn_ || lab.sequence!=last_lab_sequence_ || lab.center!=last_lab_center_ || target!=last_lab_target_){
        if(!lab_drawn_){
          lab_render_.fill(CompactRenderer::bg);
          lab_render_.text(4,4,"OrcSDR RF LAB",CompactRenderer::green,2);

          lab_render_.text(4,149,"RELATIVE TO ADC FULL SCALE",CompactRenderer::muted);
          lab_render_.text(4,161,"Z/C TUNE 100K  SPACE DASH  ESC BACK",CompactRenderer::cyan);
          lab_drawn_=true;
        }
        lab_render_.rect(0,40,320,99,CompactRenderer::bg);
        if(target!=lab.center){
          char line[48];
          std::snprintf(line,sizeof(line),"TUNING %.3f MHZ",target/1e6);
          lab_render_.text(4,43,line,CompactRenderer::yellow,2);
        }else if(lab.sequence){
          char line[64];
          std::snprintf(line,sizeof(line),"CENTER %.3f MHZ",lab.center/1e6);
          lab_render_.text(4,43,line,CompactRenderer::white,2);
          std::snprintf(line,sizeof(line),"RATE %.3f MS/S   IQ %llu",lab.rate/1e6,
                        static_cast<unsigned long long>(lab.samples));
          lab_render_.text(4,66,line,CompactRenderer::cyan);
          std::snprintf(line,sizeof(line),"MEAN POWER %.1f DBFS",lab.metrics.mean_power_dbfs);
          lab_render_.text(4,88,line,CompactRenderer::green,2);
          std::snprintf(line,sizeof(line),"PEAK OFFSET %+.1f KHZ",lab.metrics.peak_bin*(lab.rate/64000.0));
          lab_render_.text(4,115,line,CompactRenderer::white);
        }else lab_render_.text(4,43,"WAITING FOR IQ",CompactRenderer::yellow,2);
        last_lab_center_=lab.center;
        last_lab_target_=target;
        last_lab_sequence_=lab.sequence;
      }
      return lab_render_.pixels;
    }
    auto state=am_.snapshot();
    const auto sw_mode=orcsdr::shortwave_ssb::resolve(sw_choice_,state.selected_hz);
    if(mode_==14)state.cb_channel=uint8_t(cb_channel_+1);
    if(mode_==13||mode_==14){state.sideband=uint8_t(sw_mode);if(state.sideband)state.squelch_db=0;}
    lock.unlock();
    if(palette!=last_palette_){
      for(unsigned i=0;i<colors_.size();++i)colors_[i]=orcsdr::waterfall_style::color565(uint8_t(palette),i/255.f);
      last_palette_=palette;
      am_render_.invalidate_palette();
    }
    am_render_.render(state,colors_.data());
    if(mode_==13||mode_==14){
      sw_frame_.pixels=am_render_.framebuffer();
      sw_frame_.rect(205,157,115,13,CompactRenderer::panel);
      sw_frame_.text(207,159,"T MODE",CompactRenderer::cyan);
      return sw_frame_.pixels;
    }
    return am_render_.framebuffer();
  }
 private:
  void run_wifi(){
    while(running_){
      std::vector<std::pair<int,std::string>> networks;
      // Fixed command, cached scan only. The timeout bounds shutdown.
      FILE* pipe=::popen("LC_ALL=C timeout -k 1 3 nmcli -t --escape no -f FREQ,CHAN,SIGNAL,SSID device wifi list --rescan no 2>/dev/null","r");
      if(pipe){
        char line[512];
        while(std::fgets(line,sizeof(line),pipe)){
          std::istringstream in(line);
          int freq=0,channel=0,signal=0;
          char a=0,b=0,c=0;
          char unit[3]{};
          if(!(in>>freq>>std::ws)||!in.read(unit,3)||std::string_view(unit,3)!="MHz")continue;
          if(!(in>>a>>channel>>b>>signal>>c)||a!=':'||b!=':'||c!=':'||
             freq<2400||freq>2500||channel<1||channel>14||signal<0||signal>100)continue;
          std::string name;
          std::getline(in,name);
          if(name.empty())name="HIDDEN";
          for(char& ch:name)if(ch<32||ch>126)ch='?';
          char prefix[24];
          std::snprintf(prefix,sizeof(prefix),"%2d  %3d%%    ",channel,signal);
          networks.emplace_back(signal,prefix+name.substr(0,38));
          std::stable_sort(networks.begin(),networks.end(),[](const auto& l,const auto& r){return l.first>r.first;});
          if(networks.size()>8)networks.pop_back();
        }
      }
      const int status=pipe?::pclose(pipe):-1;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if(status!=0){error_="WIFI SURVEY UNAVAILABLE";return;}
        wifi_rows_.clear();
        for(const auto& entry:networks)wifi_rows_.push_back(entry.second);
        wifi_ready_=true;
      }
      for(int i=0;i<20&&running_;++i)std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
  static uint64_t now(){
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  }
  struct Audio {
    AudioSink sink;
    std::vector<int16_t> pcm;
  };
  void run(unsigned mode,unsigned device,const std::string& output,bool silent,uint32_t rf_center) noexcept{
    try{
      linux_sdr::RtlReceiver radio;
      radio.open(device);
      linux_sdr::IqMetadata meta{};
      meta.receiver_generation=++generation_;
      acars_dashboard::RadioRequest ac_request;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if(mode==1){auto r=ads_.radio_request();meta.frequency_hz=r.center_hz;meta.sample_rate_sps=r.rate_sps;}
        if(mode==2){
          if(!acars_dashboard::make_radio_request(acars_dashboard::kDefaultChannelHz,ac_request))
            throw std::runtime_error("Invalid ACARS channel");
          meta.frequency_hz=ac_request.center_hz;meta.sample_rate_sps=ac_request.rate_sps;
        }
        if(mode==3||mode==11||mode==13||mode==14){auto r=am_.requested_radio();meta.frequency_hz=r.rf_center_hz;meta.sample_rate_sps=r.sample_rate_sps;}
      }
      if(mode==5||mode==12){meta.frequency_hz=rf_center;meta.sample_rate_sps=kDefaultSpanHz;}
      if(is_nfm(mode)){meta.frequency_hz=mode==8?marine_channels_[marine_default_].hz:162400000;meta.sample_rate_sps=300000;}
      if(mode==9){meta.frequency_hz=929662500;meta.sample_rate_sps=orcsdr::pocsag::kInputSampleRateHz;}
      if(mode==10){meta.frequency_hz=145800000;meta.sample_rate_sps=300000;}
      if(mode==16){meta.frequency_hz=851000000;meta.sample_rate_sps=960000;}
      uint32_t ft8_dial=7074000;
      if(mode==15){meta.frequency_hz=ft8_dial;meta.sample_rate_sps=960000;}
      if(mode==11||mode==13||mode==15)radio.configure_direct_q(meta.frequency_hz,meta.sample_rate_sps);
      else radio.configure(meta.frequency_hz,meta.sample_rate_sps);
      orcsdr::pocsag::Decoder pager;
      orcnode_fm_dsp_t narrow{};
      std::array<int16_t,1024> weather_pcm{};
      if(is_nfm(mode)||mode==10)orcnode_nfm_dsp_init(&narrow);
      uint32_t sstv_last_image=~uint32_t{0};
      size_t sstv_last_selection=~size_t{0};
      uint32_t sstv_last_saved=0;
      const char* sstv_home=std::getenv("HOME");
      const auto sstv_dir=sstv_home?std::filesystem::path(sstv_home)/"Pictures/OrcSDR/SSTV":std::filesystem::path{};
      const auto disk_gallery=mode==10&&sstv_home?saved_sstv_images(sstv_dir):std::vector<std::filesystem::path>{};
      std::vector<uint16_t> disk_pixels;
      if(mode==10){
        orcsdr::sstv::clear_gallery();
        orcsdr::sstv::set_mode(orcsdr::sstv::Mode::auto_vis);
        orcsdr::sstv::start(meta.frequency_hz);
      }
      SidebandPcm ssb;
      linux_sdr::IqBlock ssb_block;
      if(mode==13||mode==14)ssb_block.cu8.reserve(8192);
      std::unique_ptr<Ft8Receiver> ft8;
      linux_sdr::IqBlock ft8_block;
      if(mode==15){
        ft8=std::make_unique<Ft8Receiver>();
        if(!ft8->begin())throw std::runtime_error("FT8 decoder allocation failed");
        ft8->observe_audio(+[](const int16_t* pcm,size_t count,bool reset,void* context){
          auto& self=*static_cast<DashboardSession*>(context);
          std::lock_guard<std::mutex> lock(self.mutex_);
          self.ft8_visual_.consume(pcm,count,reset);
        },this);
        ft8_block.cu8.reserve(8192);
        std::lock_guard<std::mutex> lock(mutex_);
        ft8_applied_=ft8_dial;
        ft8_header_dirty_=true;
      }
      std::unique_ptr<orcsdr::p25voice::Decoder> p25_voice;
      if(mode==16){
        orcsdr::p25core::reset(uint32_t(now()/1000000));
        p25_voice=std::make_unique<orcsdr::p25voice::Decoder>();
      }
      Audio audio;
      audio.pcm.reserve(1024);
      const bool needs_audio=mode==3||mode==11||mode==13||mode==14||is_nfm(mode)||mode==16;
      const bool audio_open=needs_audio&&audio.sink.open(output,silent);
      uint64_t audio_retry_ns=now()+1000000000ULL;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if(mode==16){p25_.applied=meta.frequency_hz;p25_.audio_ready=audio_open&&audio.sink.audible();++p25_.revision;}
        if(is_nfm(mode))weather_audio_=audio_open&&audio.sink.audible();
        if(mode==1){ads_.activate(meta.receiver_generation,now());ads_.receiver_ready(now());}
        if(mode==2&&!ac_.start(ac_request,meta.receiver_generation))throw std::runtime_error("ACARS start failed");
        if(mode==3||mode==11||mode==13||mode==14){
          am_.audio_available(audio_open&&audio.sink.audible());
          am_.radio_applied(radio.frequency_hz(),radio.sample_rate_sps(),meta.receiver_generation);
        }
      }
      std::clog<<"Dashboard "<<mode<<" RF "<<radio.frequency_hz()<<" rate "<<radio.sample_rate_sps()<<std::endl;
      std::array<uint8_t,8192> buffer{};
      uint64_t next_measurement=0;
      uint64_t measurement_sequence=0;
      std::vector<uint8_t> measurement;
      if(mode==5)measurement.reserve(buffer.size());
      linux_sdr::IqBlock dead_air_block;
      if(mode==12)dead_air_block.cu8.reserve(buffer.size());
      while(running_){
        if(needs_audio&&!audio.sink.opened()&&now()>=audio_retry_ns){
          audio_retry_ns=now()+1000000000ULL;
          const bool restored=audio.sink.open(output,silent);
          std::lock_guard<std::mutex> lock(mutex_);
          const bool audible=restored&&audio.sink.audible();
          if(mode==16){p25_.audio_ready=audible;++p25_.revision;}
          else if(is_nfm(mode))weather_audio_=audible;
          else am_.audio_available(audible,restored?"":audio.sink.last_error().c_str());
          if(restored)std::clog<<"Dashboard "<<mode<<" audio restored"<<std::endl;
        }
        if(mode==16){
          uint32_t target;orcsdr::p25core::Modulation modulation;
          {std::lock_guard<std::mutex> lock(mutex_);target=p25_.requested;modulation=p25_.modulation;}
          if(target!=meta.frequency_hz||modulation!=orcsdr::p25core::modulation()){
            if(target!=meta.frequency_hz)radio.retune(target);
            meta.frequency_hz=radio.frequency_hz();
            meta.first_sample_index=0;meta.receiver_generation=++generation_;
            orcsdr::p25core::reset(uint32_t(now()/1000000));
            orcsdr::p25core::set_modulation(modulation);
            p25_voice->reset();audio.sink.discard();
            std::lock_guard<std::mutex> lock(mutex_);
            p25_.applied=meta.frequency_hz;p25_.state={};++p25_.revision;
            std::clog<<"P25 RF "<<meta.frequency_hz<<" mode "<<orcsdr::p25core::modulation_name(modulation)<<std::endl;
          }
        }
        if(mode==15){
          uint32_t target;
          {std::lock_guard<std::mutex> lock(mutex_);target=ft8_band_.dial();}
          if(target!=ft8_dial){
            // Reopen to reset both the tuner/direct-Q path and buffered IQ.
            radio.close();radio.open(device);
            if(target<24000000)radio.configure_direct_q(target,meta.sample_rate_sps);
            else radio.configure(target,meta.sample_rate_sps);
            ft8_dial=radio.frequency_hz();
            meta.frequency_hz=ft8_dial;
            meta.first_sample_index=0;
            meta.receiver_generation=++generation_;
            std::lock_guard<std::mutex> lock(mutex_);
            ft8_applied_=ft8_dial;
            ft8_header_dirty_=true;
            ft8_lines_.clear();ft8_last_utc_=0;ft8_visual_.clear();
            std::clog<<"FT8 band RF "<<ft8_dial<<std::endl;
          }
        }
        if(mode==5||is_nfm(mode)||mode==9||mode==10){
          uint32_t target;
          {std::lock_guard<std::mutex> lock(mutex_);target=lab_target_;}
          if(target!=meta.frequency_hz){
            try{
              radio.retune(target);
              meta.frequency_hz=radio.frequency_hz();
              meta.first_sample_index=0;
              next_measurement=0;
              if(mode==10){
                orcnode_nfm_dsp_init(&narrow);
                orcsdr::sstv::stop();
                orcsdr::sstv::start(meta.frequency_hz);
                sstv_last_image=~uint32_t{0};
              }
              if(mode==9){
                pager.reset();
                std::lock_guard<std::mutex> lock(mutex_);
                pager_text_.clear();
                pager_offset_=0;
                pager_stats_={};
                ++pager_revision_;
              }
              if(is_nfm(mode)){orcnode_nfm_dsp_init(&narrow);audio.sink.discard();}
            }catch(const std::exception& e){
              std::lock_guard<std::mutex> lock(mutex_);
              if(lab_target_==target)lab_target_=meta.frequency_hz;
              std::cerr<<"RF Lab tune: "<<e.what()<<std::endl;
            }
          }
        }
        if(mode==3||mode==11||mode==13||mode==14){
          airband::Request r;
          {
            std::lock_guard<std::mutex> lock(mutex_);
            r=am_.requested_radio();
            r.retune=am_.snapshot().tune_pending;
          }
          if(r.retune){
            radio.retune(r.rf_center_hz);
            meta.frequency_hz=radio.frequency_hz();
            meta.receiver_generation=++generation_;
            meta.first_sample_index=0;
            audio.sink.discard();
            std::lock_guard<std::mutex> lock(mutex_);
            if(am_.requested_radio().rf_center_hz!=meta.frequency_hz)continue;
            am_.radio_applied(meta.frequency_hz,radio.sample_rate_sps(),meta.receiver_generation);
          }
        }
        const auto bytes=radio.read_cu8(buffer.data(),buffer.size());
        meta.monotonic_ns=now();
        if(mode==5&&meta.monotonic_ns>=next_measurement){
          measurement.assign(buffer.begin(),buffer.begin()+bytes);
          const auto metrics=orc::analyze_u8_iq(measurement);
          {
            std::lock_guard<std::mutex> lock(mutex_);
            lab_={meta.frequency_hz,meta.sample_rate_sps,meta.first_sample_index+bytes/2,++measurement_sequence,metrics};
          }
          next_measurement=meta.monotonic_ns+250000000;
        }
        if(is_nfm(mode)){
          const auto count=orcnode_fm_dsp_process(&narrow,buffer.data(),bytes,weather_pcm.data(),weather_pcm.size());
          int volume;
          bool muted;
          {
            std::lock_guard<std::mutex> lock(mutex_);
            lab_.center=meta.frequency_hz;
            volume=weather_volume_;
            muted=weather_muted_;
          }
          if(audio.sink.opened()&&count&&!audio.sink.write_mono(weather_pcm.data(),count,volume,muted)){
            audio.sink.close();
          audio_retry_ns=now()+1000000000ULL;
            std::lock_guard<std::mutex> lock(mutex_);
            weather_audio_=false;
          }
        }
        if(mode==10){
          const auto count=orcnode_fm_dsp_process(&narrow,buffer.data(),bytes,weather_pcm.data(),weather_pcm.size());
          orcsdr::sstv::offer_audio_48k(weather_pcm.data(),count);
          orcsdr::sstv::service();
          if(meta.monotonic_ns>=next_measurement){
            const auto revision=orcsdr::sstv::image_revision();
            const auto archived=orcsdr::sstv::archive_count();
            if(archived){
              const auto frame=orcsdr::sstv::archived_image(0);
              if(frame.serial!=sstv_last_saved){
                sstv_last_saved=frame.serial;
                if(frame.pixels&&!frame.partial&&frame.decoded_rows==frame.height){
                  const char* home=std::getenv("HOME");
                  if(!home||!save_completed_sstv(frame,std::filesystem::path(home)/"Pictures/OrcSDR/SSTV"))
                    std::cerr<<"SSTV: could not save completed image"<<std::endl;
                }
              }
            }
            std::lock_guard<std::mutex> lock(mutex_);
            sstv_archived_=archived+disk_gallery.size();
            sstv_index_=std::min(sstv_index_,sstv_archived_);
            if(revision!=sstv_last_image||sstv_index_!=sstv_last_selection||lab_.center!=meta.frequency_hz){
              orcsdr::sstv::Image frame{};
              if(sstv_index_>archived){
                unsigned width=0,height=0;
                if(read_saved_sstv(disk_gallery[sstv_index_-archived-1],disk_pixels,width,height)){
                  frame.pixels=disk_pixels.data();
                  frame.width=uint16_t(width);
                  frame.height=uint16_t(height);
                }
              }else frame=sstv_index_?orcsdr::sstv::archived_image(sstv_index_-1):orcsdr::sstv::live_image();
              if(!frame.pixels&&!sstv_index_&&archived)frame=orcsdr::sstv::archived_image(0);
              sstv_ready_=frame.pixels&&frame.width&&frame.height;
              if(sstv_ready_){
                sstv_pixels_.fill(0);
                const int w=std::min(160,int(frame.width)*120/frame.height);
                const int h=std::min(120,int(frame.height)*160/frame.width);
                for(int y=0;y<h;++y)
                  for(int x=0;x<w;++x)
                    sstv_pixels_[((120-h)/2+y)*160+(160-w)/2+x]=
                      frame.pixels[(y*frame.height/h)*frame.width+x*frame.width/w];
              }
              lab_.center=meta.frequency_hz;
              sstv_last_image=revision;
              sstv_last_selection=sstv_index_;
              ++sstv_revision_;
            }
            next_measurement=meta.monotonic_ns+125000000;
          }
        }
        if(mode==9){
          pager.process_cu8(buffer.data(),bytes,[](const orcsdr::pocsag::Message& message,void* ctx){
            auto& self=*static_cast<DashboardSession*>(ctx);
            char prefix[24];
            std::snprintf(prefix,sizeof(prefix),"%07u  ",unsigned(message.capcode));
            std::string line=prefix;
            for(size_t i=0;i<std::min<size_t>(message.text_length,42);++i){
              const unsigned char ch=message.text[i];
              line+=ch>=32&&ch<=126?char(ch):'?';
            }
            if(!message.text_length)line+="TONE";
            std::lock_guard<std::mutex> lock(self.mutex_);
            self.pager_text_.insert(self.pager_text_.begin(),std::move(line));
            if(self.pager_text_.size()>16)self.pager_text_.pop_back();
            self.pager_offset_=0;
            ++self.pager_revision_;
          },this);
          if(meta.monotonic_ns>=next_measurement){
            std::lock_guard<std::mutex> lock(mutex_);
            lab_.center=meta.frequency_hz;
            pager_stats_=pager.stats();
            ++pager_revision_;
            next_measurement=meta.monotonic_ns+250000000;
          }
        }
        if(mode==12&&meta.monotonic_ns>=next_measurement){
          dead_air_block.meta=meta;
          dead_air_block.cu8.assign(buffer.begin(),buffer.begin()+bytes);
          std::lock_guard<std::mutex> lock(mutex_);
          dead_air_.consume(dead_air_block);
          next_measurement=meta.monotonic_ns+250000000;
        }
        if(ft8){
          const auto utc=std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
          const uint64_t block_ns=uint64_t(bytes/2)*1000000000/meta.sample_rate_sps;
          ft8_block.meta=meta;
          ft8_block.cu8.assign(buffer.begin(),buffer.begin()+bytes);
          bool completed=false;
          ft8->consume(ft8_block,ft8_dial,ft8_dial<24000000,utc-block_ns,
            [&](uint64_t epoch,const orcsdr::ft8::Decode* rows,size_t count){
              std::lock_guard<std::mutex> lock(mutex_);
              ft8_last_utc_=epoch;
              ft8_lines_.clear();ft8_log_offset_=0;
              for(size_t n=0;n<count;++n)ft8_map_.add(rows[n]);
              for(size_t n=0;n<std::min<size_t>(count,6);++n){
                std::string line(rows[n].message,strnlen(rows[n].message,sizeof(rows[n].message)));
                for(auto& ch:line)if(ch<32||ch>126)ch='?';
                ft8_lines_.push_back(std::move(line));
              }
              std::clog<<"FT8 slot "<<epoch<<" messages "<<count<<std::endl;
              completed=true;
            });
          // Synchronous decoding can overrun the USB buffer. Flush and start
          // a fresh slot rather than appending delayed samples as live data.
          if(completed){
            radio.retune(ft8_dial);
            meta.receiver_generation=++generation_;
          }
        }
        audio.pcm.clear();
        int output_volume=100;
        bool output_muted=false;
        {
          std::lock_guard<std::mutex> lock(mutex_);
          if(mode==16){
            struct VoiceContext {
              orcsdr::p25voice::Decoder* decoder;
              std::vector<int16_t>* pcm;
              P25Screen* screen;
            } context{p25_voice.get(),&audio.pcm,&p25_};
            orcsdr::p25core::process_cu8(buffer.data(),bytes,uint32_t(now()/1000000),
              +[](const orcsdr::p25core::VoiceFrame& voice,void* ptr){
                auto& c=*static_cast<VoiceContext*>(ptr);
                if(!voice.encryption.valid||voice.encryption.encrypted||
                   voice.encryption.algorithm_id!=orcsdr::p25core::kClearAlgorithmId){
                  ++c.screen->blocked_voice;return false;
                }
                if(c.pcm->size()+orcsdr::p25voice::kPcmSamplesPerFrame>19200)return false;
                int16_t samples[orcsdr::p25voice::kPcmSamplesPerFrame];
                if(!c.decoder->process(voice,samples))return false;
                c.pcm->insert(c.pcm->end(),std::begin(samples),std::end(samples));
                c.screen->voice_pcm+=orcsdr::p25voice::kPcmSamplesPerFrame;
                return true;
              },&context);
            if(meta.monotonic_ns>=next_measurement){
              p25_.state=orcsdr::p25core::snapshot();++p25_.revision;
              next_measurement=meta.monotonic_ns+250000000;
            }
            output_volume=p25_.volume;output_muted=p25_.muted;
          }
          if(mode==1&&ads_.consume_cu8(buffer.data(),bytes,meta)!=adsb::Result::accepted)
            throw std::runtime_error("ADS-B rejected receiver data");
          if(mode==2&&!ac_.consume(buffer.data(),bytes,meta))
            throw std::runtime_error("ACARS rejected receiver data");
          if((mode==3||mode==11||mode==13||mode==14)&&am_.snapshot().tune_pending)continue;
          const auto settings=am_.settings();
          const auto sw_mode=orcsdr::shortwave_ssb::resolve(sw_choice_,am_.snapshot().selected_hz);
          const bool sideband=(mode==13||mode==14)&&sw_mode!=orcsdr::shortwave_ssb::Mode::am;
          if(sideband){
            ssb_block.meta=meta;
            ssb_block.cu8.assign(buffer.begin(),buffer.begin()+bytes);
            if(!ssb.consume(ssb_block,am_.snapshot().selected_hz,sw_mode,mode==13,0,audio.pcm))
              throw std::runtime_error("Sideband rejected receiver data");
            output_volume=settings.volume;
            output_muted=settings.user_muted;
          }else if(mode==13||mode==14)ssb.reset();
          if((mode==3||mode==11||mode==13||mode==14)&&!am_.consume(buffer.data(),bytes,meta,sideband?nullptr:+[](void* context,const int16_t* samples,size_t count){
            auto& pcm=*static_cast<std::vector<int16_t>*>(context);
            pcm.insert(pcm.end(),samples,samples+count);
          },&audio.pcm))throw std::runtime_error("Airband rejected receiver data");
        }
        if(audio.sink.opened()&&!audio.pcm.empty()&&!audio.sink.write_mono(audio.pcm.data(),audio.pcm.size(),output_volume,output_muted)){
          std::lock_guard<std::mutex> lock(mutex_);
          if(mode==16){p25_.audio_ready=false;++p25_.revision;}
          else am_.audio_available(false,audio.sink.last_error().c_str());
          audio.sink.close();
        }
        meta.first_sample_index+=bytes/2;
      }
      std::clog<<"Dashboard "<<mode<<" captured "<<meta.first_sample_index<<" samples"<<std::endl;
    }catch(const std::exception& e){
      std::lock_guard<std::mutex> lock(mutex_);
      error_=e.what();
      std::cerr<<"Dashboard "<<mode<<": "<<error_<<std::endl;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if(mode==1)ads_.stop();
    if(mode==2)ac_.stop();
    if(mode==3||mode==11||mode==13||mode==14)am_.stop();
    if(mode==10)orcsdr::sstv::stop();
  }
  struct LabSnapshot {
    uint32_t center=0,rate=0;
    uint64_t samples=0,sequence=0;
    orc::IqMetrics metrics{};
  } lab_;
  CompactRenderer lab_render_,wifi_render_,weather_render_;
  size_t cb_channel_=orcsdr::cb::kHighwayChannel;
  CompactRenderer sw_frame_;
  orcsdr::shortwave_ssb::Choice sw_choice_=orcsdr::shortwave_ssb::Choice::automatic;
  P25Screen p25_;
  Ft8Visuals ft8_visual_;
  Ft8WorldMap ft8_map_;
  bool ft8_map_open_=false;size_t ft8_log_offset_=0;
  uint64_t ft8_visual_painted_=~uint64_t{0},ft8_map_painted_=~uint64_t{0};
  unsigned ft8_visual_palette_=~0u;
  ClockControl clock_;
  bool clock_open_=false,clock_entry_=false;
  std::string clock_digits_;
  std::time_t clock_tick_=0;
  Ft8BandSelection ft8_band_;
  uint32_t ft8_applied_=0;
  bool ft8_header_dirty_=true;
  CompactRenderer ft8_screen_;
  std::vector<std::string> ft8_lines_;
  uint64_t ft8_last_utc_=0,ft8_render_epoch_=0;
  bool ft8_drawn_=false;
  DeadAirScreen dead_air_;
  CompactRenderer sstv_render_;
  std::array<uint16_t,160*120> sstv_pixels_{};
  size_t sstv_index_=0,sstv_archived_=0;
  uint64_t sstv_revision_=0,sstv_rendered_=0;
  bool sstv_ready_=false,sstv_drawn_=false;
  CompactRenderer pager_render_;
  orcsdr::pocsag::Stats pager_stats_{};
  std::vector<std::string> pager_text_;
  size_t pager_offset_=0;
  uint64_t pager_revision_=0,pager_rendered_=0;
  bool pager_drawn_=false;
  unsigned marine_index_=marine_default_;
  int weather_volume_=20;
  bool weather_muted_=false,weather_audio_=false,weather_drawn_=false;
  std::vector<std::string> wifi_rows_,wifi_displayed_;
  bool wifi_ready_=false,wifi_drawn_=false,wifi_display_ready_=false,wifi_list_drawn_=false;
  uint64_t last_lab_sequence_=0;
  uint32_t last_lab_center_=0,last_lab_target_=0,lab_target_=0;
  bool lab_drawn_=false;
  unsigned mode_=0,last_palette_=~0u;
  uint64_t generation_=0;
  std::atomic<bool> running_{false};
  std::mutex mutex_;
  std::thread worker_;
  std::string error_;
  adsb::Controller ads_;
  adsb::Renderer ads_render_;
  acars_dashboard::Dashboard ac_;
  acars_dashboard::Renderer ac_render_;
  airband::Controller am_;
  airband::Renderer am_render_;
  std::array<uint16_t,256> colors_{};
};
}
