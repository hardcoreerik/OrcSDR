#include "fm_session.hpp"
#include "linux_audio.hpp"
#include "rtl_receiver.hpp"
#include "fm_dsp.h"
#include "scan_dsp.h"
#include "iq_spectrum.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <exception>
#include <stdexcept>
#include <thread>
#include <iostream>

namespace orc::zero {
namespace {
constexpr std::uint32_t kFmSampleRate=kDefaultSpanHz;
constexpr std::size_t kComplexPerBlock=8192;
std::uint64_t steady_ns(){
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}

}
FmSession::FmSession():queue_(8,2*kComplexPerBlock){}
FmSession::~FmSession(){stop();}
void FmSession::set_error(const std::string& text){
  std::lock_guard<std::mutex> lock(state_mutex_);
  state_.last_error=text;state_.connection=Connection::error;state_.has_spectrum=false;
  state_.waterfall_rows=0;state_.audio_ready=false;
}
void FmSession::start(std::uint32_t device,std::uint32_t initial,std::string audio_device,bool null_audio){
  stop();
  if(initial<kFmMinHz||initial>kFmMaxHz)throw std::invalid_argument("WFM frequency outside 87.5-108 MHz");
  device_index_=device;audio_device_=std::move(audio_device);null_audio_=null_audio;
  requested_hz_.store(initial);muted_.store(false);volume_.store(30);
  gain_auto_.store(true);gain_request_seq_.store(0);stream_generation_.store(0);
  queue_.flush_discontinuity();
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_=FmTelemetry{};state_.connection=Connection::connecting;state_.requested_hz=initial;
  }
  running_.store(true);
  dsp_thread_=std::thread(&FmSession::dsp_worker,this);
  capture_thread_=std::thread(&FmSession::capture_worker,this,device);
}
void FmSession::restart(std::uint32_t hz){
  const auto device=device_index_;const auto audio=audio_device_;const bool silent=null_audio_;
  start(device,hz,audio,silent);
}
void FmSession::stop(){
  running_.store(false);queue_.flush_discontinuity();wake_.notify_all();
  if(capture_thread_.joinable())capture_thread_.join();
  if(dsp_thread_.joinable())dsp_thread_.join();
  std::lock_guard<std::mutex> lock(state_mutex_);
  state_.connection=Connection::disconnected;state_.audio_ready=false;
  state_.has_spectrum=false;state_.waterfall_rows=0;
}
bool FmSession::request_tune(std::uint32_t hz){
  if(hz<kFmMinHz||hz>kFmMaxHz)return false;
  requested_hz_.store(hz);
  std::lock_guard<std::mutex> lock(state_mutex_);
  state_.requested_hz=hz;
  return true;
}
bool FmSession::request_gain(bool automatic,int tenth_db){
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if(!automatic&&std::find(state_.supported_gains.begin(),state_.supported_gains.end(),tenth_db)==state_.supported_gains.end())return false;
  }
  gain_tenth_db_.store(tenth_db);gain_auto_.store(automatic);gain_request_seq_.fetch_add(1);
  return true;
}
void FmSession::set_volume(unsigned p){volume_.store(static_cast<int>(std::min(100u,p)));}
void FmSession::set_mute(bool mute){muted_.store(mute);}
FmTelemetry FmSession::snapshot()const{
  std::lock_guard<std::mutex> lock(state_mutex_);return state_;
}
void FmSession::capture_worker(std::uint32_t device){
 while(running_.load()){
  try{
    orc::linux_sdr::RtlReceiver receiver;
    receiver.open(device);
    receiver.configure(requested_hz_.load(),kFmSampleRate);
    const auto caps=receiver.capabilities();
    std::uint32_t applied=receiver.frequency_hz();
    std::uint64_t generation=stream_generation_.fetch_add(1)+1;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      state_.connection=Connection::ready;state_.applied_hz=applied;
      state_.requested_hz=applied;state_.applied_rate_sps=receiver.sample_rate_sps();
      state_.generation=generation;state_.supported_gains=caps.tuner_gains_tenth_db;
      state_.receiver_name="RTL-SDR tuner "+std::to_string(caps.tuner_type);
      state_.last_error.clear();
      std::clog<<"FM receiver connected at "<<applied<<std::endl;
    }
    std::vector<std::uint8_t> buf(2*kComplexPerBlock);
    std::uint64_t first_sample=0,processed_gain_seq=0;
    while(running_.load()){
      const auto target=requested_hz_.load();
      if(target!=applied){
        try{
          receiver.retune(target);
          const auto actual=receiver.frequency_hz();
          if(actual!=target)throw std::runtime_error("RF center readback mismatch");
          queue_.flush_discontinuity();
          applied=actual;generation=stream_generation_.fetch_add(1)+1;
          std::lock_guard<std::mutex> lock(state_mutex_);
          state_.applied_hz=actual;state_.requested_hz=target;state_.generation=generation;
          state_.has_spectrum=false;state_.waterfall_rows=0;state_.last_error.clear();
        }catch(const std::exception& e){
          requested_hz_.store(applied);
          std::lock_guard<std::mutex> lock(state_mutex_);
          state_.requested_hz=applied;state_.last_error=std::string("Tune: ")+e.what();
        }
      }
      const auto seq=gain_request_seq_.load();
      if(seq!=processed_gain_seq){
        try{
          if(gain_auto_.load())receiver.set_tuner_gain_auto();
          else receiver.set_tuner_gain_tenth_db(gain_tenth_db_.load());
          processed_gain_seq=seq;
          std::lock_guard<std::mutex> lock(state_mutex_);
          state_.gain_auto=gain_auto_.load();state_.gain_tenth_db=receiver.tuner_gain_tenth_db();
        }catch(const std::exception& e){
          processed_gain_seq=seq;
          std::lock_guard<std::mutex> lock(state_mutex_);
          state_.last_error=std::string("Gain: ")+e.what();
        }
      }
      const auto got=receiver.read_cu8(buf.data(),buf.size());
      if(!running_.load())break;
      orc::linux_sdr::IqMetadata m{};
      // IQ metadata describes the tuner center; UI telemetry describes the selected station.
      m.frequency_hz=applied;m.sample_rate_sps=kFmSampleRate;
      m.receiver_generation=generation;m.first_sample_index=first_sample;
      m.monotonic_ns=steady_ns();
      (void)queue_.try_push(buf.data(),got,m);
      first_sample+=got/2;
      wake_.notify_one();
    }
  }catch(const std::exception& e){
    if(!running_.load())break;
    set_error(e.what());stream_generation_.fetch_add(1);queue_.flush_discontinuity();wake_.notify_all();
    std::clog<<"FM receiver unavailable; retrying: "<<e.what()<<std::endl;
    for(int n=0;n<10&&running_.load();++n)std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
 }
}
void FmSession::dsp_worker(){
  AudioSink sink;
  const bool audio_ok=sink.open(audio_device_,null_audio_);
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_.audio_ready=audio_ok&&sink.audible();state_.audio_error=audio_ok?"":sink.last_error();
  }
  orcnode_fm_dsp_t dsp{};
  orcnode_fm_dsp_init(&dsp,kFmSampleRate,75);
      dsp.mix_fs4=false;
  orcnode_fft_t fft{};orcnode_fft_init(&fft);
  std::array<std::int16_t,2048> pcm{};
  orc::linux_sdr::IqBlock block;
  std::uint64_t last_generation=0,display_ns=0,audio_retry_ns=steady_ns()+1000000000ULL;
  if(!audio_ok)std::clog<<"FM audio: "<<sink.last_error()<<"; retrying\n";
  while(running_.load()||queue_.queued_blocks()!=0){
    if(!queue_.try_pop(block)){
      std::unique_lock<std::mutex> lock(wake_mutex_);
      wake_.wait_for(lock,std::chrono::milliseconds(25),[&]{return !running_.load()||queue_.queued_blocks()>0;});
      continue;
    }
    const auto audio_now=steady_ns();
    if(!sink.opened()&&audio_now>=audio_retry_ns){
      audio_retry_ns=audio_now+1000000000ULL;
      const bool restored=sink.open(audio_device_,null_audio_);
      std::lock_guard<std::mutex> lock(state_mutex_);
      state_.audio_ready=restored&&sink.audible();
      state_.audio_error=restored?"":sink.last_error();
      if(restored)std::clog<<"FM audio restored\n";
    }
    const auto generation=stream_generation_.load();
    if(block.meta.receiver_generation!=generation)continue; // never play stale IQ
    if(block.meta.discontinuity||block.meta.receiver_generation!=last_generation){
      orcnode_fm_dsp_init(&dsp,kFmSampleRate,75);
      dsp.mix_fs4=false;
      sink.discard();last_generation=generation;
    }
    const size_t frames=orcnode_fm_dsp_process(&dsp,block.cu8.data(),block.cu8.size(),pcm.data(),pcm.size());
    if(block.meta.receiver_generation!=stream_generation_.load())continue;
    if(frames&&sink.opened()&&!sink.write_mono(pcm.data(),frames,volume_.load(),muted_.load())){
      std::lock_guard<std::mutex> lock(state_mutex_);
      state_.audio_error=sink.last_error();state_.audio_ready=false;
      std::clog<<"FM audio: "<<state_.audio_error<<"; retrying\n";
      audio_retry_ns=steady_ns()+1000000000ULL;
      sink.close();
    }
    const auto now=steady_ns();
    if(block.cu8.size()<2048||now-display_ns<90000000ULL)continue; // <=11 FPS; never in USB callback
    display_ns=now;
    std::array<std::uint8_t,256> row{};
    spectrum_row(fft,block.cu8.data(),row);
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      if(block.meta.receiver_generation!=stream_generation_.load())continue;
      state_.spectrum=row;state_.has_spectrum=true;
      for(int i=kWaterfallRows-1;i>0;--i)state_.waterfall[size_t(i)]=state_.waterfall[size_t(i-1)];
      state_.waterfall[0]=row;
      state_.waterfall_rows=std::min(kWaterfallRows,state_.waterfall_rows+1);
      state_.known_queue_dropped_samples=queue_.dropped_samples();
      state_.audio_ready=sink.audible();
      state_.audio_underruns=sink.underruns();state_.audio_frames=sink.frames_written();
    }
  }
  sink.close();
}
}