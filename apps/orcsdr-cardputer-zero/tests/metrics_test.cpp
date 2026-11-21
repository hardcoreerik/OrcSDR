#include "iq_metrics.hpp"

#include <cmath>
#include <stdexcept>
#include <vector>

int main() {
    const auto positive = orc::analyze_u8_iq(orc::synthetic_u8_tone(4096, 7));
    if (positive.sample_count != 4096 || positive.peak_bin != 7 ||
        !std::isfinite(positive.mean_power_dbfs)) return 1;
    const auto negative = orc::analyze_u8_iq(orc::synthetic_u8_tone(128, -11));
    if (negative.peak_bin != -11) return 2;
    try {
        (void)orc::analyze_u8_iq(std::vector<std::uint8_t>(127));
    } catch (const std::invalid_argument&) {
        return 0;
    }
    return 3;
}
