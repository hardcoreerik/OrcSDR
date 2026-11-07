#include "iq_metrics.hpp"
#include "rtl_receiver.hpp"
#include "live_capture.hpp"
#include "acars_core.hpp"
#include "acars_selftest.hpp"
#include "adsb_decoder.hpp"
#include <array>

#include <cstdint>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using orc::linux_sdr::RtlReceiver;

void usage() {
    std::cout << "OrcSDR Cardputer Zero / Linux receiver bring-up\n"
              << "  --demo                        synthetic IQ smoke test\n"
              << "  --iq-file PATH [--samples N]  analyze unsigned 8-bit interleaved IQ\n"
              << "  --list                        enumerate system librtlsdr devices\n"
              << "  --acars-demo                  decode original OrcSDR ACARS synthetic fixture\n"
              << "  --decode-acars PATH [--freq HZ]   CU8 file: 240kS/s, carrier=center+60kHz\n"
              << "  --decode-adsb PATH            CU8 file: 2.048MS/s, centered 1090MHz\n"
              << "  --make-acars-fixture PATH     generate reproducible 240kS/s CU8 file\n"
              << "  --capture ... --save PATH     save the actual receiver CU8 samples\n"
              << "  --live-acars [--device N] [--freq HZ] [--blocks N]  capture/decode 240k CU8\n"
              << "  --live-adsb  [--device N] [--freq HZ] [--blocks N]  capture/decode 2.048M CU8\n"
              << "  --capture [--device N] [--freq HZ] [--rate SPS] [--samples N]\n"
              << "Defaults: device 0, 100100000 Hz, 1024000 sps, 32768 samples.\n"
              << "This experimental bring-up is receive-only; no GUI or audio yet.\n";
}
std::uint32_t parse_number(const std::string& value, std::uint32_t min, std::uint32_t max) {
    if (value.empty() || value[0] == '-') throw std::invalid_argument("invalid unsigned number");
    std::size_t used = 0;
    const unsigned long long result = std::stoull(value, &used);
    if (used != value.size() || result < min || result > max)
        throw std::invalid_argument("number outside supported range");
    return static_cast<std::uint32_t>(result);
}
void print_metrics(const std::vector<std::uint8_t>& iq) {
    const auto m = orc::analyze_u8_iq(iq);
    std::cout << "samples=" << m.sample_count << " mean_power_dbfs="
              << std::fixed << std::setprecision(2) << m.mean_power_dbfs
              << " peak_dft64_bin=" << m.peak_bin << '\n';
}
void list_receivers() {
    if (!RtlReceiver::available())
        throw std::runtime_error("librtlsdr not detected at build time; install librtlsdr-dev and rebuild");
    const auto found=RtlReceiver::enumerate();
    std::cout<<"rtl_sdr_devices="<<found.size()<<'\n';
    for(const auto& r:found)
      std::cout<<r.index<<": "<<r.name<<" manufacturer="<<r.manufacturer
               <<" product="<<r.product<<" serial="<<r.serial<<'\n';
}
std::vector<std::uint8_t> capture(std::uint32_t index, std::uint32_t freq,
                                  std::uint32_t rate, std::uint32_t samples) {
    if (!RtlReceiver::available())
        throw std::runtime_error("librtlsdr not detected at build time; install librtlsdr-dev and rebuild");
    RtlReceiver receiver;
    receiver.open(index);
    receiver.configure(freq,rate);
    const auto caps=receiver.capabilities();
    std::vector<std::uint8_t> iq(static_cast<std::size_t>(samples)*2);
    const std::size_t got=receiver.read_cu8(iq.data(),iq.size());
    iq.resize(got);
    std::cout<<"device="<<index<<" freq_hz="<<receiver.frequency_hz()
             <<" sample_rate="<<receiver.sample_rate_sps()
             <<" tuner_type_id="<<caps.tuner_type
             <<" gain_steps="<<caps.tuner_gains_tenth_db.size()
             <<" requested_samples="<<samples<<" received_samples="<<got/2
             <<" short_read="<<(got/2!=samples?1:0)<<'\n';
    return iq;
}

