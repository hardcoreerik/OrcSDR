#include "scan_dsp.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int cmp_float(const void *a, const void *b)
{
    const float fa = *(const float *)a;
    const float fb = *(const float *)b;
    return (fa > fb) - (fa < fb);
}

uint32_t orcnode_bin_hz(uint32_t sps)
{
    return sps / (uint32_t)ORCNODE_FFT_N;
}

int orcnode_max_offset_bins(uint32_t sps, uint32_t usable_hz)
{
    const uint32_t bh = orcnode_bin_hz(sps);
    if (bh == 0) {
        return 0;
    }
    int m = (int)((usable_hz / 2u) / bh);
    if (m < 8) {
        m = 8;
    }
    if (m > ORCNODE_FFT_N / 2 - 1) {
        m = ORCNODE_FFT_N / 2 - 1;
    }
    return m;
}

void orcnode_fft_init(orcnode_fft_t *f)
{
    if (!f) {
        return;
    }
    memset(f, 0, sizeof(*f));
    const int n = ORCNODE_FFT_N;
    int log2n = 0;
    for (int t = n; t > 1; t >>= 1) {
        log2n++;
    }
    for (int i = 0; i < n; ++i) {
        int x = i;
        int y = 0;
        for (int b = 0; b < log2n; ++b) {
            y = (y << 1) | (x & 1);
            x >>= 1;
        }
        f->bitrev[i] = y;
        f->window[i] = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * (float)i / (float)n));
    }
    for (int i = 0; i < n / 2; ++i) {
        const float ang = -2.0f * (float)M_PI * (float)i / (float)n;
        f->tw_re[i] = cosf(ang);
        f->tw_im[i] = sinf(ang);
    }
    f->inited = true;
}

static void fft_radix2(orcnode_fft_t *f)
{
    const int n = ORCNODE_FFT_N;
    for (int i = 0; i < n; ++i) {
        const int j = f->bitrev[i];
        if (i < j) {
            const float tr = f->re[i];
            const float ti = f->im[i];
            f->re[i] = f->re[j];
            f->im[i] = f->im[j];
            f->re[j] = tr;
            f->im[j] = ti;
        }
    }

    for (int len = 2; len <= n; len <<= 1) {
        const int half = len / 2;
        const int step = n / len;
        for (int i = 0; i < n; i += len) {
            for (int j = 0; j < half; ++j) {
                const int k = j * step;
                const float wr = f->tw_re[k];
                const float wi = f->tw_im[k];
                const float ur = f->re[i + j];
                const float ui = f->im[i + j];
                const float vr = wr * f->re[i + j + half] - wi * f->im[i + j + half];
                const float vi = wr * f->im[i + j + half] + wi * f->re[i + j + half];
                f->re[i + j] = ur + vr;
                f->im[i + j] = ui + vi;
                f->re[i + j + half] = ur - vr;
                f->im[i + j + half] = ui - vi;
            }
        }
    }
}

int orcnode_cu8_psd(orcnode_fft_t *f, const uint8_t *cu8, size_t bytes, float *psd_lin)
{
    if (!f || !f->inited || !cu8 || !psd_lin || bytes < (size_t)(2 * ORCNODE_FFT_N)) {
        return -1;
    }
    const int n = ORCNODE_FFT_N;
    float mean_i = 0.0f;
    float mean_q = 0.0f;
    for (int i = 0; i < n; ++i) {
        const float ii = ((float)cu8[2 * i] - 127.5f) / 127.5f;
        const float qq = ((float)cu8[2 * i + 1] - 127.5f) / 127.5f;
        f->re[i] = ii;
        f->im[i] = qq;
        mean_i += ii;
        mean_q += qq;
    }
    mean_i /= (float)n;
    mean_q /= (float)n;
    for (int i = 0; i < n; ++i) {
        f->re[i] = (f->re[i] - mean_i) * f->window[i];
        f->im[i] = (f->im[i] - mean_q) * f->window[i];
    }
    fft_radix2(f);
    const float norm = 1.0f / (float)n;
    for (int i = 0; i < n; ++i) {
        const float r = f->re[i] * norm;
        const float m = f->im[i] * norm;
        psd_lin[i] = r * r + m * m;
    }
    return 0;
}

void orcnode_psd_acc_reset(float *acc)
{
    if (acc) {
        memset(acc, 0, sizeof(float) * (size_t)ORCNODE_FFT_N);
    }
}

void orcnode_psd_acc_add(float *acc, const float *one)
{
    if (!acc || !one) {
        return;
    }
    for (int i = 0; i < ORCNODE_FFT_N; ++i) {
        acc[i] += one[i];
    }
}

void orcnode_psd_acc_mean(float *acc, int frames)
{
    if (!acc || frames <= 0) {
        return;
    }
    const float inv = 1.0f / (float)frames;
    for (int i = 0; i < ORCNODE_FFT_N; ++i) {
        acc[i] *= inv;
    }
}

void orcnode_waterfall_add_hop(float *max_power, uint32_t start_hz, uint32_t stop_hz,
                               uint32_t tune_hz, uint32_t sps, int max_off,
                               int dc_bins, const float *psd_lin)
{
    if (!max_power || !psd_lin || stop_hz <= start_hz || sps == 0 || max_off < 0) {
        return;
    }
    if (max_off >= ORCNODE_FFT_N / 2) max_off = ORCNODE_FFT_N / 2 - 1;
    for (int off = -max_off; off <= max_off; ++off) {
        if (off > -dc_bins && off < dc_bins) continue;
        const int64_t hz = (int64_t)tune_hz + (int64_t)off * sps / ORCNODE_FFT_N;
        if (hz < start_hz || hz >= stop_hz) continue;
        const int col = (int)((hz - start_hz) * ORCNODE_WATERFALL_BINS /
                              (stop_hz - start_hz));
        const float power = psd_lin[off < 0 ? ORCNODE_FFT_N + off : off];
        if (power > max_power[col]) max_power[col] = power;
    }
}

