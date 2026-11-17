#pragma once
#include "rate_defaults.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ORCNODE_FFT_N 1024
#define ORCNODE_WATERFALL_BINS 512
#define ORCNODE_SCAN_SPS ORCNODE_DEFAULT_SPS
/* Geometry reference stays fixed when the default rate changes. */
#define ORCNODE_SCAN_REFERENCE_SPS 2400000u
#define ORCNODE_SCAN_USABLE_HZ 1800000u
#define ORCNODE_SCAN_OVERLAP_HZ 200000u
#define ORCNODE_SCAN_DWELL_MS 150u
#define ORCNODE_SCAN_SETTLE_MS 25u
#define ORCNODE_DETECT_THRESH_DB 7.0f
#define ORCNODE_CONFIRM_THRESH_DB 10.0f
#define ORCNODE_DC_BINS 4
#define ORCNODE_MAX_PEAKS 16
#define ORCNODE_MAX_HOPS 48
#define ORCNODE_SNR_DETECT_ONLY_DB 6.0f
#define ORCNODE_SNR_UNCERTAIN_DB 10.0f

typedef struct {
    float re[ORCNODE_FFT_N];
    float im[ORCNODE_FFT_N];
    float window[ORCNODE_FFT_N];
    float tw_re[ORCNODE_FFT_N / 2];
    float tw_im[ORCNODE_FFT_N / 2];
    int bitrev[ORCNODE_FFT_N];
    float scratch[ORCNODE_FFT_N];
    bool inited;
} orcnode_fft_t;

typedef struct {
    float noise_lin;
    float noise_db;
    int n_bins;
} orcnode_noise_t;

typedef struct {
    int bin_lo;
    int bin_hi;
    int bin_peak;
    float peak_lin;
    float peak_db;
    float snr_db;
    int32_t offset_hz;
    uint32_t center_hz;
    uint32_t bw_hz;
    bool dc_risk;
    bool fft_edge;
} orcnode_peak_t;

void orcnode_fft_init(orcnode_fft_t *f);

/** Convert CU8 frame (2*N bytes) to linear PSD[N] (not dB). Returns 0 on success. */
int orcnode_cu8_psd(orcnode_fft_t *f, const uint8_t *cu8, size_t bytes, float *psd_lin);

void orcnode_psd_acc_reset(float *acc);
void orcnode_psd_acc_add(float *acc, const float *one);
void orcnode_psd_acc_mean(float *acc, int frames);

/* Reuse the scanner's averaged FFT to stitch one full-band waterfall row.
 * The highest power in each display column preserves narrow signals. */
void orcnode_waterfall_add_hop(float *max_power, uint32_t start_hz, uint32_t stop_hz,
                               uint32_t tune_hz, uint32_t sps, int max_off,
                               int dc_bins, const float *psd_lin);
/* 0 means no coverage; 1..255 maps roughly -80..-10 dBFS per FFT bin. */
void orcnode_waterfall_render(const float *max_power, uint8_t *row);
void orcnode_waterfall_render_mean(const float *sum_power, int frames, uint8_t *row);

int orcnode_max_offset_bins(uint32_t sps, uint32_t usable_hz);

void orcnode_estimate_noise(const float *psd_lin, int dc_bins, int max_off,
                            float *scratch, orcnode_noise_t *out);

int orcnode_detect_peaks(const float *psd_lin, uint32_t sps, uint32_t tune_hz,
                         const orcnode_noise_t *noise, float thresh_db,
                         int dc_bins, int max_off,
                         orcnode_peak_t *out, int max_out);

/** Build hop table. Returns hop count, or <0 on error. */
int orcnode_plan_hops(uint32_t start_hz, uint32_t stop_hz, uint32_t sps,
                      uint32_t usable_hz, uint32_t overlap_hz,
                      uint32_t *hop_hz, int max_hops);

uint32_t orcnode_bin_hz(uint32_t sps);

#ifdef __cplusplus
}
#endif
