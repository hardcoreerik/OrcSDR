#include "iq_metrics.hpp"
#include "rtl_receiver.hpp"

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
}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1) { usage(); return 0; }
        std::string mode,path;
        std::uint32_t device=0,freq=100100000,rate=1024000,samples=32768;
        for (int i=1;i<argc;++i) {
            const std::string arg=argv[i];
            if (arg=="--help") { usage(); return 0; }
            if (arg=="--demo" || arg=="--list" || arg=="--capture") {
                if(!mode.empty())throw std::invalid_argument("select exactly one mode");
                mode=arg;
            } else if (arg=="--iq-file") {
                if(!mode.empty())throw std::invalid_argument("select exactly one mode");
                mode=arg;
                if(++i>=argc)throw std::invalid_argument("--iq-file needs path");
                path=argv[i];
            } else {
                if(++i>=argc)throw std::invalid_argument("missing option value");
                const std::string value=argv[i];
                if(arg=="--device")device=parse_number(value,0,1024);
                else if(arg=="--freq")freq=parse_number(value,100000,3000000000U);
                else if(arg=="--rate")rate=parse_number(value,225000,3200000);
                else if(arg=="--samples")samples=parse_number(value,64,262144);
                else throw std::invalid_argument("unknown option: "+arg);
            }
        }
        if(mode=="--demo")print_metrics(orc::synthetic_u8_tone(4096));
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
        else if(mode=="--capture")print_metrics(capture(device,freq,rate,samples));
        else throw std::invalid_argument("no mode supplied");
        return 0;
    } catch(const std::exception& e) {
        std::cerr<<"orcsdr-cardputer-zero: "<<e.what()<<'\n';
        return 1;
    }
}