void orcnode_waterfall_render_mean(const float *sum_power, int frames, uint8_t *row)
{
    if (!sum_power || !row || frames <= 0) return;
    for (int i = 0; i < ORCNODE_WATERFALL_BINS; ++i) {
        if (sum_power[i] <= 0.0f) {
            row[i] = 0;
            continue;
        }
        const float power = sum_power[i] / frames;
        if (power <= 1e-8f) row[i] = 1;
        else if (power >= 1e-1f) row[i] = 255;
        else {
            int exponent;
            const float u = 2.0f * frexpf(power, &exponent) - 1.0f;
            /* log2(1+u) polynomial: under 0.03 dB error across one octave. */
            const float db = 3.010299956f * (exponent - 1 + u * (1.3465552f - 0.3465552f * u));
            row[i] = (uint8_t)(1 + (int)((db + 80.0f) * 254.0f / 70.0f + 0.5f));
        }
    }
}

void orcnode_waterfall_render(const float *max_power, uint8_t *row)
{
    orcnode_waterfall_render_mean(max_power, 1, row);
}

static int off_to_bin(int off)
{
    if (off >= 0) {
        return off;
    }
    return ORCNODE_FFT_N + off;
}

void orcnode_estimate_noise(const float *psd_lin, int dc_bins, int max_off,
                            float *scratch, orcnode_noise_t *out)
{
    if (!psd_lin || !scratch || !out) {
        return;
    }
    int n = 0;
    for (int off = -max_off; off <= max_off; ++off) {
        if (off < 0 ? -off < dc_bins : off < dc_bins) {
            continue;
        }
        scratch[n++] = psd_lin[off_to_bin(off)];
    }
    out->n_bins = n;
    if (n <= 0) {
        out->noise_lin = 1e-12f;
        out->noise_db = -120.0f;
        return;
    }
    qsort(scratch, (size_t)n, sizeof(float), cmp_float);
    out->noise_lin = scratch[n / 2];
    if (out->noise_lin < 1e-20f) {
        out->noise_lin = 1e-20f;
    }
    out->noise_db = 10.0f * log10f(out->noise_lin);
}

int orcnode_detect_peaks(const float *psd_lin, uint32_t sps, uint32_t tune_hz,
                         const orcnode_noise_t *noise, float thresh_db,
                         int dc_bins, int max_off,
                         orcnode_peak_t *out, int max_out)
{
    if (!psd_lin || !noise || !out || max_out <= 0) {
        return 0;
    }
    const float thr = noise->noise_lin * powf(10.0f, thresh_db / 10.0f);
    const uint32_t bh = orcnode_bin_hz(sps);
    int count = 0;
    int run_lo = 0;
    bool in_run = false;
    float run_peak = 0.0f;
    int run_peak_off = 0;

    for (int off = -max_off; off <= max_off + 1; ++off) {
        const bool last = (off == max_off + 1);
        float v = 0.0f;
        bool above = false;
        if (!last) {
            const bool dc = (off < 0 ? -off < dc_bins : off < dc_bins);
            v = dc ? 0.0f : psd_lin[off_to_bin(off)];
            above = (!dc && v >= thr);
        }
        if (above) {
            if (!in_run) {
                in_run = true;
                run_lo = off;
                run_peak = v;
                run_peak_off = off;
            } else if (v > run_peak) {
                run_peak = v;
                run_peak_off = off;
            }
        } else if (in_run) {
            if (count < max_out) {
                orcnode_peak_t *p = &out[count++];
                const int hi = off - 1;
                p->bin_lo = off_to_bin(run_lo);
                p->bin_hi = off_to_bin(hi);
                p->bin_peak = off_to_bin(run_peak_off);
                p->peak_lin = run_peak;
                p->peak_db = 10.0f * log10f(run_peak > 1e-20f ? run_peak : 1e-20f);
                p->snr_db = p->peak_db - noise->noise_db;
                p->offset_hz = run_peak_off * (int32_t)bh;
                p->center_hz = (uint32_t)((int64_t)tune_hz + (int64_t)p->offset_hz);
                int width = hi - run_lo + 1;
                if (width < 1) {
                    width = 1;
                }
                p->bw_hz = (uint32_t)width * bh;
                p->dc_risk = (run_lo <= dc_bins && hi >= -dc_bins);
                p->fft_edge = (run_lo <= -max_off + 2) || (hi >= max_off - 2);
            }
            in_run = false;
        }
    }
    return count;
}

int orcnode_plan_hops(uint32_t start_hz, uint32_t stop_hz, uint32_t sps,
                      uint32_t usable_hz, uint32_t overlap_hz,
                      uint32_t *hop_hz, int max_hops)
{
    (void)sps;
    if (!hop_hz || max_hops <= 0 || stop_hz <= start_hz || usable_hz < 100000u) {
        return -1;
    }
    uint32_t step = usable_hz;
    if (overlap_hz < usable_hz) {
        step = usable_hz - overlap_hz;
    }
    if (step < usable_hz / 4u) {
        step = usable_hz / 4u;
    }
    uint32_t center = start_hz + usable_hz / 2u;
    int n = 0;
    while (n < max_hops) {
        hop_hz[n++] = center;
        if ((uint64_t)center + usable_hz / 2u >= stop_hz) return n;
        center += step;
    }
    return -1;
}
