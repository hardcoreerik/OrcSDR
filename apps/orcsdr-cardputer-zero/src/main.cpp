#include "iq_metrics.hpp"

#include <cstdint>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef ORC_HAVE_RTLSDR
#include <rtl-sdr.h>
#endif

namespace {

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

#ifdef ORC_HAVE_RTLSDR
void list_receivers() {
    const std::uint32_t count = rtlsdr_get_device_count();
    std::cout << "rtl_sdr_devices=" << count << '\n';
    for (std::uint32_t i = 0; i < count; ++i) {
        std::cout << i << ": " << rtlsdr_get_device_name(i) << '\n';
    }
}

std::vector<std::uint8_t> capture(std::uint32_t device, std::uint32_t freq,
                                  std::uint32_t rate, std::uint32_t samples) {
    rtlsdr_dev_t* raw = nullptr;
    if (rtlsdr_open(&raw, device) != 0 || !raw) {
        throw std::runtime_error("could not open RTL-SDR; check USB host, permissions and power");
    }
    struct Closer {
        rtlsdr_dev_t* p;
        ~Closer() { if (p) rtlsdr_close(p); }
    } handle{raw};
    if (rtlsdr_set_sample_rate(raw, rate) != 0 ||
        rtlsdr_set_center_freq(raw, freq) != 0 ||
        rtlsdr_set_tuner_gain_mode(raw, 0) != 0 ||
        rtlsdr_reset_buffer(raw) != 0) {
        throw std::runtime_error("RTL-SDR configuration failed");
    }
    std::vector<std::uint8_t> iq(static_cast<std::size_t>(samples) * 2);
    int bytes_read = 0;
    const int rc = rtlsdr_read_sync(raw, iq.data(), static_cast<int>(iq.size()), &bytes_read);
    if (rc != 0 || bytes_read < 128 || bytes_read % 2) {
        throw std::runtime_error("RTL-SDR synchronous read failed or returned insufficient IQ");
    }
    iq.resize(static_cast<std::size_t>(bytes_read));
    std::cout << "device=" << device << " freq_hz=" << rtlsdr_get_center_freq(raw)
              << " sample_rate=" << rtlsdr_get_sample_rate(raw) << '\n';
    return iq;
}
#endif

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1) { usage(); return 0; }
        std::string mode;
        std::string path;
        std::uint32_t device = 0;
        std::uint32_t freq = 100100000;
        std::uint32_t rate = 1024000;
        std::uint32_t samples = 32768;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--help") { usage(); return 0; }
            if (arg == "--demo" || arg == "--list" || arg == "--capture") {
                if (!mode.empty()) throw std::invalid_argument("select exactly one mode");
                mode = arg;
            } else if (arg == "--iq-file") {
                if (!mode.empty()) throw std::invalid_argument("select exactly one mode");
                mode = arg;
                if (++i >= argc) throw std::invalid_argument("--iq-file needs path");
                path = argv[i];
            } else {
                if (++i >= argc) throw std::invalid_argument("missing option value");
                const std::string value = argv[i];
                if (arg == "--device") device = parse_number(value, 0, 1024);
                else if (arg == "--freq") freq = parse_number(value, 100000, 3000000000U);
                else if (arg == "--rate") rate = parse_number(value, 225000, 3200000);
                else if (arg == "--samples") samples = parse_number(value, 64, 262144);
                else throw std::invalid_argument("unknown option: " + arg);
            }
        }
        if (mode == "--demo") print_metrics(orc::synthetic_u8_tone(4096));
        else if (mode == "--iq-file") {
            std::ifstream in(path, std::ios::binary);
            if (!in) throw std::runtime_error("failed to open IQ file");
            std::vector<std::uint8_t> iq(static_cast<std::size_t>(samples) * 2);
            in.read(reinterpret_cast<char*>(iq.data()), static_cast<std::streamsize>(iq.size()));
            const auto got = in.gcount();
            if (got < 128 || (got % 2)) throw std::runtime_error("IQ file requires at least 64 complete samples");
            iq.resize(static_cast<std::size_t>(got));
            print_metrics(iq);
        }
        else if (mode == "--list" || mode == "--capture") {
#ifdef ORC_HAVE_RTLSDR
            if (mode == "--list") list_receivers();
            else print_metrics(capture(device, freq, rate, samples));
#else
            (void)device; (void)freq; (void)rate;
            throw std::runtime_error("librtlsdr not detected at build time; install librtlsdr-dev and rebuild");
#endif
        }
        else throw std::invalid_argument("no mode supplied");
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "orcsdr-cardputer-zero: " << e.what() << '\n';
        return 1;
    }
}
