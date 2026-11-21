#include "live_capture.hpp"
#include "iq_queue.hpp"
#include "rtl_receiver.hpp"
#include "acars_core.hpp"
#include "adsb_decoder.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
#include <ostream>
#include <iomanip>

namespace orc::linux_sdr {
namespace {
void validate(const LiveCaptureOptions& o) {
  if (!o.max_blocks || o.max_blocks>4096 || !o.complex_samples_per_block ||
      o.complex_samples_per_block>131072 ||
      o.complex_samples_per_block>(static_cast<std::size_t>(std::numeric_limits<int>::max())/2))
    throw std::invalid_argument("invalid bounded capture block count/size");
  if (o.decoder==LiveDecoder::acars) {
    if (o.sample_rate_sps!=240000 || o.center_hz>UINT32_MAX-60000u)
      throw std::invalid_argument("ACARS expects exact 240000 S/s CU8 with +60000 Hz carrier");
  } else if (o.decoder==LiveDecoder::adsb) {
    if (o.sample_rate_sps!=2048000)
      throw std::invalid_argument("ADS-B expects exact 2048000 S/s CU8");
  } else throw std::invalid_argument("unsupported native decoder");
}
std::uint64_t now_ns() {
  return static_cast<std::uint64_t>(
    std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}
struct DecoderWorker {
  LiveDecoder kind;
  std::ostream& out;
  orcsdr::acars::Frontend240k frontend;
  orcsdr::acars::Decoder acars;
  orcsdr::adsb_rx::Decoder adsb;
  std::uint64_t count=0;
  explicit DecoderWorker(LiveDecoder selected,std::uint32_t center,std::ostream& stream)
      : kind(selected),out(stream) {
    acars.set_frequency(center+60000u);
    acars.set_callback(acars_message,this);
    adsb.reset();
  }
  static void acars_message(void* ctx,const orcsdr::acars::Message& m) {
    auto& self=*static_cast<DecoderWorker*>(ctx);
    ++self.count;
    self.out<<"ACARS frequency_hz="<<m.frequency_hz
            <<" tail="<<m.tail<<" label="<<m.label
            <<" corrected_bits="<<static_cast<unsigned>(m.corrected_bits)<<" text=";
    for(unsigned i=0;i<m.text_length;++i) {
      const unsigned char c=static_cast<unsigned char>(m.text[i]);
      self.out<<(c>=32&&c<=126?static_cast<char>(c):'.');
    }
    self.out<<'\n';
  }
  static void channel_sample(void* ctx,float i,float q) {
    static_cast<DecoderWorker*>(ctx)->acars.process_channel_iq(i,q);
  }
  static void adsb_frame(const orcsdr::adsb_rx::Frame& f,void* ctx) {
    auto& self=*static_cast<DecoderWorker*>(ctx);
    ++self.count;
    self.out<<"ADS-B icao="<<std::hex<<std::setw(6)<<std::setfill('0')<<f.icao
            <<std::dec<<std::setfill(' ')<<" bits="<<static_cast<unsigned>(f.bit_length);
    if(f.has_callsign)self.out<<" callsign="<<f.callsign;
    if(f.has_altitude)self.out<<" altitude_ft="<<f.altitude_ft;
    self.out<<'\n';
  }
  void reset_stream() {
    if(kind==LiveDecoder::acars) {frontend.reset();acars.reset_stream();}
    else adsb.reset();
  }
  void feed(const std::uint8_t* data,std::size_t bytes) {
    if(kind==LiveDecoder::acars)
      frontend.process_cu8(data,bytes/2,channel_sample,this);
    else
      adsb.process_cu8(data,bytes,adsb_frame,this);
  }
};
struct ProducerResults {
  std::uint64_t read=0;
  std::uint64_t short_reads=0;
};
} // namespace

LiveCaptureResult run_live_blocks(const LiveCaptureOptions& o,
                                 const BlockReader& read,
                                 std::ostream& events,std::size_t queue_blocks) {
  validate(o);
  if (!read)throw std::invalid_argument("capture requires a block reader");
  BoundedIqQueue queue(queue_blocks,o.complex_samples_per_block*2);
  DecoderWorker decoder(o.decoder,o.center_hz,events);
  std::mutex wake_mutex;
  std::condition_variable wake;
  std::atomic<bool> done{false};
  std::exception_ptr producer_failure;
  ProducerResults prod{};
  LiveCaptureResult result{};
  std::thread producer([&] {
    try {
      std::vector<std::uint8_t> buf(o.complex_samples_per_block*2);
      std::uint64_t first_sample=0;
      for(std::uint32_t n=0;n<o.max_blocks;++n) {
        const std::size_t got=read(buf.data(),buf.size());
        if(!got)break; // deterministic replay end; hardware read errors throw
        if(got>buf.size()||(got&1u)||got<128)
          throw std::runtime_error("receiver returned invalid/incomplete CU8 block");
        if(got!=buf.size())++prod.short_reads;
        IqMetadata m{};
        m.frequency_hz=o.center_hz;
        m.sample_rate_sps=o.sample_rate_sps;
        m.receiver_generation=1;
        m.first_sample_index=first_sample;
        m.monotonic_ns=now_ns(); // read-completion observation, not a hardware ADC timestamp
        (void)queue.try_push(buf.data(),got,m);
        first_sample+=got/2;
        prod.read+=got/2;
        wake.notify_one();
      }
    } catch(...) { producer_failure=std::current_exception(); }
    done.store(true,std::memory_order_release);
    wake.notify_one();
  });

  IqBlock block;
  std::uint64_t expected_sample=0;
  bool first=true;
  for(;;) {
    if(queue.try_pop(block)) {
      if(block.meta.discontinuity ||
         (!first&&block.meta.first_sample_index!=expected_sample) ||
         block.meta.receiver_generation!=1) {
        decoder.reset_stream();
        ++result.decoder_resets;
      }
      first=false;
      expected_sample=block.meta.first_sample_index+block.cu8.size()/2;
      decoder.feed(block.cu8.data(),block.cu8.size());
      result.complex_samples_decoded+=block.cu8.size()/2;
    } else {
      if(done.load(std::memory_order_acquire))break;
      std::unique_lock<std::mutex> lock(wake_mutex);
      wake.wait_for(lock,std::chrono::milliseconds(25),[&]{
        return done.load(std::memory_order_acquire)||queue.queued_blocks()!=0;
      });
    }
  }
  producer.join();
  if(producer_failure)std::rethrow_exception(producer_failure);
  result.complex_samples_read=prod.read;
  result.short_reads=prod.short_reads;
  result.known_queue_dropped_samples=queue.dropped_samples();
  result.decoded_messages=decoder.count;
  result.applied_center_hz=o.center_hz;
  result.applied_rate_sps=o.sample_rate_sps;
  return result;
}

LiveCaptureResult run_librtlsdr_blocks(const LiveCaptureOptions& o,std::ostream& out) {
  validate(o);
  if(!RtlReceiver::available())
    throw std::runtime_error("system librtlsdr unavailable: install librtlsdr-dev and rebuild");
  RtlReceiver receiver;
  receiver.open(o.device_index);
  receiver.configure(o.center_hz,o.sample_rate_sps);
  const auto actual_center=receiver.frequency_hz();
  const auto actual_rate=receiver.sample_rate_sps();
  if(actual_center!=o.center_hz||actual_rate!=o.sample_rate_sps)
    throw std::runtime_error("librtlsdr tuning readback mismatch");
  out<<"receiver=rtl device="<<o.device_index
     <<" applied_center_hz="<<actual_center
     <<" applied_rate_sps="<<actual_rate
     <<" CU8 read_completion_time=monotonic"
     <<" usb_loss=unknown\n";
  auto result=run_live_blocks(o,[&](std::uint8_t* data,std::size_t bytes){
    return receiver.read_cu8(data,bytes);
  },out);
  out<<"capture_complex_read="<<result.complex_samples_read
     <<" capture_complex_decoded="<<result.complex_samples_decoded
     <<" known_queue_dropped_complex="<<result.known_queue_dropped_samples
     <<" decoder_resets="<<result.decoder_resets
     <<" short_reads="<<result.short_reads
     <<" decoded_messages="<<result.decoded_messages
     <<" usb_upstream_loss=unknown\n";
  return result;
}
} // namespace orc::linux_sdr
