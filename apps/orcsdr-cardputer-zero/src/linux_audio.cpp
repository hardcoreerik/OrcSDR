#include "linux_audio.hpp"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <stdexcept>
#ifdef ORC_HAVE_ALSA
#include <alsa/asoundlib.h>
#endif

namespace orc::zero {
AudioSink::~AudioSink(){close();}
bool AudioSink::open(const std::string& device,bool null_sink){
  close();last_error_.clear();underruns_=short_writes_=frames_written_=0;gain_=0;
  if(null_sink){null_=true;channels_=1;scratch_.resize(4096);return true;}
#ifdef ORC_HAVE_ALSA
  snd_pcm_t* pcm=nullptr;
  int rc=snd_pcm_open(&pcm,device.c_str(),SND_PCM_STREAM_PLAYBACK,0);
  if(rc<0){last_error_=std::string("ALSA open: ")+snd_strerror(rc);return false;}
  snd_pcm_hw_params_t* p=nullptr;
  snd_pcm_hw_params_alloca(&p);
  rc=snd_pcm_hw_params_any(pcm,p);
  if(rc>=0)rc=snd_pcm_hw_params_set_access(pcm,p,SND_PCM_ACCESS_RW_INTERLEAVED);
  if(rc>=0)rc=snd_pcm_hw_params_set_format(pcm,p,SND_PCM_FORMAT_S16_LE);
  unsigned channels=2;
  if(rc>=0){int cr=snd_pcm_hw_params_set_channels(pcm,p,2);
    if(cr<0){channels=1;rc=snd_pcm_hw_params_set_channels(pcm,p,1);}}
  if(rc>=0)rc=snd_pcm_hw_params_set_rate(pcm,p,48000,0);
  unsigned buffer_us=80000,period_us=20000;
  if(rc>=0)rc=snd_pcm_hw_params_set_buffer_time_near(pcm,p,&buffer_us,nullptr);
  if(rc>=0)rc=snd_pcm_hw_params_set_period_time_near(pcm,p,&period_us,nullptr);
  if(rc>=0)rc=snd_pcm_hw_params(pcm,p);
  if(rc>=0)rc=snd_pcm_prepare(pcm);
  if(rc<0){last_error_=std::string("ALSA format 48k S16: ")+snd_strerror(rc);snd_pcm_close(pcm);return false;}
  pcm_=pcm;channels_=channels;scratch_.resize(4096*channels);
  return true;
#else
  (void)device;last_error_="ALSA development support not built (install libasound2-dev)";return false;
#endif
}
void AudioSink::close() noexcept {
#ifdef ORC_HAVE_ALSA
  if(pcm_) {snd_pcm_drop(static_cast<snd_pcm_t*>(pcm_));snd_pcm_close(static_cast<snd_pcm_t*>(pcm_));}
#endif
  pcm_=nullptr;null_=false;channels_=0;scratch_.clear();gain_=0.0f;
}
void AudioSink::discard() noexcept {
#ifdef ORC_HAVE_ALSA
  if(pcm_) {auto* pcm=static_cast<snd_pcm_t*>(pcm_);snd_pcm_drop(pcm);snd_pcm_prepare(pcm);}
#endif
  gain_=0;
}
bool AudioSink::write_mono(const int16_t* mono,size_t frames,int volume,bool mute){
  if(!opened()||!mono||frames>4096){last_error_="Audio sink not open or invalid PCM frame count";return false;}
  const float target=(mute?0.0f:static_cast<float>(std::clamp(volume,0,100))/100.0f);
  for(size_t i=0;i<frames;++i){
    // 10 ms approximate gain smoothing at 48k to avoid mute/tune clicks.
    gain_+=(target-gain_)*0.002f;
    const auto s=static_cast<int16_t>(std::clamp(std::lround(double(mono[i])*gain_),-32768l,32767l));
    for(unsigned c=0;c<channels_;++c)scratch_[i*channels_+c]=s;
  }
  if(null_){frames_written_+=frames;return true;}
#ifdef ORC_HAVE_ALSA
  auto* pcm=static_cast<snd_pcm_t*>(pcm_);
  size_t written=0;
  unsigned attempts=0;
  while(written<frames){
    const auto rc=snd_pcm_writei(pcm,scratch_.data()+written*channels_,frames-written);
    if(rc>0){written+=static_cast<size_t>(rc);attempts=0;if(written<frames)++short_writes_;continue;}
    if(rc==-EPIPE||rc==-ESTRPIPE){++underruns_;if(snd_pcm_prepare(pcm)<0)break;}
    else if(rc==-EAGAIN){if(snd_pcm_wait(pcm,50)<0)break;}
    else {last_error_=std::string("ALSA write: ")+snd_strerror(static_cast<int>(rc));break;}
    if(++attempts>8){last_error_="ALSA audio output repeatedly stalled";break;}
  }
  frames_written_+=written;
  if(written!=frames){if(last_error_.empty())last_error_="ALSA audio short write/timeout";return false;}
  return true;
#else
  last_error_="ALSA not compiled";return false;
#endif
}
}