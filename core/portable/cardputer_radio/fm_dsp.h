#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif
#define ORCNODE_FM_CHANNEL_SPS 320000u
#define ORCNODE_FM_AUDIO_SPS 48000u
typedef struct {
    float b0, b1, b2, a1, a2, z1, z2;
} orcnode_fm_biquad_t;
typedef struct {
    uint32_t input_sps, channel_sps, decimation, rf_phase, mix_phase, audio_phase;
    uint32_t spectrum_pairs;
    float rf_alpha, rf_scale, rf_i, rf_q, rf_i_sum, rf_q_sum;
    float previous_i, previous_q, deemphasis, deemph_alpha, dc, previous_audio;
    bool have_previous;
    float deviation_hz;
    bool mix_fs4; /* Disable after init when the wanted station is centered. */
    orcnode_fm_biquad_t lowpass[2];
    uint8_t spectrum[2048];
    float rf_power_sum, audio_power_sum;
    uint32_t channel_samples, audio_samples;
    int audio_peak;
} orcnode_fm_dsp_t;

/* Independent mono FM implementation. Input station is at +Fs/4 relative
 * to the tuner center; 2.56 and 3.2 MS/s share a 320 kS/s channel stage.
 * 300 kS/s captures directly at station center and uses a 300 kS/s stage. */
int orcnode_fm_dsp_init(orcnode_fm_dsp_t *dsp, uint32_t input_sps, uint32_t tau_us);
int orcnode_nfm_dsp_init(orcnode_fm_dsp_t *dsp);
size_t orcnode_fm_dsp_process(orcnode_fm_dsp_t *dsp, const uint8_t *iq,
                            size_t bytes, int16_t *mono, size_t capacity);
#ifdef __cplusplus
}
#endif