struct AcarsChain {
    orcsdr::acars::Frontend240k frontend;
    orcsdr::acars::Decoder decoder;
    unsigned messages=0;
    explicit AcarsChain(std::uint32_t center) {
        if(center>UINT32_MAX-60000u)throw std::invalid_argument("ACARS RF center overflow");
        decoder.set_frequency(center+60000u);
        decoder.set_callback(on_message,this);
    }
    static void on_message(void* ptr,const orcsdr::acars::Message& m) {
        auto& self=*static_cast<AcarsChain*>(ptr);
        ++self.messages;
        std::cout<<"ACARS frequency_hz="<<m.frequency_hz<<" tail="<<m.tail
                 <<" label="<<m.label<<" corrected_bits="<<unsigned(m.corrected_bits)
                 <<" text=";
        for(unsigned i=0;i<m.text_length;++i) {
            const unsigned char c=static_cast<unsigned char>(m.text[i]);
            std::cout<<((c>=32 && c<=126)?char(c):'.');
        }
        std::cout<<'\n';
    }
    static void on_channel(void* ptr,float i,float q) {
        static_cast<AcarsChain*>(ptr)->decoder.process_channel_iq(i,q);
    }
    void feed(const std::uint8_t* data,std::size_t bytes) {
        frontend.process_cu8(data,bytes/2,on_channel,this);
    }
    void summary() const {
        const auto& stats=decoder.stats();
        std::cout<<"ACARS messages="<<messages<<" crc_ok="<<stats.crc_ok
                 <<" corrected="<<stats.corrected<<" crc_rejected="<<stats.crc_rejected<<'\n';
    }
};
struct AdsbChain {
    orcsdr::adsb_rx::Decoder decoder;
    unsigned messages=0;
    static void on_frame(const orcsdr::adsb_rx::Frame& f,void* ptr) {
        auto& self=*static_cast<AdsbChain*>(ptr);
        ++self.messages;
        std::cout<<"ADS-B icao="<<std::hex<<std::setw(6)<<std::setfill('0')<<f.icao
                 <<std::dec<<std::setfill(' ')<<" bits="<<unsigned(f.bit_length);
        if(f.has_callsign)std::cout<<" callsign="<<f.callsign;
        if(f.has_altitude)std::cout<<" altitude_ft="<<f.altitude_ft;
        std::cout<<'\n';
    }
    void feed(const std::uint8_t* data,std::size_t bytes) {
        decoder.process_cu8(data,bytes,on_frame,this);
    }
};

