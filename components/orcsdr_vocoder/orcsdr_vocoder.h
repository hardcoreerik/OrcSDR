// OrcSDR experimental receive-only AMBE+2 speech synthesis adapter.
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#include "mbelib.h"
typedef struct {
    mbe_parms current, previous, enhanced;
    int bad_streak;
    int has_speech;
} orc_vocoder;
enum orc_privacy { ORC_PRIVACY_UNKNOWN=0, ORC_PRIVACY_CLEAR=1, ORC_PRIVACY_PROTECTED=2 };
typedef struct { int corrected; int total; char flags[64]; } orc_vocoder_result;
void orc_vocoder_reset(orc_vocoder *v);
// Nine MSB-first, on-air DMR bytes. Caller owns validated call/slot and privacy.
// One context per stream; serialize calls (upstream mbelib uses rand()).
// Returns 1 if processed, 0 if blocked/invalid. Output is always 160 samples when non-null.
int orc_vocoder_dmr(orc_vocoder *v, const uint8_t wire[9], enum orc_privacy privacy,
                    int16_t pcm[160], orc_vocoder_result *result);
// Soft-reliability variant: c0_soft[12] packs 24 4-bit LLR magnitudes (0..15) for Golay(24,12) C0
// (low nibble even bit j, high nibble odd bit j). When non-null and hard C0 fails (>3 errors),
// evaluates the 24-trial Steiner-sextet soft-ML Golay candidates and rescues high-margin frames.
int orc_vocoder_dmr_soft(orc_vocoder *v, const uint8_t wire[9], const uint8_t c0_soft[12],
                         enum orc_privacy privacy, int16_t pcm[160], orc_vocoder_result *result);
#ifdef __cplusplus
}
#endif
