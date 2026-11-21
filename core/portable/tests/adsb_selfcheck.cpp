// SPDX-License-Identifier: AGPL-3.0-only
// Executes the *canonical* decoder's own deterministic Mode S/ADS-B vectors.
#include "adsb_decoder.hpp"

#include <cstdio>

int main() {
    if (!orcsdr::adsb_rx::Decoder::self_check()) {
        std::fputs("ADS-B canonical self_check failed\n", stderr);
        return 1;
    }
    std::puts("ADS-B canonical self_check passed");
    return 0;
}