int make_acars_fixture(const std::string& path) {
    std::ofstream out(path,std::ios::binary|std::ios::trunc);
    if(!out)throw std::runtime_error("failed to open ACARS fixture output");
    orcsdr::acars_selftest::Signal fixture(3023,0.0,0x1234);
    std::array<std::uint8_t,4096> buf{};
    std::uint64_t bytes_total=0;
    for(;;) {
        const auto n=fixture.next(buf.data(),buf.size());
        if(!n)break;
        out.write(reinterpret_cast<const char*>(buf.data()),static_cast<std::streamsize>(n));
        if(!out)throw std::runtime_error("could not write complete ACARS fixture");
        bytes_total+=n;
    }
    out.close();
    if(!out)throw std::runtime_error("failed to finalize ACARS fixture");
    std::cout<<"ACARS fixture bytes="<<bytes_total<<" CU8_samples="<<bytes_total/2<<'\n';
    return bytes_total?0:2;
}
void write_capture_file(const std::string& path,const std::vector<std::uint8_t>& iq) {
    std::ofstream out(path,std::ios::binary|std::ios::trunc);
    if(!out)throw std::runtime_error("could not open IQ output");
    out.write(reinterpret_cast<const char*>(iq.data()),static_cast<std::streamsize>(iq.size()));
    if(!out)throw std::runtime_error("could not save all captured IQ bytes");
    std::cout<<"saved_cu8_bytes="<<iq.size()<<" output="<<path<<'\n';
}
int acars_demo() {
    AcarsChain chain(131490000u);
    orcsdr::acars_selftest::Signal fixture(3023,0.0,0x1234);
    std::array<std::uint8_t,4096> buf{};
    for(;;) {
        const auto n=fixture.next(buf.data(),buf.size());
        if(!n)break;
        chain.feed(buf.data(),n);
    }
    chain.summary();
    return chain.messages==1 && chain.decoder.stats().crc_ok==1?0:2;
}
int decode_file(const std::string& path,bool acars,std::uint32_t center_hz) {
    std::ifstream in(path,std::ios::binary);
    if(!in)throw std::runtime_error("could not open CU8 IQ file");
    AcarsChain acars_chain(center_hz);
    AdsbChain adsb_chain;
    std::array<std::uint8_t,8192> buf{};
    std::uint64_t samples=0;
    for(;;) {
        in.read(reinterpret_cast<char*>(buf.data()),static_cast<std::streamsize>(buf.size()));
        const auto got=in.gcount();
        if(!got)break;
        if(got<0||(got&1))throw std::runtime_error("incomplete CU8 sample in input file");
        if(acars)acars_chain.feed(buf.data(),static_cast<std::size_t>(got));
        else adsb_chain.feed(buf.data(),static_cast<std::size_t>(got));
        samples+=static_cast<std::uint64_t>(got/2);
    }
    std::cout<<"input_complex_samples="<<samples<<'\n';
    if(acars) {acars_chain.summary();return acars_chain.messages?0:2;}
    std::cout<<"ADS-B frames="<<adsb_chain.messages
             <<" crc_ok="<<adsb_chain.decoder.stats().crc_ok<<'\n';
    return adsb_chain.messages?0:2;
}
}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1) { usage(); return 0; }
        std::string mode,path,save_path;
        std::uint32_t device=0,freq=100100000,rate=1024000,samples=32768,blocks=32;
        bool freq_explicit=false,rate_explicit=false;
        for (int i=1;i<argc;++i) {
            const std::string arg=argv[i];
            if (arg=="--help") { usage(); return 0; }
            if (arg=="--demo" || arg=="--acars-demo" || arg=="--list" || arg=="--capture" ||
                arg=="--live-acars" || arg=="--live-adsb") {
                if(!mode.empty())throw std::invalid_argument("select exactly one mode");
                mode=arg;
            } else if (arg=="--iq-file" || arg=="--decode-acars" || arg=="--decode-adsb" || arg=="--make-acars-fixture") {
                if(!mode.empty())throw std::invalid_argument("select exactly one mode");
                mode=arg;
                if(++i>=argc)throw std::invalid_argument(arg+" needs path");
                path=argv[i];
            } else {
                if(++i>=argc)throw std::invalid_argument("missing option value");
                const std::string value=argv[i];
                if(arg=="--device")device=parse_number(value,0,1024);
                else if(arg=="--freq"){freq=parse_number(value,100000,3000000000U);freq_explicit=true;}
                else if(arg=="--rate"){rate=parse_number(value,225000,3200000);rate_explicit=true;}
                else if(arg=="--blocks")blocks=parse_number(value,1,4096);
                else if(arg=="--samples")samples=parse_number(value,64,262144);
                else if(arg=="--save")save_path=value;
                else throw std::invalid_argument("unknown option: "+arg);
            }
        }
        if(!save_path.empty() && mode!="--capture")throw std::invalid_argument("--save requires --capture");
        if(mode=="--demo")print_metrics(orc::synthetic_u8_tone(4096));
        else if(mode=="--make-acars-fixture")return make_acars_fixture(path);
        else if(mode=="--acars-demo")return acars_demo();
        else if(mode=="--decode-acars")return decode_file(path,true,freq);
        else if(mode=="--decode-adsb")return decode_file(path,false,freq);
        else if(mode=="--live-acars" || mode=="--live-adsb") {
            orc::linux_sdr::LiveCaptureOptions options{};
            options.decoder=(mode=="--live-acars")?orc::linux_sdr::LiveDecoder::acars:
                                                    orc::linux_sdr::LiveDecoder::adsb;
            options.device_index=device;
            options.center_hz=freq_explicit?freq:(mode=="--live-acars"?131490000u:1090000000u);
            options.sample_rate_sps=rate_explicit?rate:(mode=="--live-acars"?240000u:2048000u);
            options.max_blocks=blocks;
            (void)orc::linux_sdr::run_librtlsdr_blocks(options,std::cout);
        }
        else if(mode=="--iq-file") {
            std::ifstream in(path,std::ios::binary);
            if(!in)throw std::runtime_error("failed to open IQ file");
            std::vector<std::uint8_t> iq(static_cast<std::size_t>(samples)*2);
            in.read(reinterpret_cast<char*>(iq.data()),static_cast<std::streamsize>(iq.size()));
            const auto got=in.gcount();
            if(got<128||(got%2))throw std::runtime_error("IQ file requires at least 64 complete samples");
            iq.resize(static_cast<std::size_t>(got));
            print_metrics(iq);
        } else if(mode=="--list")list_receivers();
        else if(mode=="--capture") {
            auto iq=capture(device,freq,rate,samples);
            if(!save_path.empty())write_capture_file(save_path,iq);
            print_metrics(iq);
        }
        else throw std::invalid_argument("no mode supplied");
        return 0;
    } catch(const std::exception& e) {
        std::cerr<<"orcsdr-cardputer-zero: "<<e.what()<<'\n';
        return 1;
    }
}
