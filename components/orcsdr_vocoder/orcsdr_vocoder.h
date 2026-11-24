// OrcSDR experimental receive-only AMBE+2 speech synthesis adapter.
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#include "mbelib.h"
typedef struct {
    mbe_parms current, previous, enhanced;
} orc_vocoder;
enum orc_privacy { ORC_PRIVACY_UNKNOWN=0, ORC_PRIVACY_CLEAR=1, ORC_PRIVACY_PROTECTED=2 };
typedef struct { int corrected; int total; char flags[64]; } orc_vocoder_result;
void orc_vocoder_reset(orc_vocoder *v);
// Nine MSB-first, on-air DMR bytes. Caller owns validated call/slot and privacy.
// One context per stream; serialize calls (upstream mbelib uses rand()).
// Returns 1 if processed, 0 if blocked/invalid. Output is always 160 samples when non-null.
int orc_vocoder_dmr(orc_vocoder *v, const uint8_t wire[9], enum orc_privacy privacy,
                    int16_t pcm[160], orc_vocoder_result *result);
#ifdef __cplusplus
}
#endif
