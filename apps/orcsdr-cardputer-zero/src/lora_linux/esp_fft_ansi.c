// Copyright 2018-2019 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Selected functions from ESP-DSP v1.7.1, fd110921fb57b4de79f092337b9e047130762eca.
// Linux adaptation: caller owns prepared twiddles; no ESP global FFT table.
#include "fft.h"
#include <math.h>
#include <stddef.h>
static int dsp_is_power_of_two(int n){return n>=2 && n<=32768 && (n&(n-1))==0;}
esp_err_t dsps_fft2r_fc32_ansi_(float *data, int N, float *w)
{
    if (!dsp_is_power_of_two(N)) {
        return ESP_ERR_DSP_INVALID_LENGTH;
    }
    if (data == NULL || w == NULL) return ESP_ERR_DSP_INVALID_LENGTH;

    esp_err_t result = ESP_OK;

    int ie, ia, m;
    float re_temp, im_temp;
    float c, s;
    ie = 1;
    for (int N2 = N / 2; N2 > 0; N2 >>= 1) {
        ia = 0;
        for (int j = 0; j < ie; j++) {
            c = w[2 * j];
            s = w[2 * j + 1];
            for (int i = 0; i < N2; i++) {
                m = ia + N2;
                re_temp = c * data[2 * m] + s * data[2 * m + 1];
                im_temp = c * data[2 * m + 1] - s * data[2 * m];
                data[2 * m] = data[2 * ia] - re_temp;
                data[2 * m + 1] = data[2 * ia + 1] - im_temp;
                data[2 * ia] = data[2 * ia] + re_temp;
                data[2 * ia + 1] = data[2 * ia + 1] + im_temp;
                ia++;
            }
            ia += N2;
        }
        ie <<= 1;
    }
    return result;
}

esp_err_t dsps_bit_rev_fc32_ansi(float *data, int N)
{
    if (!dsp_is_power_of_two(N)) {
        return ESP_ERR_DSP_INVALID_LENGTH;
    }

    esp_err_t result = ESP_OK;

    int j, k;
    float r_temp, i_temp;
    j = 0;
    for (int i = 1; i < (N - 1); i++) {
        k = N >> 1;
        while (k <= j) {
            j -= k;
            k >>= 1;
        }
        j += k;
        if (i < j) {
            r_temp = data[j * 2];
            data[j * 2] = data[i * 2];
            data[i * 2] = r_temp;
            i_temp = data[j * 2 + 1];
            data[j * 2 + 1] = data[i * 2 + 1];
            data[i * 2 + 1] = i_temp;
        }
    }
    return result;
}

esp_err_t dsps_gen_w_r2_fc32(float *w, int N)
{
    if (!dsp_is_power_of_two(N)) {
        return ESP_ERR_DSP_INVALID_LENGTH;
    }

    esp_err_t result = ESP_OK;

    int i;
    float e = M_PI * 2.0 / N;

    for (i = 0; i < (N >> 1); i++) {
        w[2 * i] = cosf(i * e);
        w[2 * i + 1] = sinf(i * e);
    }

    return result;
}
