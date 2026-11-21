#pragma once
#ifdef __cplusplus
extern "C" {
#endif
typedef int esp_err_t;
enum {ESP_OK=0,ESP_ERR_DSP_INVALID_LENGTH=-1};
int dsps_fft2r_fc32_ansi_(float*,int,float*);
int dsps_bit_rev_fc32_ansi(float*,int);
int dsps_gen_w_r2_fc32(float*,int);
#ifdef __cplusplus
}
#endif
#define dsps_fft2r_fc32_arp4_ dsps_fft2r_fc32_ansi_
#define dsps_bit_rev_fc32 dsps_bit_rev_fc32_ansi
