#include "rtl_receiver.hpp"
#include <limits>
#include <stdexcept>
#include <string>
#ifdef ORC_HAVE_RTLSDR
#include <rtl-sdr.h>
#endif

namespace orc::linux_sdr {
namespace {
#ifdef ORC_HAVE_RTLSDR
void fail(const char* operation,int rc) {
  throw std::runtime_error(std::string(operation)+" failed (librtlsdr rc="+std::to_string(rc)+")");
}
#else
[[noreturn]] void unsupported() {
  throw std::runtime_error("librtlsdr not detected at build time; install librtlsdr-dev and rebuild");
}
#endif
}
RtlReceiver::~RtlReceiver(){close();}
bool RtlReceiver::available() {
#ifdef ORC_HAVE_RTLSDR
  return true;
#else
  return false;
#endif
}
std::vector<ReceiverInfo> RtlReceiver::enumerate() {
#ifdef ORC_HAVE_RTLSDR
  std::vector<ReceiverInfo> found;
  const auto count=rtlsdr_get_device_count();
  for(std::uint32_t i=0;i<count;++i) {
    ReceiverInfo info{};
    info.index=i;
    const char* name=rtlsdr_get_device_name(i);
    if (name) info.name=name;
    char manufacturer[256]{},product[256]{},serial[256]{};
    if(rtlsdr_get_device_usb_strings(i,manufacturer,product,serial)==0) {
      info.manufacturer=manufacturer;
      info.product=product;
      info.serial=serial;
    }
    found.push_back(info);
  }
  return found;
#else
  unsupported();
#endif
}
void RtlReceiver::open(std::uint32_t index) {
#ifdef ORC_HAVE_RTLSDR
  if (device_) throw std::logic_error("receiver already open");
  rtlsdr_dev_t* dev=nullptr;
  const int rc=rtlsdr_open(&dev,index);
  if(rc!=0||!dev) {
    if(dev) rtlsdr_close(dev);
    fail("open RTL-SDR (check USB host, permissions and power)",rc);
  }
  device_=dev;
#else
  (void)index;unsupported();
#endif
}
void RtlReceiver::configure(std::uint32_t frequency,std::uint32_t rate) {
#ifdef ORC_HAVE_RTLSDR
  if (!device_) throw std::logic_error("receiver not open");
  auto* dev=static_cast<rtlsdr_dev_t*>(device_);
  int rc=rtlsdr_set_sample_rate(dev,rate);
  if(rc)fail("set sample rate",rc);
  rc=rtlsdr_set_center_freq(dev,frequency);
  if(rc)fail("tune center frequency",rc);
  rc=rtlsdr_set_tuner_gain_mode(dev,0); // automatic gain for bring-up
  if(rc)fail("select automatic tuner gain",rc);
  if(rtlsdr_get_sample_rate(dev)!=rate) throw std::runtime_error("driver readback sample rate differs from request");
  if(rtlsdr_get_center_freq(dev)!=frequency) throw std::runtime_error("driver readback center frequency differs from request");
  rc=rtlsdr_reset_buffer(dev);
  if(rc)fail("reset receiver buffer",rc);
#else
  (void)frequency;(void)rate;unsupported();
#endif
}
std::size_t RtlReceiver::read_cu8(std::uint8_t* data,std::size_t bytes) {
#ifdef ORC_HAVE_RTLSDR
  if(!device_)throw std::logic_error("receiver not open");
  if(!data||bytes<128||(bytes&1u)||bytes>static_cast<std::size_t>(std::numeric_limits<int>::max()))
    throw std::invalid_argument("receiver read requires even CU8 size of at least 128 bytes");
  int got=0;
  const int rc=rtlsdr_read_sync(static_cast<rtlsdr_dev_t*>(device_),data,static_cast<int>(bytes),&got);
  if(rc)fail("USB synchronous IQ read",rc);
  if(got<128||(got&1))throw std::runtime_error("RTL-SDR returned insufficient or incomplete interleaved IQ");
  return static_cast<std::size_t>(got); // short reads are NOT silently padded
#else
  (void)data;(void)bytes;unsupported();
#endif
}
void RtlReceiver::close() noexcept {
#ifdef ORC_HAVE_RTLSDR
  if(device_){rtlsdr_close(static_cast<rtlsdr_dev_t*>(device_));device_=nullptr;}
#else
  device_=nullptr;
#endif
}
std::uint32_t RtlReceiver::frequency_hz() const {
#ifdef ORC_HAVE_RTLSDR
  if(!device_)throw std::logic_error("receiver not open");
  return rtlsdr_get_center_freq(static_cast<rtlsdr_dev_t*>(device_));
#else
  unsupported();
#endif
}
std::uint32_t RtlReceiver::sample_rate_sps() const {
#ifdef ORC_HAVE_RTLSDR
  if(!device_)throw std::logic_error("receiver not open");
  return rtlsdr_get_sample_rate(static_cast<rtlsdr_dev_t*>(device_));
#else
  unsupported();
#endif
}
ReceiverCapabilities RtlReceiver::capabilities() const {
#ifdef ORC_HAVE_RTLSDR
  if(!device_)throw std::logic_error("receiver not open");
  auto* dev=static_cast<rtlsdr_dev_t*>(device_);
  ReceiverCapabilities caps{};
  caps.tuner_type=static_cast<int>(rtlsdr_get_tuner_type(dev));
  const int count=rtlsdr_get_tuner_gains(dev,nullptr);
  if(count>0 && count<1024) {
    caps.tuner_gains_tenth_db.resize(static_cast<std::size_t>(count));
    const int received=rtlsdr_get_tuner_gains(dev,caps.tuner_gains_tenth_db.data());
    if(received<0)fail("list tuner gains",received);
    caps.tuner_gains_tenth_db.resize(static_cast<std::size_t>(received));
  }
  return caps;
#else
  unsupported();
#endif
}
}
