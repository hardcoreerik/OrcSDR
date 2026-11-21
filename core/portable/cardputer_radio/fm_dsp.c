#include "fm_dsp.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define FM_HOT IRAM_ATTR
#else
#define FM_HOT
#endif

#define FM_PI 3.14159265358979323846f
/* All inputs and filter states in this path are finite. Comparisons avoid
 * libm's signaling-NaN checks on every bounded PCM/spectrum sample. */
static float clamp_sample(float value, float low, float high)
{
    return value < low ? low : value > high ? high : value;
}
/* Independent implementation of the quadratic atan approximation reviewed
 * in Sensors 2019, 19, 5148 (Table 2, [1]-b), extended by quadrant symmetry.
 * No libm calls in the discriminator; max angle error is below 0.005 rad. */
static float phase_angle(float y, float x)
{
    const float ax = fabsf(x), ay = fabsf(y);
    if (ax + ay < 1e-20f) return 0;
    const bool steep = ay > ax;
    const float r = steep ? ax / ay : ay / ax;
    float a = r * (FM_PI * 0.25f + 0.273f * (1 - r));
    if (steep) a = FM_PI * 0.5f - a;
    if (x < 0) a = FM_PI - a;
    return y < 0 ? -a : a;
}
static void init_lowpass(orcnode_fm_biquad_t *b, float q, uint32_t rate, float cutoff)
{
    const float w = 2 * FM_PI * cutoff / rate;
    const float c = cosf(w), a = sinf(w) / (2 * q), norm = 1 / (1 + a);
    b->b0 = (1 - c) * 0.5f * norm;
    b->b1 = (1 - c) * norm;
    b->b2 = b->b0;
    b->a1 = -2 * c * norm;
    b->a2 = (1 - a) * norm;
}
static float lowpass(orcnode_fm_biquad_t *b, float x)
{
    const float y = b->b0 * x + b->z1;
    b->z1 = b->b1 * x - b->a1 * y + b->z2;
    b->z2 = b->b2 * x - b->a2 * y;
    return y;
}
int orcnode_fm_dsp_init(orcnode_fm_dsp_t *d, uint32_t input_sps, uint32_t tau_us)
{
    if (!d || (input_sps != 300000u && input_sps != 2560000u && input_sps != 3200000u) ||
        (tau_us != 50 && tau_us != 75)) return -1;
    memset(d, 0, sizeof(*d));
    d->deviation_hz = 75000.0f;
    d->input_sps = input_sps;
    d->mix_fs4 = input_sps != 300000u;
    d->channel_sps = input_sps == 300000u ? 300000u : ORCNODE_FM_CHANNEL_SPS;
    d->decimation = input_sps / d->channel_sps;
    d->rf_scale = 1.0f / d->decimation;
    d->deemph_alpha = 1 - expf(-1.0f / (ORCNODE_FM_AUDIO_SPS * tau_us * 1e-6f));
    /* OrcSDR architecture: complex one-pole channel filter (260 kHz
     * two-sided width), followed by boxcar averaging before decimation. */
    d->rf_alpha = 1 - expf(-FM_PI * 260000.0f / input_sps);
    init_lowpass(&d->lowpass[0], 0.5411961f, d->channel_sps, 15000.0f);
    init_lowpass(&d->lowpass[1], 1.3065630f, d->channel_sps, 15000.0f);
    return 0;
}
int orcnode_nfm_dsp_init(orcnode_fm_dsp_t *d)
{
    if (orcnode_fm_dsp_init(d, 300000u, 75u) != 0) return -1;
    d->deviation_hz = 5000.0f;
    d->rf_alpha = 1 - expf(-FM_PI * 16000.0f / d->input_sps);
    init_lowpass(&d->lowpass[0], 0.5411961f, d->channel_sps, 3500.0f);
    init_lowpass(&d->lowpass[1], 1.3065630f, d->channel_sps, 3500.0f);
    return 0;
}

size_t FM_HOT orcnode_fm_dsp_process(orcnode_fm_dsp_t *d, const uint8_t *iq,
                            size_t bytes, int16_t *mono, size_t capacity)
{
    if (!d || !iq || !mono || !d->decimation || bytes % 2) return 0;
    size_t output = 0;
    for (size_t offset = 0; offset < bytes; offset += 2) {
        float ii = iq[offset] - 127.5f, qq = iq[offset + 1] - 127.5f;
        /* Mix down +Fs/4 using sign changes/swaps, avoiding per-sample trig. */
        const float original_i = ii;
        switch (d->mix_fs4 ? d->mix_phase : 0) {
        case 1: ii = qq; qq = -original_i; break;
        case 2: ii = -ii; qq = -qq; break;
        case 3: ii = -qq; qq = original_i; break;
        }
        d->mix_phase = (d->mix_phase + 1) & 3u;
        d->rf_i += d->rf_alpha * (ii - d->rf_i);
        d->rf_q += d->rf_alpha * (qq - d->rf_q);
        d->rf_i_sum += d->rf_i;
        d->rf_q_sum += d->rf_q;
        if (++d->rf_phase != d->decimation) continue;
        d->rf_phase = 0;
        /* Fixed for this session. Avoid two divides on every channel sample. */
        float i = d->rf_i_sum * d->rf_scale, q = d->rf_q_sum * d->rf_scale;
        d->rf_i_sum = d->rf_q_sum = 0;
        d->rf_power_sum += i * i + q * q;
        ++d->channel_samples;
        if (d->spectrum_pairs < 1024) {
            d->spectrum[2 * d->spectrum_pairs] = (uint8_t)clamp_sample(i + 128, 0, 255);
            d->spectrum[2 * d->spectrum_pairs + 1] = (uint8_t)clamp_sample(q + 128, 0, 255);
            ++d->spectrum_pairs;
        }
        float fm = 0;
        if (d->have_previous)
            fm = phase_angle(d->previous_i * q - d->previous_q * i,
                        d->previous_i * i + d->previous_q * q) *
                 (d->channel_sps / (2 * FM_PI * d->deviation_hz));
        d->previous_i = i; d->previous_q = q; d->have_previous = true;
        float audio = lowpass(&d->lowpass[1], lowpass(&d->lowpass[0], fm));
        d->audio_phase += ORCNODE_FM_AUDIO_SPS;
        if (d->audio_phase >= d->channel_sps) {
            d->audio_phase -= d->channel_sps;
            const float fraction = 1 - (float)d->audio_phase / ORCNODE_FM_AUDIO_SPS;
            const float value = d->previous_audio + fraction * (audio - d->previous_audio);
            d->deemphasis += d->deemph_alpha * (value - d->deemphasis);
            d->dc += 0.0006667f * (d->deemphasis - d->dc);
            const int16_t pcm = (int16_t)clamp_sample((d->deemphasis - d->dc) * 12000, -14000, 14000);
            if (output < capacity) mono[output++] = pcm;
            d->audio_power_sum += (float)pcm * pcm;
            if (abs(pcm) > d->audio_peak) d->audio_peak = abs(pcm);
            ++d->audio_samples;
        }
        d->previous_audio = audio;
    }
    return output;
}
