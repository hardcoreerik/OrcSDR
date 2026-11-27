#pragma once
#include "adsb_renderer.hpp"
#include "acars_renderer.hpp"
#include "acars_archive_worker.hpp"
#include "acars_tuning.hpp"
#include "airband.hpp"
#include "linux_audio.hpp"
#include "iq_metrics.hpp"
#include "ft8_receiver.hpp"
#include "ft8_band.hpp"
#include "clock_control.hpp"
#include "ft8_visuals.hpp"
#include <ctime>
#include "compact/p25_screen.hpp"
#include "compact/lora_screen.hpp"
#include "compact/dmr_screen.hpp"
#include "dv_decimator.hpp"
#include "dv_channelizer.hpp"
#include "dv_voice.hpp"
#include "lora_history.hpp"
#include "settings.hpp"
#include "compact/satellite_screen.hpp"
#include "iq_spectrum.hpp"
#include "p25_voice.hpp"
#include "p25_follow.hpp"
#include <memory>
#include <cstring>
#include "sideband_pcm.hpp"
#include "cb_scanner.hpp"
#include "dead_air_screen.hpp"
#include "dead_air_storage.hpp"
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
  static constexpr bool is_nfm(unsigned mode){return mode==7||mode==8||mode==18;}
  ~DashboardSession(){stop();}
  unsigned mode() const {return mode_;}
  void stop(){
    running_=false;
    if(worker_.joinable())worker_.join();
    clock_.stop();
    if(dead_air_storage_){dead_air_storage_->stop();dead_air_storage_.reset();}
    ac_archive_.stop();
    mode_=0;
  }
  void start(unsigned mode,unsigned device,const std::string& audio,bool silent,uint32_t rf_center){
    stop();
    mode_=mode;
    error_.clear();
    ft8_band_=Ft8BandSelection{};
    ft8_applied_=0;
    ft8_header_dirty_=true;
    ft8_lines_.clear();ft8_lines_epoch_=0;
    ft8_last_utc_=0;
    ft8_drawn_=false;
    clock_open_=clock_entry_=false;clock_digits_.clear();clock_tick_=0;
    if(mode==15){clock_.start();ft8_visual_.clear();ft8_map_.clear();ft8_map_open_=false;ft8_page_=0;ft8_log_offset_=0;ft8_visual_painted_=ft8_map_painted_=~uint64_t{0};ft8_visual_palette_=~0u;}
    if(mode==16){
      p25_=P25Screen{};
      const uint32_t profile_channel=rf_center;
      p25_.configure_profile(&profile_channel,1);
    }
    if(mode==17){
      lora_=LoraScreen{};UserSettings saved{};
      if(load_settings(settings_path(),saved))lora_.set_plan(saved.lora_region,uint16_t(saved.lora_slot));
      lora_.lines=lora_history::load(lora_history::path());++lora_.revision;
    }
    if(mode==18)satellite_=SatelliteScreen{};
    if(mode==12){
      dead_air_=DeadAirScreen{};dead_air_storage_=std::make_unique<DeadAirStorage>();
      dead_air_.set_event_listener(orcsdr::dead_air::enqueue_event,&dead_air_storage_->queue());
      const char* data=std::getenv("XDG_DATA_HOME");const char* home=std::getenv("HOME");
      const auto root=data&&data[0]=='/'?std::filesystem::path(data):home?std::filesystem::path(home)/".local/share":std::filesystem::path{};
      if(!root.empty())dead_air_storage_->start(root/"orcsdr/dead-air");
      else dead_air_.storage_status(0,0,true);
    }
    lab_={};
    wifi_rows_.clear();
    wifi_ready_=false;
    marine_index_=marine_default_;
    lab_target_=mode==8?marine_channels_[marine_index_].hz:mode==7?162400000:rf_center;
    if(mode==9)lab_target_=929662500;
    if(mode==18)lab_target_=satellite_.tuning.requested;
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
    if(mode==2){
      ac_tuning_=AcarsTuning{};ac_.receiver_connecting();
      const char* data=std::getenv("XDG_DATA_HOME");const char* home=std::getenv("HOME");
      const auto root=data&&data[0]=='/'?std::filesystem::path(data):home?std::filesystem::path(home)/".local/share":std::filesystem::path{};
      if(!root.empty())ac_archive_.start(root/"orcsdr/acars");
      ac_.set_archive_listener(+[](void* context,const acars_dashboard::Record& record){
        static_cast<acars_dashboard::ArchiveWorker*>(context)->enqueue(record);
      },&ac_archive_);
    }
    running_=true;
    worker_=std::thread([=]{
      if(mode==6){run_wifi();return;}
      while(running_){
        if(mode==17)run_lora(device);else run(mode,device,audio,silent,rf_center);
        for(int n=0;n<10&&running_;++n)std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    });
  }
  bool input(Key key,char digit){
    std::lock_guard<std::mutex> lock(mutex_);
    if(mode_==16)return p25_.input(key,digit);
    if(mode_==17){
      const size_t region=lora_.region;const uint16_t slot=lora_.slot;const bool handled=lora_.input(key,digit);
      if(handled&&(region!=lora_.region||slot!=lora_.slot)){
        UserSettings saved{};load_settings(settings_path(),saved);
        saved.lora_region=unsigned(lora_.region);saved.lora_slot=lora_.slot;
        if(!save_settings(settings_path(),saved))std::cerr<<"Could not save LoRa region settings\n";
      }
      return handled;
    }
    if(mode_==18){
      const bool handled=satellite_.input(key,digit);
      lab_target_=satellite_.tuning.requested;weather_volume_=satellite_.tuning.volume;weather_muted_=satellite_.tuning.muted;
      return handled;
    }
    if(mode_==19){
      const bool handled=dmr_.input(key,digit);
      lab_target_=dmr_.tuning.requested;
      return handled;
    }
    if(mode_==1){
      using A=adsb::Action;
      if(key==Key::escape&&!ads_.snapshot(now()).detail)return false;
      ads_.navigate(key==Key::up?A::up:key==Key::down?A::down:key==Key::enter?A::open_detail:key==Key::escape?A::back:A::none);
      return true;
    }
    if(mode_==2){
      if(ac_tuning_.input(key,digit))return true;
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
      if(key==Key::gain){
        ft8_band_.toggle_mode();ft8_header_dirty_=true;clock_tick_=0;return true;
      }
      if(key==Key::mute){
        ft8_page_=ft8_page_?0:1;ft8_map_open_=ft8_page_==1;ft8_screen_.rect(0,0,320,146,CompactRenderer::bg);ft8_map_painted_=ft8_visual_painted_=~uint64_t{0};
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
      // Same contract as every dashboard: left/right page, up/down act inside
      // the page. Band moved to B, which FT8 never used for bias tee.
      if(key==Key::left||key==Key::right){
        ft8_page_=key==Key::right?1:0;
        ft8_map_open_=ft8_page_==1;
        ft8_screen_.rect(0,36,320,124,CompactRenderer::bg);
        ft8_map_painted_=ft8_visual_painted_=~uint64_t{0};
        ft8_header_dirty_=true;
        return true;
      }
      if(key==Key::bias_tee){
        ft8_band_.step(true);
        ft8_header_dirty_=true;
        return true;
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
      std::array<acars_dashboard::Record,acars_dashboard::kHistory> archive{};size_t archive_total=0;
      const size_t archive_held=ac_archive_.recent(0,archive.data(),archive.size(),&archive_total);
      ac_.set_archive(archive.data(),archive_held,archive_total,ac_archive_.dropped(),ac_archive_.failed());
      ac_.service();auto state=ac_.snapshot();const auto tuning=ac_tuning_;lock.unlock();
      state.requested_channel_hz=tuning.requested;
      ac_render_.render(state,now());ac_frame_.pixels=ac_render_.pixels();
      if(tuning.editing){
        ac_frame_.rect(10,47,300,72,CompactRenderer::panel);ac_frame_.border(10,47,300,72,CompactRenderer::cyan);
        ac_frame_.text(16,54,"ACARS CHANNEL MHZ",CompactRenderer::cyan);
        ac_frame_.text(16,72,tuning.entry+"_",CompactRenderer::white,2,23);
        ac_frame_.text(16,100,tuning.error.empty()?"ENTER APPLY  ESC CANCEL":tuning.error,CompactRenderer::yellow,1,47);
      }
      return ac_frame_.pixels;
    }
    if(mode_==16)return p25_.render();
    if(mode_==17)return lora_.render();
    if(mode_==18)return satellite_.render(palette);
    if(mode_==19){
      // Drain on the UI thread so ALSA never blocks the receiver. A call decodes
      // faster than it plays, so the ring is normally a few hundred milliseconds
      // deep; a chunk per repaint keeps it there without stalling.
      if(dmr_audio_.audible()){
        static std::int16_t dmr_pcm[4096];
        const size_t got=dmr_voice_.take_upsampled(dmr_pcm,4096);
        if(got)dmr_audio_.write_mono(dmr_pcm,got,100,false);
      }
      return dmr_.render();
    }
    if(mode_==12){
      if(dead_air_storage_)dead_air_.storage_status(dead_air_storage_->saved(),dead_air_storage_->dropped(),dead_air_storage_->failed());
      dead_air_.render();
      return dead_air_.frame().pixels;
    }
    if(mode_==15){
      const auto utc_ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
      const std::time_t utc=utc_ms/1000;
      const auto ui_tick=utc_ms/500;
      std::tm tm{};gmtime_r(&utc,&tm);
      const auto clock=clock_.snapshot();
      if(clock_open_){
        if(clock_tick_!=ui_tick){
          ft8_screen_.rect(0,36,320,134,CompactRenderer::bg);
          ft8_screen_.text(4,39,std::string("UTC CLOCK / ")+orcsdr::ft8::mode_name(ft8_band_.mode),CompactRenderer::green,2);
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
          ft8_screen_.keys(4,160,{{"ESC","BACK"},{"TAB","DASH"}});
          clock_tick_=ui_tick;
        }
        return ft8_screen_.pixels;
      }
      if(!ft8_drawn_){
        ft8_screen_.fill(CompactRenderer::bg);



        if(ft8_map_open_){ft8_screen_.rect(0,158,320,12,CompactRenderer::bg);ft8_screen_.keys(4,160,{{"Z/C","PAGE"},{"M","RX"},{"T","UTC"},{"TAB","DASH"}});}
        else {ft8_screen_.rect(0,158,320,12,CompactRenderer::bg);ft8_screen_.keys(4,160,{{"Z/C","PAGE"},{"F/X","LOG"},{"B","BAND"},{"M","MAP"},{"TAB","DASH"}});}
      }
      if(!ft8_drawn_||ft8_header_dirty_){
        const auto* preset=orcsdr::ft8::band(ft8_band_.index);
        char label[54];
        std::snprintf(label,sizeof(label),"%.4f MHZ USB  %s  %s",ft8_band_.dial()/1e6,preset->label,
          ft8_applied_==ft8_band_.dial()&&ft8_applied_mode_==ft8_band_.mode?"RX":"TUNING");
        ft8_screen_.rect(0,0,320,36,CompactRenderer::bg);
        ft8_screen_.status_bar((std::string(orcsdr::ft8::mode_name(ft8_band_.mode))+(ft8_map_open_?" MAP":" RX")).c_str(),-1,ft8_page_,2);
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
          if(ft8_lines_epoch_&&ft8_lines_epoch_!=ft8_last_utc_){
            char age[40];const std::time_t when=std::time_t(ft8_lines_epoch_);std::tm t{};
            if(gmtime_r(&when,&t))std::snprintf(age,sizeof(age),"NO NEW DECODES  LAST %02d:%02d",t.tm_hour,t.tm_min);
            else std::snprintf(age,sizeof(age),"NO NEW DECODES");
            ft8_screen_.text(4,137,age,CompactRenderer::muted,1,30);
          }
          ft8_render_epoch_=ft8_last_utc_;
        }
      }
      ft8_drawn_=true;
      if(clock_tick_!=ui_tick){
        char b[64];std::strftime(b,sizeof(b),"UTC %H:%M:%S",&tm);
        const auto period=orcsdr::ft8::slot_ms(ft8_band_.mode);
        char remaining[16];std::snprintf(remaining,sizeof(remaining),"%.1fS",(period-utc_ms%period)/1000.);
        const std::string label=std::string(b)+(clock.synced?" NTP SYNC":" UNSYNC")+" SLOT "+remaining;
        ft8_screen_.rect(0,146,320,10,CompactRenderer::bg);
        ft8_screen_.text(4,147,label,clock.synced?CompactRenderer::green:CompactRenderer::yellow);
        clock_tick_=ui_tick;
        ft8_screen_.rect(0,160,320,10,CompactRenderer::bg);
        if(ft8_map_open_){ft8_screen_.rect(0,158,320,12,CompactRenderer::bg);ft8_screen_.keys(4,160,{{"Z/C","PAGE"},{"M","RX"},{"T","UTC"},{"TAB","DASH"}});}
        else {ft8_screen_.rect(0,158,320,12,CompactRenderer::bg);ft8_screen_.keys(4,160,{{"Z/C","PAGE"},{"F/X","LOG"},{"B","BAND"},{"M","MAP"},{"TAB","DASH"}});}
      }
      return ft8_screen_.pixels;
    }
    if(mode_==6){
      auto rows=wifi_rows_;
      const bool ready=wifi_ready_;
      lock.unlock();
      if(!wifi_drawn_){
        wifi_render_.fill(CompactRenderer::bg);
        wifi_render_.badge(1,1,28);wifi_render_.text(33,4,"OrcSDR 2.4 GHZ WIFI",CompactRenderer::green,2);

        wifi_render_.text(4,42,"CH  SIGNAL   NETWORK",CompactRenderer::cyan);
        wifi_render_.keys(4,160,{{"TAB","DASH"},{"ESC","BACK"}});
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
        weather_render_.badge(1,1,28);weather_render_.text(33,4,mode_==8?"OrcSDR MARINE":"OrcSDR WEATHER",CompactRenderer::green,2);

        weather_render_.text(4,39,mode_==8?"VHF MARINE  NFM  RX ONLY":"NOAA WEATHER RADIO  NFM",CompactRenderer::cyan);
        weather_render_.keys(4,144,{{"Z/C","CHANNEL"},{"S/D","VOLUME"},{"M","MUTE"}});
        weather_render_.keys(4,160,{{"TAB","DASH"},{"ESC","BACK"}});
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
        sstv_render_.text(33,3,"OrcSDR SSTV",CompactRenderer::green,2);

        sstv_render_.keys(4,159,{{"Z/C","TUNE"},{"UP/DOWN","GALLERY"},{"TAB","DASH"}});
        sstv_drawn_=true;
      }
      if(sstv_rendered_!=sstv_revision_){
        sstv_render_.rect(0,19,320,137,CompactRenderer::bg);
        char line[52];
        std::snprintf(line,sizeof(line),"%.4f MHZ  %s  GALLERY %zu",lab_target_/1e6,
          lab_target_!=lab_.center?"TUNING":sstv_index_?"GALLERY":sstv_ready_?"LIVE IMAGE":"WAITING FOR IMAGE",sstv_archived_);
        sstv_render_.text(33,21,line,CompactRenderer::white,1,47);
        if(sstv_ready_)
          for(int y=0;y<120;++y)
            for(int x=0;x<160;++x)
              sstv_render_.point(80+x,34+y,sstv_pixels_[y*160+x]);
        sstv_rendered_=sstv_revision_;
      }
      sstv_render_.badge(1,1,28);
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
        pager_render_.badge(1,1,28);pager_render_.text(33,4,"OrcSDR POCSAG",CompactRenderer::green,2);

        pager_render_.keys(4,145,{{"Z/C","TUNE 12.5K"},{"T","BAND"},{"UP/DOWN","LOG"}});
        pager_render_.keys(4,160,{{"TAB","DASH"},{"ESC","BACK"}});
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
          lab_render_.badge(1,1,28);lab_render_.text(33,4,"OrcSDR RF LAB",CompactRenderer::green,2);

          lab_render_.text(4,149,"RELATIVE TO ADC FULL SCALE",CompactRenderer::muted);
          lab_render_.keys(4,161,{{"Z/C","TUNE 100K"},{"TAB","DASH"},{"ESC","BACK"}});
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
  void run_lora(unsigned device)noexcept{
    try{
      linux_sdr::RtlReceiver radio;radio.open(device);
      uint32_t target;uint8_t sf;
      {std::lock_guard<std::mutex> lock(mutex_);target=lora_.tuning.requested;sf=lora_.sf;}
      radio.configure(target,960000);
      if(!orcsdr::lora_native::initialize()||!orcsdr::lora_preamble::configure(sf))throw std::runtime_error("LoRa workspace allocation failed");
      std::array<uint8_t,8192> buffer{};
      std::vector<uint8_t> preroll(480000),capture;capture.reserve(7680000);
      size_t ring_at=0,ring_used=0;bool capturing=false;
      uint64_t next_update=0;
      {
        std::lock_guard<std::mutex> lock(mutex_);error_.clear();lora_.tuning.applied=target;
        lora_.capturing=lora_.decoding=false;++lora_.revision;
      }
      std::clog<<"LoRa RF "<<target<<" SF "<<unsigned(sf)<<" BW 250000 rate 960000"<<std::endl;
      while(running_){
        uint32_t requested;uint8_t requested_sf;bool manual;
        {std::lock_guard<std::mutex> lock(mutex_);requested=lora_.tuning.requested;requested_sf=lora_.sf;manual=lora_.manual;lora_.manual=false;}
        if(requested!=target||requested_sf!=sf){
          radio.retune(requested);target=requested;sf=requested_sf;
          if(!orcsdr::lora_preamble::configure(sf))throw std::runtime_error("LoRa SF allocation failed");
          orcsdr::lora_preamble::reset();capture.clear();capturing=false;ring_at=ring_used=0;
          std::lock_guard<std::mutex> lock(mutex_);lora_.tuning.applied=target;lora_.capturing=false;++lora_.revision;
          std::clog<<"LoRa RF "<<target<<" SF "<<unsigned(sf)<<std::endl;
        }
        const auto bytes=radio.read_cu8(buffer.data(),buffer.size());
        const auto result=orcsdr::lora_preamble::feed(buffer.data(),bytes);
        if(!capturing&&(result.fired||manual)){
          capture.clear();
          for(size_t n=0;n<ring_used;++n)capture.push_back(preroll[(ring_at+preroll.size()-ring_used+n)%preroll.size()]);
          capturing=true;
          std::lock_guard<std::mutex> lock(mutex_);lora_.capturing=true;++lora_.revision;
        }
        if(capturing)capture.insert(capture.end(),buffer.begin(),buffer.begin()+bytes);
        for(size_t n=0;n<bytes;++n){preroll[ring_at]=buffer[n];ring_at=(ring_at+1)%preroll.size();ring_used=std::min(ring_used+1,preroll.size());}
        if(capturing&&((capture.size()>=960000&&result.quiet_samples>=144000)||capture.size()+buffer.size()>7680000)){
          {std::lock_guard<std::mutex> lock(mutex_);lora_.capturing=false;lora_.decoding=true;++lora_.revision;}
          std::array<orcsdr::lora_native::Packet,orcsdr::lora_native::kMaxPacketsPerCapture> packets{};
          orcsdr::lora_native::Stats stats;orcsdr::lora_native::Config config;
          const auto found=orcsdr::lora_native::decode_capture_in_place(capture.data(),capture.size(),960000,sf,250000,target,config,packets.data(),packets.size(),&stats);
          if(stats.workspace_failed)throw std::runtime_error("LoRa capture workspace allocation failed");
          {
            std::lock_guard<std::mutex> lock(mutex_);lora_.decoding=false;++lora_.captures;
            lora_.crc_ok+=stats.crc_ok;lora_.crc_failed+=stats.crc_failures;lora_.decode_ms=stats.decode_millis;
            for(size_t n=0;n<found;++n)lora_.add(packets[n]);
            ++lora_.revision;
          }
          for(size_t n=0;n<found;++n)if(!lora_history::append(lora_history::path(),packets[n]))std::cerr<<"LoRa packet history write failed\n";
          std::clog<<"LoRa capture bytes "<<capture.size()<<" packets "<<found<<" crc_ok "<<stats.crc_ok<<" decode_ms "<<stats.decode_millis<<std::endl;
          radio.retune(target);orcsdr::lora_preamble::reset();capture.clear();capturing=false;ring_at=ring_used=0;
        }
        if(now()>=next_update){
          std::lock_guard<std::mutex> lock(mutex_);lora_.preamble=orcsdr::lora_preamble::status();++lora_.revision;next_update=now()+250000000;
        }
      }
    }catch(const std::exception& e){
      std::lock_guard<std::mutex> lock(mutex_);error_=e.what();std::cerr<<"LoRa: "<<error_<<std::endl;
    }
    orcsdr::lora_native::release_capture_workspace();
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
          if(!acars_dashboard::make_radio_request(ac_tuning_.requested,ac_request))
            throw std::runtime_error("Invalid ACARS channel");
          meta.frequency_hz=ac_request.center_hz;meta.sample_rate_sps=ac_request.rate_sps;
        }
        if(mode==3||mode==11||mode==13||mode==14){auto r=am_.requested_radio();meta.frequency_hz=r.rf_center_hz;meta.sample_rate_sps=r.sample_rate_sps;}
      }
      if(mode==5||mode==12){meta.frequency_hz=rf_center;meta.sample_rate_sps=kDefaultSpanHz;}
      if(is_nfm(mode)){meta.frequency_hz=mode==18?lab_target_:mode==8?marine_channels_[marine_default_].hz:162400000;meta.sample_rate_sps=300000;}
      if(mode==9){meta.frequency_hz=929662500;meta.sample_rate_sps=orcsdr::pocsag::kInputSampleRateHz;}
      if(mode==10){meta.frequency_hz=145800000;meta.sample_rate_sps=300000;}
      if(mode==16){std::lock_guard<std::mutex> lock(mutex_);meta.frequency_hz=p25_.requested;meta.sample_rate_sps=960000;}
  if(mode==19){std::lock_guard<std::mutex> lock(mutex_);meta.frequency_hz=dmr_.radio_hz();meta.sample_rate_sps=960000;dmr_reset_pending_=true;}
      uint32_t ft8_dial;
      {std::lock_guard<std::mutex> lock(mutex_);ft8_dial=ft8_band_.dial();}
      orcsdr::ftx::Mode ft8_mode;
      {std::lock_guard<std::mutex> lock(mutex_);ft8_dial=ft8_band_.dial();ft8_mode=ft8_band_.mode==orcsdr::ft8::DigitalMode::ft4?orcsdr::ftx::Mode::ft4:orcsdr::ftx::Mode::ft8;}
      if(mode==15){meta.frequency_hz=ft8_dial;meta.sample_rate_sps=960000;}
      if(mode==11||mode==13||(mode==15&&ft8_dial<24000000))radio.configure_direct_q(meta.frequency_hz,meta.sample_rate_sps);
      else radio.configure(meta.frequency_hz,meta.sample_rate_sps);
      orcsdr::pocsag::Decoder pager;
      orcnode_fft_t satellite_fft{};
      if(mode==18)orcnode_fft_init(&satellite_fft);
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
        if(!ft8->begin(ft8_mode))throw std::runtime_error("FT8 decoder allocation failed");
        ft8->observe_audio(+[](const int16_t* pcm,size_t count,bool reset,void* context){
          auto& self=*static_cast<DashboardSession*>(context);
          std::lock_guard<std::mutex> lock(self.mutex_);
          self.ft8_visual_.consume(pcm,count,reset);
        },this);
        ft8_block.cu8.reserve(8192);
        std::lock_guard<std::mutex> lock(mutex_);
        ft8_applied_=ft8_dial;ft8_applied_mode_=ft8_band_.mode;
        ft8_header_dirty_=true;
      }
      P25Follow p25_follow;
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
        error_.clear();
        if(mode==18){satellite_.signal.connection=Connection::ready;satellite_.signal.audio_ready=audio_open&&audio.sink.audible();satellite_.tuning.applied=meta.frequency_hz;}
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
        if(mode==2){
          uint32_t target;{std::lock_guard<std::mutex> lock(mutex_);target=ac_tuning_.requested;}
          if(target!=ac_request.channel_hz){
            acars_dashboard::RadioRequest next;
            if(!acars_dashboard::make_radio_request(target,next))throw std::runtime_error("Invalid ACARS channel request");
            radio.retune(next.center_hz);ac_request=next;
            meta.frequency_hz=radio.frequency_hz();meta.first_sample_index=0;meta.receiver_generation=++generation_;
            std::lock_guard<std::mutex> lock(mutex_);
            if(!ac_.start(ac_request,meta.receiver_generation))throw std::runtime_error("ACARS retune failed");
            std::clog<<"ACARS channel "<<target<<" RF "<<meta.frequency_hz<<std::endl;
          }
        }
        if(needs_audio&&!audio.sink.opened()&&now()>=audio_retry_ns){
          audio_retry_ns=now()+1000000000ULL;
          const bool restored=audio.sink.open(output,silent);
          std::lock_guard<std::mutex> lock(mutex_);
          const bool audible=restored&&audio.sink.audible();
          if(mode==16){p25_.audio_ready=audible;++p25_.revision;}
          else if(is_nfm(mode)){weather_audio_=audible;if(mode==18)satellite_.signal.audio_ready=audible;}
          else am_.audio_available(audible,restored?"":audio.sink.last_error().c_str());
          if(restored)std::clog<<"Dashboard "<<mode<<" audio restored"<<std::endl;
        }
        if(mode==16){
          uint32_t control;bool enabled,hold;orcsdr::p25core::Modulation modulation;
          {std::lock_guard<std::mutex> lock(mutex_);control=p25_.requested;modulation=p25_.modulation;enabled=p25_.auto_follow;hold=p25_.hold;}
          uint32_t survey_target=0;
          {std::lock_guard<std::mutex> lock(mutex_);if(p25_.survey_active)survey_target=p25_.service_survey(uint32_t(now()/1000));}
          if(survey_target&&survey_target!=meta.frequency_hz){
            radio.retune(survey_target);meta.frequency_hz=radio.frequency_hz();
            std::lock_guard<std::mutex> lock(mutex_);p25_.applied=meta.frequency_hz;p25_.state={};++p25_.revision;
          }
          {std::lock_guard<std::mutex> lock(mutex_);control=p25_.requested;}
          const uint32_t target=p25_follow.update(control,enabled,hold,orcsdr::p25core::snapshot(),uint32_t(now()/1000000));
          {
            std::lock_guard<std::mutex> lock(mutex_);
            if(p25_.following!=p25_follow.voice||p25_.held_talkgroup!=p25_follow.held){
              p25_.following=p25_follow.voice;p25_.held_talkgroup=p25_follow.held;++p25_.revision;
            }
            p25_.followed_grant=p25_follow.grant;
          }
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
          uint32_t target;orcsdr::ft8::DigitalMode requested_mode;
          {std::lock_guard<std::mutex> lock(mutex_);target=ft8_band_.dial();requested_mode=ft8_band_.mode;}
          const auto next_mode=requested_mode==orcsdr::ft8::DigitalMode::ft4?orcsdr::ftx::Mode::ft4:orcsdr::ftx::Mode::ft8;
          if(target!=ft8_dial||next_mode!=ft8_mode){
            // Reopen to reset both the tuner/direct-Q path and buffered IQ.
            radio.close();radio.open(device);
            if(target<24000000)radio.configure_direct_q(target,meta.sample_rate_sps);
            else radio.configure(target,meta.sample_rate_sps);
            ft8_dial=radio.frequency_hz();
            if(next_mode!=ft8_mode){
              if(!ft8->begin(next_mode))throw std::runtime_error("Digital mode decoder allocation failed");
              ft8_mode=next_mode;
            }
            meta.frequency_hz=ft8_dial;
            meta.first_sample_index=0;
            meta.receiver_generation=++generation_;
            std::lock_guard<std::mutex> lock(mutex_);
            ft8_applied_=ft8_dial;ft8_applied_mode_=requested_mode;
            ft8_header_dirty_=true;
            ft8_lines_.clear();ft8_last_utc_=0;ft8_lines_epoch_=0;ft8_visual_.clear();
            std::clog<<orcsdr::ft8::mode_name(requested_mode)<<" band RF "<<ft8_dial<<std::endl;
          }
        }
        if(mode==5||is_nfm(mode)||mode==9||mode==10){
          uint32_t target;
          {std::lock_guard<std::mutex> lock(mutex_);target=lab_target_;}
          if(target!=meta.frequency_hz){
            try{
              radio.retune(target);
              meta.frequency_hz=radio.frequency_hz();
              if(mode==18){
                std::lock_guard<std::mutex> lock(mutex_);satellite_.tuning.applied=meta.frequency_hz;
                satellite_.signal.waterfall_rows=0;satellite_.signal.live_spectrum=false;
              }
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
        if(mode==18&&meta.monotonic_ns>=next_measurement){
          std::array<uint8_t,256> row{};spectrum_row(satellite_fft,buffer.data(),row);
          std::lock_guard<std::mutex> lock(mutex_);
          auto& state=satellite_.signal;state.spectrum=row;state.live_spectrum=true;
          for(int y=kWaterfallRows-1;y>0;--y)state.waterfall[y]=state.waterfall[y-1];
          state.waterfall[0]=row;state.waterfall_rows=std::min(kWaterfallRows,state.waterfall_rows+1);
          next_measurement=meta.monotonic_ns+100000000;
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
            weather_audio_=false;if(mode==18)satellite_.signal.audio_ready=false;
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
        if(mode==19){
          // DMR: 2.4 MS/s CU8 -> CIC decimator -> DV channeliser -> d1 chain.
          // The statics keep DSP state between blocks; a retune calls
          // discontinuity() so the protocol side does not stitch across it.
          static orc::zero::dv::CicDecimator cic(4);  // 960 kS/s -> 240 kS/s
          static orcsdr::dv::Channelizer channelizer;
          static orcsdr::dmr::d1::integration::Receiver dmr_rx;
          if(dmr_reset_pending_){
            dmr_reset_pending_=false;
            cic.reset();channelizer.reset();dmr_rx.reset();
            dmr_voice_.reset();
            // Decode into the player on this thread; the UI thread drains it.
            dmr_rx.set_voice_sink([](const orcsdr::dmr::d1::VoiceGroup& group,void* user) noexcept {
              static_cast<orc::zero::dv::VoicePlayer*>(user)->decode_superframe(group.payload.data(),group.payload.size());
            },&dmr_voice_);
            if(!dmr_audio_tried_){
              dmr_audio_tried_=true;
              dmr_audio_.open("plughw:ES8389Audio,0",false);
            }
          }
          std::vector<std::uint8_t> decimated(bytes/4+16);
          const size_t decim_pairs=cic.process_cu8(buffer.data(),bytes/2,decimated.data(),decimated.size()/2);
          std::vector<orcsdr::dv::Iq48> baseband(decim_pairs/5+8);
          const size_t produced=channelizer.process_cu8(decimated.data(),decim_pairs,baseband.data(),baseband.size());
          const double dv_level=double(channelizer.take_level());
          if(produced){
            const auto* samples=reinterpret_cast<const orcsdr::dmr::d1::ComplexSample*>(baseband.data());
            dmr_rx.process(samples,produced,[](const orcsdr::dmr::d1::integration::Event& event,void* user) noexcept{
              auto* self=static_cast<DashboardSession*>(user);
              std::lock_guard<std::mutex> lock(self->mutex_);
              self->dmr_.observe(event);
            },this);
          }
          {
            std::lock_guard<std::mutex> lock(mutex_);
            dmr_.set_level(dv_level);
            dmr_.update_stats(dmr_rx.stats());
          }
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
              if(count){ft8_lines_.clear();ft8_log_offset_=0;ft8_lines_epoch_=epoch;}
              for(size_t n=0;n<count;++n)ft8_map_.add(rows[n]);
              for(size_t n=0;n<std::min<size_t>(count,6);++n){
                std::string line(rows[n].message,strnlen(rows[n].message,sizeof(rows[n].message)));
                for(auto& ch:line)if(ch<32||ch>126)ch='?';
                ft8_lines_.push_back(std::move(line));
              }
              std::clog<<(ft8_mode==orcsdr::ftx::Mode::ft4?"FT4":"FT8")<<" slot "<<epoch<<" messages "<<count<<std::endl;
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
    if(!running_&&(mode==3||mode==11||mode==13||mode==14))am_.stop();
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
  AcarsTuning ac_tuning_;
  acars_dashboard::ArchiveWorker ac_archive_;
  CompactRenderer ac_frame_;
  SatelliteScreen satellite_;
  LoraScreen lora_;
  P25Screen p25_;
  Ft8Visuals ft8_visual_;
  Ft8WorldMap ft8_map_;
  DmrScreen dmr_;
  orc::zero::dv::VoicePlayer dmr_voice_;
  orc::zero::AudioSink dmr_audio_;
  bool dmr_audio_tried_=false;
bool dmr_reset_pending_=false;
bool ft8_map_open_=false;int ft8_page_=0;size_t ft8_log_offset_=0;
  uint64_t ft8_visual_painted_=~uint64_t{0},ft8_map_painted_=~uint64_t{0};
  unsigned ft8_visual_palette_=~0u;
  ClockControl clock_;
  bool clock_open_=false,clock_entry_=false;
  std::string clock_digits_;
  std::time_t clock_tick_=0;
  orcsdr::ft8::DigitalMode ft8_applied_mode_=orcsdr::ft8::DigitalMode::ft8;
  Ft8BandSelection ft8_band_;
  uint32_t ft8_applied_=0;
  bool ft8_header_dirty_=true;
  CompactRenderer ft8_screen_;
  std::vector<std::string> ft8_lines_;
  uint64_t ft8_last_utc_=0,ft8_render_epoch_=0,ft8_lines_epoch_=0;
  bool ft8_drawn_=false;
  std::unique_ptr<DeadAirStorage> dead_air_storage_;
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
