#include "orcsdr_vocoder.h"
#include <string.h>
#include <math.h>
// DMR AMBE interleave schedule, from szechyjs/dsd include/dmr_const.h.
// Copyright (C) 2010 DSD Author. ISC notice in THIRD_PARTY.md.
static const unsigned char rw[36]={0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,2,0,2,0,2,0,2,0,2,0,2,0,2};
static const unsigned char rx[36]={23,10,22,9,21,8,20,7,19,6,18,5,17,4,16,3,15,2,14,1,13,0,12,10,11,9,10,8,9,7,8,6,7,5,6,4};
static const unsigned char ry[36]={0,2,0,2,0,2,0,2,0,3,0,3,1,3,1,3,1,3,1,3,1,3,1,3,1,3,1,3,1,3,1,3,1,3,1,3};
static const unsigned char rz[36]={5,3,4,2,3,1,2,0,1,13,0,12,22,11,21,10,20,9,19,8,18,7,17,6,16,5,15,4,14,3,13,2,12,1,11,0};
// Use the existing pinned mbelib Golay generator, not another table copy.
extern const int golayGenerator[12];
static unsigned weight(unsigned v) { unsigned n=0; while(v){n+=v&1u;v>>=1;} return n; }
static unsigned code23(const char *data) {
    unsigned d=0,parity=0;
    for(unsigned i=0;i<12;i++){d=(d<<1)|(unsigned)data[i];if(data[i])parity^=(unsigned)golayGenerator[i];}
    return (d<<11)|parity;
}
static unsigned code23_u(unsigned d12) {
    unsigned parity=0;
    for(unsigned i=0;i<12;i++){if((d12>>(11u-i))&1u)parity^=(unsigned)golayGenerator[i];}
    return ((d12&0xFFFu)<<11)|parity;
}
static unsigned rowword(const char *row,unsigned n) {
    unsigned v=0;while(n){v=(v<<1)|(unsigned)row[--n];}return v;
}
static int try_soft_c0(unsigned c0wire, const uint8_t c0_soft[12], unsigned *out_d12) {
    uint8_t q[24];
    for (unsigned j = 0; j < 24; ++j) {
        q[j] = (j & 1u) ? (uint8_t)((c0_soft[j >> 1] >> 4) & 0x0Fu) : (uint8_t)(c0_soft[j >> 1] & 0x0Fu);
    }
    unsigned seen_d12[24];
    unsigned seen_count = 0;
    unsigned best_cost = 0xFFFFu, second_cost = 0xFFFFu, best_d12 = 0;
    for (unsigned trial = 0; trial < 24; ++trial) {
        const unsigned flip = trial ? (1u << trial) : 0u;
        long int block = (long int)((c0wire ^ flip) >> 1u);
        mbe_checkGolayBlock(&block);
        const unsigned d12 = (unsigned)block & 0xFFFu;
        unsigned duplicate = 0;
        for (unsigned k = 0; k < seen_count; ++k) {
            if (seen_d12[k] == d12) { duplicate = 1; break; }
        }
        if (duplicate) continue;
        seen_d12[seen_count++] = d12;
        const unsigned c23 = code23_u(d12);
        const unsigned c24 = (c23 << 1u) | (weight(c23) & 1u);
        const unsigned diff = c0wire ^ c24;
        if (weight(diff) > 4u) continue;
        unsigned cost = 0;
        for (unsigned j = 0; j < 24; ++j) {
            if ((diff >> j) & 1u) cost += (unsigned)q[j];
        }
        if (cost < best_cost) {
            second_cost = best_cost;
            best_cost = cost;
            best_d12 = d12;
        } else if (cost < second_cost) {
            second_cost = cost;
        }
    }
    if (best_cost <= 24u && second_cost < 0xFFFFu &&
        best_cost + 10u <= second_cost && best_cost * 2u <= second_cost) {
        *out_d12 = best_d12;
        return 1;
    }
    return 0;
}
void orc_vocoder_reset(orc_vocoder *v) {
    if (v) { memset(v,0,sizeof(*v)); mbe_initMbeParms(&v->current,&v->previous,&v->enhanced); }
}
int orc_vocoder_dmr(orc_vocoder *v,const uint8_t wire[9],enum orc_privacy privacy,int16_t pcm[160],orc_vocoder_result *result) {
    return orc_vocoder_dmr_soft(v, wire, NULL, privacy, pcm, result);
}
int orc_vocoder_dmr_soft(orc_vocoder *v,const uint8_t wire[9],const uint8_t c0_soft[12],enum orc_privacy privacy,int16_t pcm[160],orc_vocoder_result *result) {
    if (result) memset(result,0,sizeof(*result));
    if (!pcm) return 0;
    memset(pcm,0,160*sizeof(*pcm));
    if (!v || !wire || privacy!=ORC_PRIVACY_CLEAR) { orc_vocoder_reset(v); return 0; }
    char matrix[4][24]={{0}}, raw_matrix[4][24]={{0}}, data[49]={0}, errors[64]={0};
    int c0=0,total=0,c1_errs=0,soft_rescued=0;
    for (unsigned i=0;i<36;i++) {
        unsigned d=(wire[i/4]>>(6-2*(i%4)))&3;
        matrix[rw[i]][rx[i]]=(char)(d>>1); matrix[ry[i]][rz[i]]=(char)(d&1);
    }
    memcpy(raw_matrix, matrix, sizeof(matrix));
    const unsigned c0wire=rowword(matrix[0],24);
    (void)mbe_eccAmbe3600x2450C0(matrix);
    mbe_demodulateAmbe3600x2450Data(matrix);
    unsigned c1wire=rowword(matrix[1],23);
    (void)mbe_eccAmbe3600x2450Data(matrix,data);
    unsigned c0code=code23(data);
    c0=(int)weight(c0wire ^ ((c0code<<1)|(weight(c0code)&1u)));
    c1_errs=(int)weight(c1wire ^ code23(data+12));
    total=c0+c1_errs;
    if (c0 > 3 && c0_soft) {
        unsigned best_d12 = 0;
        if (try_soft_c0(c0wire, c0_soft, &best_d12)) {
            memcpy(matrix, raw_matrix, sizeof(matrix));
            for (unsigned i = 0; i < 12; ++i) {
                matrix[0][23u - i] = (char)((best_d12 >> (11u - i)) & 1u);
            }
            mbe_demodulateAmbe3600x2450Data(matrix);
            c1wire = rowword(matrix[1], 23);
            (void)mbe_eccAmbe3600x2450Data(matrix, data);
            c0code = code23(data);
            c0 = (int)weight(c0wire ^ ((c0code << 1) | (weight(c0code) & 1u)));
            c1_errs = (int)weight(c1wire ^ code23(data + 12));
            total = c0 + c1_errs;
            soft_rescued = 1;
        }
    }
    // Extended Golay C0 detects a fourth error; conceal 1-2 bad frames during active speech before resetting.
    if (c0 > 3 && !soft_rescued) {
        if (v->has_speech && v->bad_streak < 2) {
            v->bad_streak++;
            float buf[160] = {0};
            mbe_useLastMbeParms(&v->current, &v->previous);
            v->current.repeat = v->bad_streak;
            mbe_moveMbeParms(&v->current, &v->previous);
            const float ml_damp = (v->bad_streak == 1) ? 0.88f : 0.70f;
            const int max_l = (v->current.L > 56) ? 56 : (v->current.L < 0 ? 0 : v->current.L);
            for (int l = 1; l <= max_l; ++l) v->current.Ml[l] *= ml_damp;
            mbe_spectralAmpEnhance(&v->current);
            mbe_synthesizeSpeechf(buf, &v->current, &v->enhanced, 3);
            mbe_moveMbeParms(&v->current, &v->enhanced);
            float peak = 0.0f;
            int valid = 1;
            for (unsigned i = 0; i < 160; i++) {
                if (!isfinite(buf[i])) { valid = 0; break; }
                float a = fabsf(buf[i]);
                if (a > peak) peak = a;
            }
            if (!valid) {
                orc_vocoder_reset(v);
                strcpy(errors, "E");
            } else {
                const float att = v->bad_streak == 1 ? 0.75f : 0.45f;
                const float gain = (peak > 8000.0f ? 12000.0f / peak : 1.5f) * att;
                for (unsigned i = 0; i < 160; i++) {
                    const float fade = (v->bad_streak == 2 && i >= 120u)
                        ? 0.5f * (1.0f + cosf(3.14159265f * (float)(i - 120u + 1u) / 40.0f))
                        : 1.0f;
                    pcm[i] = (int16_t)(buf[i] * gain * fade);
                }
                strcpy(errors, "ER");
            }
        } else {
            orc_vocoder_reset(v);
            strcpy(errors, "E");
        }
    } else {
        unsigned b0 = ((unsigned)data[0] << 6) | ((unsigned)data[1] << 5) | ((unsigned)data[2] << 4) |
                      ((unsigned)data[3] << 3) | ((unsigned)data[37] << 2) | ((unsigned)data[38] << 1) | (unsigned)data[39];
        float buf[160] = {0};
        int synth_c0 = soft_rescued ? 0 : c0;
        int synth_total = soft_rescued ? c1_errs : total;
        mbe_processAmbe2450Dataf(buf, &synth_c0, &synth_total, errors, data, &v->current, &v->previous, &v->enhanced, 3);
        // With c0 <= 3, b0 in 120..125 are valid non-speech DTX/erasure/comfort-silence frames
        // (e.g. ETSI null ACAA40200044408080 b0=120, MotoTRBO Cap+ DTX ACAA40210044408080 b0=122).
        if (b0 >= 120 && b0 <= 125) {
            v->bad_streak = 0;
            v->has_speech = 0;
            size_t w = 0;
            int has_s = 0;
            for (size_t i = 0; errors[i]; ++i) {
                if (errors[i] == 'E') continue;
                if (errors[i] == 'S') has_s = 1;
                errors[w++] = errors[i];
            }
            if (!has_s && w + 1 < sizeof(errors)) errors[w++] = 'S';
            errors[w] = 0;
        } else if (b0 == 126 || b0 == 127) {
            v->bad_streak = 0;
            v->has_speech = 0;
        } else {
            float peak = 0.0f;
            int valid = 1;
            for (unsigned i = 0; i < 160; i++) {
                if (!isfinite(buf[i])) { valid = 0; break; }
                float a = fabsf(buf[i]);
                if (a > peak) peak = a;
            }
            if (!valid) {
                orc_vocoder_reset(v);
                strcpy(errors, "E");
            } else {
                const float gain = peak > 8000.0f ? 12000.0f / peak : 1.5f;
                for (unsigned i = 0; i < 160; i++) pcm[i] = (int16_t)(buf[i] * gain);
                v->bad_streak = 0;
                v->has_speech = 1;
            }
        }
    }
    if (result) {result->corrected=c0;result->total=total;memcpy(result->flags,errors,sizeof(errors));}
    return 1;
}
