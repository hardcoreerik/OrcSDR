#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace orc {

struct IqMetrics {
    std::size_t sample_count{};
    double mean_power_dbfs{};
    int peak_bin{};  // Signed DFT bin, relative to the IQ center.
};

// Diagnostic-only 64-bin DFT. Not a calibrated RF power measurement or
// a replacement for the ESP32 production DSP/spectrum implementations.
inline IqMetrics analyze_u8_iq(const std::vector<std::uint8_t>& iq) {
    constexpr int nfft = 64;
    constexpr double pi = 3.14159265358979323846;
    if (iq.size() < 2 * nfft || (iq.size() & 1U)) {
        throw std::invalid_argument("IQ must be even-sized and contain >=64 complex samples");
    }

    const auto samples = iq.size() / 2;
    double power = 0.0;
    for (std::size_t i = 0; i < iq.size(); i += 2) {
        const double re = static_cast<double>(iq[i]) - 127.5;
        const double im = static_cast<double>(iq[i + 1]) - 127.5;
        power += re * re + im * im;
    }
    power /= static_cast<double>(samples);
    const double full_scale_power = 2.0 * 127.5 * 127.5;

    int best_bin = 0;
    double best_energy = -1.0;
    for (int k = 0; k < nfft; ++k) {
        double out_re = 0.0;
        double out_im = 0.0;
        for (int n = 0; n < nfft; ++n) {
            const double re = static_cast<double>(iq[2 * n]) - 127.5;
            const double im = static_cast<double>(iq[2 * n + 1]) - 127.5;
            const double phase = -2.0 * pi * k * n / nfft;
            const double co = std::cos(phase);
            const double si = std::sin(phase);
            out_re += re * co - im * si;
            out_im += re * si + im * co;
        }
        const double energy = out_re * out_re + out_im * out_im;
        if (energy > best_energy) {
            best_energy = energy;
            best_bin = k;
        }
    }
    return {samples, 10.0 * std::log10(std::max(power / full_scale_power, 1e-12)),
            best_bin >= nfft / 2 ? best_bin - nfft : best_bin};
}

inline std::vector<std::uint8_t> synthetic_u8_tone(std::size_t samples, int dft_bin = 7) {
    constexpr double pi = 3.14159265358979323846;
    std::vector<std::uint8_t> iq(samples * 2);
    for (std::size_t n = 0; n < samples; ++n) {
        const double phase = 2.0 * pi * dft_bin * static_cast<double>(n) / 64.0;
        iq[n * 2] = static_cast<std::uint8_t>(std::lround(127.5 + 60.0 * std::cos(phase)));
        iq[n * 2 + 1] = static_cast<std::uint8_t>(std::lround(127.5 + 60.0 * std::sin(phase)));
    }
    return iq;
}

}  // namespace orc
